package org.cryomonitor.companion

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Intent
import android.content.pm.ServiceInfo
import android.media.AudioManager
import android.media.ToneGenerator
import android.os.BatteryManager
import android.os.Build
import android.os.IBinder
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

/**
 * The always-on hub (OpenSeizureDetector SdServer pattern):
 * PebbleTransport in, server heartbeats out, watch watchdog in between,
 * alarms to AlarmActivity + server escalation with phone-direct fallback.
 */
class MonitorService : Service(), PebbleTransport.Listener {

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)
    private lateinit var watchLink: WatchLink
    private lateinit var settings: SettingsStore
    private lateinit var server: ServerClient
    private lateinit var escalator: Escalator

    @Volatile private var lastWatchDataT = 0L
    @Volatile private var watchBattery: Int? = null
    @Volatile private var drillT0 = 0L   // S1 latency drill start (epoch ms)
    @Volatile private var chargingHold = false  // watch on charger = paused
    @Volatile private var workerLastRecT = 0L   // last DataLogging record (arrival)
    @Volatile private var workerFaultNotified = false
    private val dlFlushLatencies = ArrayDeque<Long>() // S5 stats, last 30
    @Volatile private var watchConnected = false
    @Volatile private var faultNotified = false
    @Volatile private var serverReachable = true
    @Volatile private var activeEscalationId: String? = null
    @Volatile private var degradedNotified = false
    @Volatile private var suspendedUntilT = 0L
    @Volatile private var sirenOn = false
    private var tone: ToneGenerator? = null

    override fun onCreate() {
        super.onCreate()
        settings = SettingsStore(this)
        CmLog.init(this)
        server = ServerClient(settings)
        escalator = Escalator(this, settings)
        CmLog.i(TAG, "service starting, server=${settings.serverUrl.isNotEmpty()}")
        startForegroundSafely(buildNotification("Starting…"))

        watchLink = WatchLink(context = this, scope = scope, listener = this)
        watchLink.start()
        watchLink.startWatchapp() // also relaunches the background worker

        scope.launch { watchdogLoop() }
        scope.launch { serverHeartbeatLoop() }
    }

    /**
     * Android 14+ enforces per-type prerequisites for foreground services:
     * connectedDevice requires a granted Bluetooth permission. If the user
     * hasn't granted BLUETOOTH_CONNECT yet, fall back to specialUse, then to
     * the untyped legacy call — a life-safety service must never crash the
     * app because a permission dialog hasn't been answered yet.
     */
    private fun startForegroundSafely(n: Notification) {
        if (Build.VERSION.SDK_INT < 34) {
            startForeground(NOTIF_ID, n)
            return
        }
        val attempts = listOf(
            "connectedDevice" to ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE,
            "specialUse" to ServiceInfo.FOREGROUND_SERVICE_TYPE_SPECIAL_USE)
        for ((name, type) in attempts) {
            try {
                startForeground(NOTIF_ID, n, type)
                CmLog.i(TAG, "foreground started with type=$name")
                return
            } catch (e: Exception) {
                CmLog.w(TAG, "startForeground($name) failed: $e")
            }
        }
        try {
            startForeground(NOTIF_ID, n)
            CmLog.w(TAG, "foreground started untyped (legacy fallback)")
        } catch (e: Exception) {
            CmLog.e(TAG, "all startForeground attempts failed — running degraded", e)
        }
    }

    // ---- PebbleTransport.Listener ----

    override fun onAppMessage(data: Map<Int, Any>) {
        lastWatchDataT = System.currentTimeMillis()
        faultNotified = false
        (data[PebbleTransport.KEY_WATCH_BATTERY] as? Int)?.let { watchBattery = it }

        when (data[PebbleTransport.KEY_MSG_TYPE] as? Int) {
            Protocol.PMSG_HEARTBEAT -> {
                // Re-sync the debug flag while the watchapp is open: a
                // toggle flipped while the app was closed would otherwise
                // be lost (AppMessage needs an open inbox) — the field
                // finding behind "no debug line on the watch".
                watchLink.send(mapOf(
                    PebbleTransport.KEY_MSG_TYPE to Protocol.PMSG_SET_DEBUG,
                    PebbleTransport.KEY_SECONDS to
                        (if (settings.debugLogging) 1 else 0)))
                CmLog.d(TAG, "watch heartbeat seq=${data[PebbleTransport.KEY_HEARTBEAT_SEQ]} " +
                    "batt=$watchBattery")
                updateNotification()
            }
            Protocol.PMSG_PRE_ALARM -> {
                val det = detectorName(data)
                CmLog.i(TAG, "PRE-ALARM from watch: $det")
                showAlarmUi(det, preAlarm = true)
            }
            Protocol.PMSG_ALARM -> {
                val det = detectorName(data)
                CmLog.i(TAG, "ALARM from watch: $det")
                startSiren()
                showAlarmUi(det, preAlarm = false)
                scope.launch { escalate(det) }
            }
            Protocol.PMSG_CANCEL -> {
                CmLog.i(TAG, "watch cancelled alert (reason=" +
                    "${data[PebbleTransport.KEY_CANCEL_REASON]})")
                clearAlarmUi()
                sendBroadcast(Intent(ACTION_ALERT_CANCELLED).setPackage(packageName))
                scope.launch { retract("cancelled_on_watch") }
            }
            Protocol.PMSG_SUSPENDED -> {
                // SECONDS carries the suspension duration; 0 = ended
                // (expired or auto-resumed). Wall-clock deadline self-clears
                // even if the end message is lost.
                val secs = (data[PebbleTransport.KEY_SECONDS] as? Int) ?: 0
                suspendedUntilT = if (secs > 0)
                    System.currentTimeMillis() + secs * 1000L else 0L
                CmLog.i(TAG, "suspension from watch: " +
                    if (secs > 0) "for ${secs}s" else "ended")
                updateNotification()
            }
            Protocol.PMSG_DRILL_RESULT -> {
                val launchMs = (data[PebbleTransport.KEY_SECONDS] as? Int) ?: 0
                // The watch reports its own arm->result time; subtracting it
                // from our round trip leaves pure BT transport (both ways).
                // Never guess the countdown duration — it is tick-aligned.
                val watchMs = (data[PebbleTransport.KEY_HEARTBEAT_SEQ] as? Int) ?: 0
                val rtt = if (drillT0 > 0)
                    System.currentTimeMillis() - drillT0 else null
                val transport = if (rtt != null && watchMs > 0) rtt - watchMs else null
                drillT0 = 0
                CmLog.i(TAG, "LATENCY DRILL: worker->app launch=${launchMs}ms " +
                    "watch-total=${watchMs}ms rtt=${rtt}ms bt-transport=${transport}ms " +
                    "model=${Build.MODEL}")
                scope.launch {
                    if (server.configured)
                        server.drillResult(launchMs, rtt, watchMs, transport,
                                           Build.MODEL)
                }
            }
            Protocol.PMSG_CHARGING -> {
                chargingHold = ((data[PebbleTransport.KEY_SECONDS] as? Int) ?: 0) > 0
                CmLog.i(TAG, "watch charging hold: $chargingHold")
                updateNotification()
            }
            Protocol.PMSG_NOTWORN -> {
                CmLog.w(TAG, "watch reports not worn")
                notifyFault("Watch appears OFF-WRIST (no pulse, no motion) " +
                    "without a suspension — monitoring is blind. Re-wear the " +
                    "watch or suspend monitoring. Contacts are NOT alerted.")
            }
        }
    }

    override fun onConnectionChanged(connected: Boolean) {
        val was = watchConnected
        watchConnected = connected
        if (connected && !was) {
            // Reconnect (or reboot) self-heal: relaunch the watchapp so the
            // worker restarts on fresh code; the stale-launch guard hands
            // the screen back to the watchface within seconds.
            CmLog.i(TAG, "watch reconnected — relaunching watchapp/worker")
            watchLink.startWatchapp()
        } else if (!connected && was) {
            CmLog.w(TAG, "watch connection LOST")
        }
        updateNotification()
    }

    private fun detectorName(data: Map<Int, Any>): String {
        val idx = data[PebbleTransport.KEY_DETECTOR] as? Int ?: return "unknown"
        return Protocol.DETECTOR_NAMES.getOrElse(idx) { "unknown" }
    }

    // ---- escalation ----

    private fun escalate(detector: String, isTest: Boolean = false) {
        val kind = if (isTest) "test" else "watch_alarm"
        val loc = escalator.lastKnownLocation()
        val escId = server.alarm(detector, kind, loc?.first, loc?.second)
        CmLog.i(TAG, "escalate det=$detector test=$isTest loc=$loc serverEsc=$escId")
        activeEscalationId = escId
        serverReachable = escId != null
        // SMS fires regardless (redundant path); Telegram-direct only when
        // the server (which owns Telegram with ACK buttons) is unreachable.
        escalator.fire(detector, isTest)
        updateNotification()
    }

    private fun retract(reason: String) {
        CmLog.i(TAG, "retract: $reason (esc=$activeEscalationId)")
        clearAlarmUi()
        activeEscalationId?.let { server.resolve(it, "false_alarm") }
        activeEscalationId = null
        escalator.cancel(reason)
    }

    // ---- alarm surface (T2 fix) ----
    //
    // startActivity() from a background service is silently discarded on
    // Android 10+ (background-activity-launch restriction) — the reason the
    // full-screen alarm never appeared during E2E T2 while Telegram and the
    // dashboard fired fine. The reliable path is a full-screen-intent
    // notification: the system itself launches AlarmActivity when the screen
    // is off/locked, and shows an urgent heads-up otherwise. The siren is
    // service-owned so a bystander hears it even if no UI ever launches.

    private fun showAlarmUi(detector: String, preAlarm: Boolean) {
        val nm = getSystemService(NotificationManager::class.java)
        nm.createNotificationChannel(NotificationChannel(
            CHANNEL_ALARM, "Alarms", NotificationManager.IMPORTANCE_HIGH))
        if (Build.VERSION.SDK_INT >= 34 && !nm.canUseFullScreenIntent()) {
            CmLog.w(TAG, "full-screen intents NOT permitted — alarm shows " +
                "as heads-up only (Settings > Apps > Special access)")
        }
        val pi = PendingIntent.getActivity(this, 0,
            Intent(this, AlarmActivity::class.java)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                .putExtra("detector", detector)
                .putExtra("preAlarm", preAlarm),
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE)
        nm.notify(NOTIF_ALARM_ID, Notification.Builder(this, CHANNEL_ALARM)
            .setContentTitle(if (preAlarm) "PRE-ALARM: $detector"
                             else "ALARM: $detector — contacts being alerted")
            .setContentText("Tap to open. Cancel there if you are OK.")
            .setSmallIcon(android.R.drawable.stat_notify_error)
            .setCategory(Notification.CATEGORY_ALARM)
            .setFullScreenIntent(pi, true)
            .setOngoing(true)
            .build())
        // Direct launch still works whenever we do hold launch privilege
        // (app visible or recently visible); harmless no-op otherwise.
        AlarmActivity.launch(this, detector, preAlarm)
    }

    private fun clearAlarmUi() {
        stopSiren()
        getSystemService(NotificationManager::class.java).cancel(NOTIF_ALARM_ID)
    }

    private fun startSiren() {
        if (sirenOn) return
        sirenOn = true
        tone = ToneGenerator(AudioManager.STREAM_ALARM, ToneGenerator.MAX_VOLUME)
        scope.launch {
            while (sirenOn) {
                runCatching {
                    tone?.startTone(ToneGenerator.TONE_CDMA_EMERGENCY_RINGBACK, 800)
                }
                delay(1000)
            }
        }
    }

    private fun stopSiren() {
        sirenOn = false
        tone?.stopTone()
        tone?.release()
        tone = null
    }

    // ---- loops ----

    private suspend fun watchdogLoop() {
        var flushTick = 0
        while (true) {
            // While Bluetooth is up, app-message silence is EXPECTED in
            // worker mode (the worker cannot send AppMessages; the phone
            // only hears the watch while the watchapp is open). A fault is
            // only a fault when the LINK is gone — or, once DataLogging
            // heartbeats have ever flowed, when they stop (worker evicted).
            val silentFor = (System.currentTimeMillis() - lastWatchDataT) / 1000
            if (!watchConnected && lastWatchDataT > 0 &&
                silentFor > Protocol.WATCH_SILENT_AFTER_S && !faultNotified) {
                faultNotified = true
                CmLog.w(TAG, "watch watchdog: link down, silent ${silentFor}s")
                notifyFault("Watch link lost — check Bluetooth and the " +
                    "watch battery. Monitoring on the watch continues, but " +
                    "alarms cannot reach this phone until the link returns.")
                watchLink.startWatchapp() // best-effort self-heal on reconnect
            }
            // Worker-eviction watchdog: only armed once records have ever
            // arrived (so it stays quiet until S5 proves the channel).
            val workerSilent = (System.currentTimeMillis() - workerLastRecT) / 1000
            if (watchConnected && workerLastRecT > 0 &&
                workerSilent > Protocol.WORKER_SILENT_AFTER_S &&
                !workerFaultNotified) {
                workerFaultNotified = true
                CmLog.w(TAG, "worker heartbeats stopped (${workerSilent}s) — evicted?")
                notifyFault("The watch background worker stopped reporting " +
                    "(possibly evicted by another background app). " +
                    "Relaunching the watchapp to restore monitoring.")
                watchLink.startWatchapp() // relaunching the app re-arms the worker
            }
            // Ask the phone's Pebble app to flush buffered worker records
            // once a minute (every 4th 15 s tick).
            if (flushTick++ % 4 == 0) DataLogReceiver.requestFlush(this)
            updateNotification()
            delay(15_000) // also re-posts the notification (OSD pattern)
        }
    }

    private fun humanAge(ms: Long): String {
        val s = ms / 1000
        return when {
            s < 90 -> "${s}s"
            s < 5400 -> "${s / 60}m"
            else -> "%.1fh".format(s / 3600.0)
        }
    }

    private suspend fun serverHeartbeatLoop() {
        while (true) {
            if (server.configured) {
                val bm = getSystemService(BatteryManager::class.java)
                val pct = bm.getIntProperty(BatteryManager.BATTERY_PROPERTY_CAPACITY)
                val age = if (lastWatchDataT == 0L) null
                          else ((System.currentTimeMillis() - lastWatchDataT) / 1000).toInt()
                val ack = server.heartbeat(pct, watchBattery, age,
                                           lowBatteryWarning = pct in 1..15)
                val ok = ack != null
                CmLog.d(TAG, "server heartbeat ok=$ok phoneBatt=$pct watchAge=$age " +
                    "degraded=${ack?.degraded}")
                if (ack != null) onDegradedState(ack.degraded)
                if (ack?.command == "latency_drill") runLatencyDrill()
                if (!ok && serverReachable) {
                    serverReachable = false
                    CmLog.w(TAG, "server unreachable")
                    notifyFault("Server unreachable — phone-direct escalation active.")
                } else if (ok && !serverReachable) {
                    serverReachable = true
                    CmLog.i(TAG, "server reachable again")
                }
            }
            delay(300_000)
        }
    }

    // ---- commands from activities ----

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            ACTION_USER_CANCEL -> {
                watchLink.send(mapOf(
                    PebbleTransport.KEY_MSG_TYPE to Protocol.PMSG_USER_OK_REMOTE))
                scope.launch { retract(intent.getStringExtra("cause") ?: "cancelled_on_phone") }
            }
            ACTION_TEST_ALARM -> {
                CmLog.i(TAG, "fire-drill TEST alarm requested")
                scope.launch { escalate("test", isTest = true) }
            }
            ACTION_HEARTBEAT_NOW -> scope.launch {
                val bm = getSystemService(BatteryManager::class.java)
                val pct = bm.getIntProperty(BatteryManager.BATTERY_PROPERTY_CAPACITY)
                val ack = if (server.configured)
                    server.heartbeat(pct, watchBattery, null,
                                     lowBatteryWarning = false) else null
                serverReachable = ack != null
                if (ack != null) onDegradedState(ack.degraded)
                CmLog.i(TAG, "manual heartbeat: ${if (ack != null) "OK" else "FAILED"} " +
                    "(url=${settings.serverUrl.ifEmpty { "unset" }}, " +
                    "result=${server.lastResult})")
                updateNotification()
            }
            ACTION_LATENCY_DRILL -> runLatencyDrill()
            ACTION_WORKER_HEARTBEAT -> {
                // The background worker's DataLogging record made it over —
                // this is watch liveness WITHOUT the watchapp being open
                // (M0 S5). Treat it as watch data: the synced age, watch
                // battery, and the server's watch_data_age become honest.
                workerLastRecT = System.currentTimeMillis()
                lastWatchDataT = workerLastRecT
                intent.getIntExtra("battery", -1)
                    .takeIf { it in 0..100 }?.let { watchBattery = it }
                val flushS = intent.getLongExtra("flush_s", -1)
                if (flushS >= 0) {
                    synchronized(dlFlushLatencies) {
                        dlFlushLatencies.addLast(flushS)
                        while (dlFlushLatencies.size > 30) dlFlushLatencies.removeFirst()
                        val median = dlFlushLatencies.sorted()[dlFlushLatencies.size / 2]
                        CmLog.i(TAG, "S5: worker record flush=${flushS}s " +
                            "median=${median}s over ${dlFlushLatencies.size}")
                    }
                }
                if (workerFaultNotified) {
                    workerFaultNotified = false
                    CmLog.i(TAG, "worker heartbeats resumed")
                }
                updateNotification()
            }
            ACTION_SET_DEBUG -> {
                val on = intent.getBooleanExtra("enabled", false)
                CmLog.debugEnabled = on
                CmLog.i(TAG, "debug logging ${if (on) "ENABLED" else "disabled"} " +
                    "(pushing to watch)")
                watchLink.send(mapOf(
                    PebbleTransport.KEY_MSG_TYPE to Protocol.PMSG_SET_DEBUG,
                    PebbleTransport.KEY_SECONDS to (if (on) 1 else 0)))
            }
        }
        return START_STICKY
    }

    /**
     * S1 latency drill: measure the real worker->launch->AppMessage alarm
     * path. Opens the watchapp, arms the worker (which waits a fixed
     * DRILL_DELAY after the app exits, then fires a synthetic alert through
     * the genuine cold path), and times the round trip. Results land in
     * CmLog and the server event feed with the phone model attached.
     */
    private fun runLatencyDrill() {
        CmLog.i(TAG, "latency drill: starting (open watchapp, arm worker)")
        scope.launch {
            watchLink.startWatchapp()
            delay(3_000) // let the watchapp come up and register its inbox
            drillT0 = System.currentTimeMillis()
            watchLink.send(mapOf(
                PebbleTransport.KEY_MSG_TYPE to Protocol.PMSG_DRILL))
            // Result arrives as PMSG_DRILL_RESULT in ~DRILL_DELAY + latency.
        }
    }

    /** DEGRADED = alarms would reach nobody. That is a fault, treated like
     *  one: FAULT-channel notification on the transition into degraded. */
    private fun onDegradedState(degraded: Boolean) {
        if (degraded && !degradedNotified) {
            degradedNotified = true
            notifyFault("No emergency contacts configured — alarms currently " +
                "reach NOBODY except this phone. Open Contacts & safety net.")
        } else if (!degraded && degradedNotified) {
            degradedNotified = false
            CmLog.i(TAG, "degraded cleared: deliverable contacts exist")
        }
    }

    // ---- notifications ----

    private fun buildNotification(text: String): Notification {
        val nm = getSystemService(NotificationManager::class.java)
        nm.createNotificationChannel(NotificationChannel(
            CHANNEL_ID, "Monitoring", NotificationManager.IMPORTANCE_LOW))
        nm.createNotificationChannel(NotificationChannel(
            CHANNEL_FAULT, "System faults", NotificationManager.IMPORTANCE_HIGH))
        // The status-bar glyph tells the state at a glance: heartbeat trace
        // = all well; bluetooth-off = watch link down (the critical leg);
        // cloud-off = server leg down (phone-direct fallback active).
        val icon = when {
            !watchConnected -> R.drawable.ic_stat_watch_off
            server.configured && !serverReachable -> R.drawable.ic_stat_server_off
            else -> R.drawable.ic_stat_monitor
        }
        return Notification.Builder(this, CHANNEL_ID)
            .setContentTitle("Cryonics Monitor")
            .setContentText(text)
            .setSmallIcon(icon)
            .setOngoing(true)
            .setContentIntent(openAppIntent())
            .build()
    }

    private fun openAppIntent(): PendingIntent =
        PendingIntent.getActivity(this, 0,
            Intent(this, MainActivity::class.java)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK),
            PendingIntent.FLAG_IMMUTABLE)

    private fun notifyFault(text: String) {
        val nm = getSystemService(NotificationManager::class.java)
        nm.notify(NOTIF_FAULT_ID, Notification.Builder(this, CHANNEL_FAULT)
            .setContentTitle("Cryonics Monitor FAULT")
            .setContentText(text)
            .setStyle(Notification.BigTextStyle().bigText(text))
            .setSmallIcon(android.R.drawable.stat_notify_error)
            .setContentIntent(openAppIntent())
            .build())
    }

    private fun updateNotification() {
        val nm = getSystemService(NotificationManager::class.java)
        nm.notify(NOTIF_ID, buildNotification(statusLine()))
    }

    private fun statusLine(): String {
        // "link" is live Bluetooth truth; "sync" is when the watchAPP last
        // spoke (only possible while it is open) — two different facts that
        // must not be conflated into one scary number.
        val link = if (watchConnected) "watch ✓" else "watch LINK DOWN"
        val wb = watchBattery?.let { " ${it}%" } ?: ""
        val sync = if (lastWatchDataT == 0L) ""
                   else " · synced ${humanAge(
                       System.currentTimeMillis() - lastWatchDataT)} ago"
        val srv = when {
            !server.configured -> "no server"
            serverReachable -> "server ✓"
            else -> "SERVER: ${server.lastResult}"
        }
        val suspLeft = suspendedUntilT - System.currentTimeMillis()
        val susp = when {
            chargingHold -> "ON CHARGER · "
            suspLeft > 0 -> "SUSPENDED ${(suspLeft / 60000) + 1}m · "
            else -> ""
        }
        return "$susp$link$wb$sync · $srv"
    }

    override fun onDestroy() {
        stopSiren()
        watchLink.stop()
        scope.cancel()
        super.onDestroy()
    }

    override fun onBind(intent: Intent?): IBinder? = null

    companion object {
        private const val TAG = "MonitorService"
        const val CHANNEL_ID = "monitor"
        const val CHANNEL_FAULT = "faults"
        const val CHANNEL_ALARM = "alarms"
        const val NOTIF_ID = 1
        const val NOTIF_FAULT_ID = 2
        const val NOTIF_ALARM_ID = 3
        const val ACTION_USER_CANCEL = "org.cryomonitor.USER_CANCEL"
        const val ACTION_TEST_ALARM = "org.cryomonitor.TEST_ALARM"
        const val ACTION_ALERT_CANCELLED = "org.cryomonitor.ALERT_CANCELLED"
        const val ACTION_SET_DEBUG = "org.cryomonitor.SET_DEBUG"
        const val ACTION_HEARTBEAT_NOW = "org.cryomonitor.HEARTBEAT_NOW"
        const val ACTION_LATENCY_DRILL = "org.cryomonitor.LATENCY_DRILL"
        const val ACTION_WORKER_HEARTBEAT = "org.cryomonitor.WORKER_HEARTBEAT"
    }
}
