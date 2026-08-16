package org.cryomonitor.companion

import android.app.AlertDialog
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.graphics.Color
import android.media.AudioManager
import android.media.ToneGenerator
import android.net.Uri
import android.os.Build
import android.os.Bundle
import androidx.appcompat.app.AppCompatActivity
import android.view.Gravity
import android.widget.Button
import android.widget.LinearLayout
import android.widget.TextView
import kotlin.concurrent.thread

/**
 * Full-screen alarm over the lock screen: siren for bystanders, big CANCEL,
 * one-tap dial of the local emergency number for the wearer.
 * Pre-alarm (watch countdown running) and full alarm differ only in wording;
 * cancelling either sends USER_OK to the watch and retracts escalation.
 */
class AlarmActivity : AppCompatActivity() {

    private var tone: ToneGenerator? = null
    @Volatile private var sirenOn = false

    private val cancelledReceiver = object : BroadcastReceiver() {
        override fun onReceive(ctx: Context?, intent: Intent?) = finish() // watch cancelled
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setShowWhenLocked(true)
        setTurnScreenOn(true)

        val detector = intent.getStringExtra("detector") ?: "alert"
        val preAlarm = intent.getBooleanExtra("preAlarm", false)

        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            gravity = Gravity.CENTER
            setBackgroundColor(if (preAlarm) Color.rgb(133, 84, 0)
                               else Ui.error(this))
            setPadding(Ui.dp(context, 24), Ui.dp(context, 24), Ui.dp(context, 24), Ui.dp(context, 24))
        }
        root.addView(TextView(this).apply {
            text = if (preAlarm) "PRE-ALARM: $detector" else "ALARM: $detector"
            textSize = 32f
            setTextColor(if (preAlarm) Color.WHITE else Ui.onError(this))
            gravity = Gravity.CENTER
        })
        root.addView(TextView(this).apply {
            text = if (preAlarm)
                "Watch countdown running.\nCancel here or on the watch if you are OK."
            else
                "Contacts are being alerted.\nCancel if this is a false alarm."
            textSize = 18f
            setTextColor(if (preAlarm) Color.WHITE else Ui.onError(this))
            gravity = Gravity.CENTER
            setPadding(0, Ui.dp(context, 16), 0, Ui.dp(context, 32))
        })
        root.addView(Button(this).apply {
            text = "I'M OK — CANCEL"
            textSize = 26f
            setOnClickListener { onCancelPressed() }
        }, LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT,
            Ui.dp(this, 88)).apply { bottomMargin = Ui.dp(this@AlarmActivity, 16) })
        root.addView(Button(this).apply {
            val settings = SettingsStore(this@AlarmActivity)
            text = "CALL ${settings.emergencyNumber}"
            setOnClickListener {
                startActivity(Intent(Intent.ACTION_DIAL,
                    Uri.parse("tel:${settings.emergencyNumber}")))
            }
        })
        Ui.applySystemInsets(root)
        setContentView(root)

        val filter = IntentFilter(MonitorService.ACTION_ALERT_CANCELLED)
        if (Build.VERSION.SDK_INT >= 33)
            registerReceiver(cancelledReceiver, filter, Context.RECEIVER_NOT_EXPORTED)
        else @Suppress("UnspecifiedRegisterReceiverFlag")
            registerReceiver(cancelledReceiver, filter)

        if (!preAlarm) startSiren()
    }

    private fun startSiren() {
        sirenOn = true
        tone = ToneGenerator(AudioManager.STREAM_ALARM, ToneGenerator.MAX_VOLUME)
        thread {
            while (sirenOn) {
                tone?.startTone(ToneGenerator.TONE_CDMA_EMERGENCY_RINGBACK, 800)
                Thread.sleep(1000)
            }
        }
    }

    private fun onCancelPressed() {
        stopSiren()
        // Post-event cause picker (Pixel "Share what happened" pattern) —
        // the cause feeds the learning layer via the retraction message.
        val causes = arrayOf("Loose strap", "Slept on my arm", "Took the watch off",
                             "Real event, but I'm fine now", "Other")
        AlertDialog.Builder(this)
            .setTitle("Cancelled. What happened?")
            .setItems(causes) { _, which -> sendCancel(causes[which]) }
            .setCancelable(false)
            .setNegativeButton("Skip") { _, _ -> sendCancel("skipped") }
            .show()
    }

    private fun sendCancel(cause: String) {
        startService(Intent(this, MonitorService::class.java)
            .setAction(MonitorService.ACTION_USER_CANCEL)
            .putExtra("cause", cause))
        finish()
    }

    private fun stopSiren() {
        sirenOn = false
        tone?.stopTone()
        tone?.release()
        tone = null
    }

    override fun onDestroy() {
        stopSiren()
        runCatching { unregisterReceiver(cancelledReceiver) }
        super.onDestroy()
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
