package org.cryomonitor.companion

import android.app.Activity
import android.content.Context
import android.content.Intent
import android.os.Bundle

/**
 * Full-screen alarm (shows over lock screen, turns screen on):
 * big CANCEL (retracts everything, sends false-alarm to watch + server),
 * one-tap "Call 911" dialer for the wearer, loud siren for bystanders.
 *
 * TODO(M1): layout, ToneGenerator siren with escalating volume, cancel
 * window countdown mirroring the watch stage, post-event cause picker
 * ("loose strap / slept on arm / took watch off / real event / other").
 */
class AlarmActivity : Activity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setShowWhenLocked(true)
        setTurnScreenOn(true)
        // TODO(M1): setContentView(R.layout.activity_alarm)
    }

    companion object {
        fun launch(ctx: Context, detector: String, preAlarm: Boolean) {
            ctx.startActivity(Intent(ctx, AlarmActivity::class.java).apply {
                addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                putExtra("detector", detector)
                putExtra("preAlarm", preAlarm)
            })
        }
    }
}
