package org.cryomonitor.companion

import android.app.Activity
import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.os.PowerManager
import android.provider.Settings
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import android.widget.Toast

/**
 * Minimal v0.1 settings screen (programmatic UI).
 * TODO(M3): proper onboarding, contact opt-in confirmation flow,
 * per-detector config pushed to the watch, suspension schedules.
 */
class MainActivity : Activity() {

    private lateinit var settings: SettingsStore

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        settings = SettingsStore(this)
        startForegroundService(Intent(this, MonitorService::class.java))

        val col = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(32, 32, 32, 32)
        }

        fun label(t: String) = col.addView(TextView(this).apply { text = t })
        fun field(hint: String, value: String): EditText {
            val e = EditText(this).apply { setHint(hint); setText(value) }
            col.addView(e)
            return e
        }

        label("Server URL (empty = phone-direct only)")
        val fUrl = field("https://nas.example.com:8080", settings.serverUrl)
        label("API token")
        val fTok = field("token", settings.apiToken)
        label("SMS contacts (comma-separated numbers)")
        val fSms = field("+491701234567,+15551234567",
                         settings.smsContacts.joinToString(","))
        label("Telegram bot token (fallback channel)")
        val fTg = field("12345:ABC…", settings.telegramBotToken)
        label("Telegram chat ids (comma-separated)")
        val fTgIds = field("1122334455", settings.telegramChatIds.joinToString(","))
        label("Emergency number for one-tap dial")
        val fEmg = field("112 / 911", settings.emergencyNumber)
        label("Wearer name (used in alert texts)")
        val fName = field("name", settings.wearerName)

        col.addView(Button(this).apply {
            text = "Save"
            setOnClickListener {
                settings.serverUrl = fUrl.text.toString().trim()
                settings.apiToken = fTok.text.toString().trim()
                settings.smsContacts = fSms.text.toString().split(',')
                    .map { it.trim() }.filter { it.isNotEmpty() }
                settings.telegramBotToken = fTg.text.toString().trim()
                settings.telegramChatIds = fTgIds.text.toString().split(',')
                    .map { it.trim() }.filter { it.isNotEmpty() }
                settings.emergencyNumber = fEmg.text.toString().trim()
                    .ifEmpty { "112" }
                settings.wearerName = fName.text.toString().trim()
                Toast.makeText(this@MainActivity, "Saved", Toast.LENGTH_SHORT).show()
            }
        })

        col.addView(Button(this).apply {
            text = "Allow running in background (battery exemption)"
            setOnClickListener {
                val pm = getSystemService(PowerManager::class.java)
                if (!pm.isIgnoringBatteryOptimizations(packageName)) {
                    startActivity(Intent(
                        Settings.ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS,
                        Uri.parse("package:$packageName")))
                } else Toast.makeText(this@MainActivity,
                    "Already exempt", Toast.LENGTH_SHORT).show()
            }
        })

        col.addView(Button(this).apply {
            text = "Fire-drill: TEST alarm through the full chain"
            setOnClickListener {
                startService(Intent(this@MainActivity, MonitorService::class.java)
                    .setAction(MonitorService.ACTION_TEST_ALARM))
                Toast.makeText(this@MainActivity,
                    "TEST alarm sent (messages are tagged TEST)",
                    Toast.LENGTH_LONG).show()
            }
        })

        setContentView(ScrollView(this).apply { addView(col) })
    }
}
