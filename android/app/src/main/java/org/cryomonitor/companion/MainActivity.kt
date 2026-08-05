package org.cryomonitor.companion

import android.app.Activity
import android.content.Intent
import android.os.Bundle

/**
 * TODO(M1/M3): status dashboard, contact management with opt-in
 * confirmation, per-detector settings pushed to the watch (PMSG_CONFIG),
 * suspension schedules, onboarding (battery-optimization exemption,
 * dontkillmyapp guidance, watchapp .pbw sideload-install).
 */
class MainActivity : Activity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        startForegroundService(Intent(this, MonitorService::class.java))
    }
}
