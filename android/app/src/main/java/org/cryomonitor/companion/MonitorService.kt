package org.cryomonitor.companion

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.Service
import android.content.Intent
import android.os.IBinder
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

/**
 * The always-on hub (OpenSeizureDetector SdServer pattern, ported to Kotlin):
 *
 *  - foreground service + persistent notification (re-posted periodically in
 *    case the user dismisses it)
 *  - receives watch AppMessages via PebbleKitAndroid2 (TODO M1: transport)
 *  - watch watchdog: FAULT if heartbeats stop, worker evicted, battery low
 *  - forwards alarms to the server; falls back to phone-direct SMS/call/
 *    Telegram when the server is unreachable
 *  - sends its own heartbeat to the server every HEARTBEAT_INTERVAL_S
 */
class MonitorService : Service() {

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)
    private var lastWatchDataT = 0L

    override fun onCreate() {
        super.onCreate()
        startForeground(NOTIF_ID, buildNotification("Monitoring"))
        scope.launch { watchdogLoop() }
        scope.launch { serverHeartbeatLoop() }
        // TODO(M1): PebbleKitAndroid2 listener service wiring:
        //  - PMSG_HEARTBEAT -> lastWatchDataT = now; forward battery/bpm
        //  - PMSG_PRE_ALARM -> AlarmActivity.launch(this, preAlarm = true)
        //  - PMSG_ALARM     -> AlarmActivity.launch(this); Escalator.fire(...)
        //  - PMSG_CANCEL    -> Escalator.cancel(reason)
        //  - DataLogging heartbeat records (tag 0xC201) as backup liveness
    }

    private suspend fun watchdogLoop() {
        while (true) {
            val silentFor = (System.currentTimeMillis() - lastWatchDataT) / 1000
            if (lastWatchDataT > 0 && silentFor > Protocol.WATCH_SILENT_AFTER_S) {
                // TODO(M1): FAULT alert to wearer (notification + sound);
                // optionally escalate to Tier 1 after prolonged silence.
                // Causes: BT drop, worker evicted by another app, watch died.
            }
            updateNotification()
            delay(15_000) // also re-posts the notification (OSD pattern)
        }
    }

    private suspend fun serverHeartbeatLoop() {
        while (true) {
            // TODO(M2): ServerClient.heartbeat(batteryPct, watchDataAge, ...)
            // On repeated failure: mark server unreachable, phone-direct
            // fallback becomes primary, notify wearer once.
            delay(300_000)
        }
    }

    private fun buildNotification(text: String): Notification {
        val nm = getSystemService(NotificationManager::class.java)
        nm.createNotificationChannel(
            NotificationChannel(CHANNEL_ID, "Monitoring",
                NotificationManager.IMPORTANCE_LOW))
        return Notification.Builder(this, CHANNEL_ID)
            .setContentTitle("Cryonics Monitor")
            .setContentText(text)
            .setSmallIcon(android.R.drawable.stat_sys_warning)
            .setOngoing(true)
            .build()
    }

    private fun updateNotification() {
        val nm = getSystemService(NotificationManager::class.java)
        nm.notify(NOTIF_ID, buildNotification(statusLine()))
    }

    private fun statusLine(): String {
        val age = if (lastWatchDataT == 0L) "never"
        else "${(System.currentTimeMillis() - lastWatchDataT) / 1000}s ago"
        return "Watch data: $age"
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int) = START_STICKY

    override fun onDestroy() {
        scope.cancel()
        super.onDestroy()
    }

    override fun onBind(intent: Intent?): IBinder? = null

    companion object {
        const val CHANNEL_ID = "monitor"
        const val NOTIF_ID = 1
    }
}
