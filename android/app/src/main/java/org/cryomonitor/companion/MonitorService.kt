package org.cryomonitor.companion

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.Service
import android.content.Intent
import android.os.BatteryManager
import android.os.IBinder
import android.util.Log
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
    private lateinit var transport: PebbleTransport
    private lateinit var settings: SettingsStore
    private lateinit var server: ServerClient
    private lateinit var escalator: Escalator

    @Volatile private var lastWatchDataT = 0L
    @Volatile private var watchBattery: Int? = null
    @Volatile private var watchConnected = false
    @Volatile private var faultNotified = false
    @Volatile private var serverReachable = true
    @Volatile private var activeEscalationId: String? = null

    override fun onCreate() {
        super.onCreate()
        settings = SettingsStore(this)
        server = ServerClient(settings)
        escalator = Escalator(this, settings)
        startForeground(NOTIF_ID, buildNotification("Starting…"))

        transport = PebbleTransport(context = this, listener = this)
        transport.start()
        transport.startWatchapp() // also relaunches the background worker

        scope.launch { watchdogLoop() }
        scope.launch { serverHeartbeatLoop() }
    }

    // ---- PebbleTransport.Listener ----

    override fun onAppMessage(data: Map<Int, Any>) {
        lastWatchDataT = System.currentTimeMillis()
        faultNotified = false
        (data[PebbleTransport.KEY_WATCH_BATTERY] as? Int)?.let { watchBattery = it }

        when (data[PebbleTransport.KEY_MSG_TYPE] as? Int) {
            Protocol.PMSG_HEARTBEAT -> updateNotification()
            Protocol.PMSG_PRE_ALARM -> {
                val det = detectorName(data)
                AlarmActivity.launch(this, det, preAlarm = true)
            }
            Protocol.PMSG_ALARM -> {
                val det = detectorName(data)
                AlarmActivity.launch(this, det, preAlarm = false)
                scope.launch { escalate(det) }
            }
            Protocol.PMSG_CANCEL -> {
                sendBroadcast(Intent(ACTION_ALERT_CANCELLED).setPackage(packageName))
                scope.launch { retract("cancelled_on_watch") }
            }
            Protocol.PMSG_SUSPENDED -> updateNotification()
        }
    }

    override fun onConnectionChanged(connected: Boolean) {
        watchConnected = connected
        if (connected) transport.startWatchapp()
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
        activeEscalationId = escId
        serverReachable = escId != null
        // SMS fires regardless (redundant path); Telegram-direct only when
        // the server (which owns Telegram with ACK buttons) is unreachable.
        escalator.fire(detector, isTest)
        updateNotification()
    }

    private fun retract(reason: String) {
        activeEscalationId?.let { server.resolve(it, "false_alarm") }
        activeEscalationId = null
        escalator.cancel(reason)
    }

    // ---- loops ----

    private suspend fun watchdogLoop() {
        while (true) {
            val silentFor = (System.currentTimeMillis() - lastWatchDataT) / 1000
            if (lastWatchDataT > 0 && silentFor > Protocol.WATCH_SILENT_AFTER_S &&
                !faultNotified) {
                faultNotified = true
                notifyFault(
                    if (watchConnected)
                        "Watch connected but silent ${silentFor}s — worker evicted? " +
                        "Open the watchapp to restart monitoring."
                    else
                        "Watch link lost ${silentFor}s — check Bluetooth/battery.")
                transport.startWatchapp() // best-effort self-heal
            }
            updateNotification()
            delay(15_000) // also re-posts the notification (OSD pattern)
        }
    }

    private suspend fun serverHeartbeatLoop() {
        while (true) {
            if (server.configured) {
                val bm = getSystemService(BatteryManager::class.java)
                val pct = bm.getIntProperty(BatteryManager.BATTERY_PROPERTY_CAPACITY)
                val age = if (lastWatchDataT == 0L) null
                          else ((System.currentTimeMillis() - lastWatchDataT) / 1000).toInt()
                val ok = server.heartbeat(pct, watchBattery, age,
                                          lowBatteryWarning = pct in 1..15)
                if (!ok && serverReachable) {
                    serverReachable = false
                    notifyFault("Server unreachable — phone-direct escalation active.")
                } else if (ok && !serverReachable) {
                    serverReachable = true
                    Log.i(TAG, "server back")
                }
            }
            delay(300_000)
        }
    }

    // ---- commands from activities ----

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            ACTION_USER_CANCEL -> {
                transport.send(mapOf(
                    PebbleTransport.KEY_MSG_TYPE to Protocol.PMSG_USER_OK_REMOTE))
                scope.launch { retract(intent.getStringExtra("cause") ?: "cancelled_on_phone") }
            }
            ACTION_TEST_ALARM -> scope.launch { escalate("test", isTest = true) }
        }
        return START_STICKY
    }

    // ---- notifications ----

    private fun buildNotification(text: String): Notification {
        val nm = getSystemService(NotificationManager::class.java)
        nm.createNotificationChannel(NotificationChannel(
            CHANNEL_ID, "Monitoring", NotificationManager.IMPORTANCE_LOW))
        nm.createNotificationChannel(NotificationChannel(
            CHANNEL_FAULT, "System faults", NotificationManager.IMPORTANCE_HIGH))
        return Notification.Builder(this, CHANNEL_ID)
            .setContentTitle("Cryonics Monitor")
            .setContentText(text)
            .setSmallIcon(android.R.drawable.stat_sys_warning)
            .setOngoing(true)
            .build()
    }

    private fun notifyFault(text: String) {
        val nm = getSystemService(NotificationManager::class.java)
        nm.notify(NOTIF_FAULT_ID, Notification.Builder(this, CHANNEL_FAULT)
            .setContentTitle("Cryonics Monitor FAULT")
            .setContentText(text)
            .setStyle(Notification.BigTextStyle().bigText(text))
            .setSmallIcon(android.R.drawable.stat_notify_error)
            .build())
    }

    private fun updateNotification() {
        val nm = getSystemService(NotificationManager::class.java)
        nm.notify(NOTIF_ID, buildNotification(statusLine()))
    }

    private fun statusLine(): String {
        val age = if (lastWatchDataT == 0L) "never"
                  else "${(System.currentTimeMillis() - lastWatchDataT) / 1000}s"
        val srv = when {
            !server.configured -> "no server"
            serverReachable -> "server ok"
            else -> "SERVER DOWN"
        }
        val wb = watchBattery?.let { " ${it}%" } ?: ""
        return "watch $age$wb · $srv"
    }

    override fun onDestroy() {
        transport.stop()
        scope.cancel()
        super.onDestroy()
    }

    override fun onBind(intent: Intent?): IBinder? = null

    companion object {
        private const val TAG = "MonitorService"
        const val CHANNEL_ID = "monitor"
        const val CHANNEL_FAULT = "faults"
        const val NOTIF_ID = 1
        const val NOTIF_FAULT_ID = 2
        const val ACTION_USER_CANCEL = "org.cryomonitor.USER_CANCEL"
        const val ACTION_TEST_ALARM = "org.cryomonitor.TEST_ALARM"
        const val ACTION_ALERT_CANCELLED = "org.cryomonitor.ALERT_CANCELLED"
    }
}
