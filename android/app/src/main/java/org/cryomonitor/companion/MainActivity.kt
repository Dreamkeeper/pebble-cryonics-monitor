package org.cryomonitor.companion

import android.Manifest
import android.app.Activity
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.PowerManager
import android.provider.Settings
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.Switch
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
        requestPermissionsThenStartService()

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

        @Suppress("UseSwitchCompatOrMaterialCode")
        col.addView(Switch(this).apply {
            text = "Debug mode (extensive logs, phone + watch)"
            isChecked = settings.debugLogging
            setOnCheckedChangeListener { _, on ->
                settings.debugLogging = on
                startService(Intent(this@MainActivity, MonitorService::class.java)
                    .setAction(MonitorService.ACTION_SET_DEBUG)
                    .putExtra("enabled", on))
                Toast.makeText(this@MainActivity,
                    if (on) "Debug logging ON (watch too)" else "Debug logging off",
                    Toast.LENGTH_SHORT).show()
            }
        })

        col.addView(Button(this).apply {
            text = "View logs"
            setOnClickListener {
                startActivity(Intent(this@MainActivity, LogActivity::class.java))
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

        val root = ScrollView(this).apply { addView(col) }
        Ui.applySystemInsets(root)
        setContentView(root)
    }

    /**
     * BLUETOOTH_CONNECT must be granted before the monitoring service can
     * hold a connectedDevice foreground service on Android 14+; the rest
     * are needed for notifications, alarm location, and SMS fallback.
     * The service starts after the dialog either way — it has typed
     * fallbacks — but granting everything is the supported path.
     */
    private fun requestPermissionsThenStartService() {
        val wanted = mutableListOf<String>()
        if (Build.VERSION.SDK_INT >= 31) wanted += Manifest.permission.BLUETOOTH_CONNECT
        if (Build.VERSION.SDK_INT >= 33) wanted += Manifest.permission.POST_NOTIFICATIONS
        wanted += Manifest.permission.ACCESS_FINE_LOCATION
        wanted += Manifest.permission.SEND_SMS   // sideload flavor escalation
        val missing = wanted.filter {
            checkSelfPermission(it) != PackageManager.PERMISSION_GRANTED
        }
        if (missing.isEmpty()) {
            startMonitorService()
        } else {
            CmLog.i("MainActivity", "requesting permissions: $missing")
            requestPermissions(missing.toTypedArray(), REQ_PERMS)
        }
    }

    override fun onRequestPermissionsResult(
        requestCode: Int, permissions: Array<out String>, grantResults: IntArray) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        if (requestCode == REQ_PERMS) {
            val denied = permissions.filterIndexed { i, _ ->
                grantResults.getOrNull(i) != PackageManager.PERMISSION_GRANTED }
            if (denied.isNotEmpty()) {
                CmLog.w("MainActivity", "permissions denied: $denied")
                Toast.makeText(this,
                    "Denied: ${denied.joinToString()} — monitoring will run degraded",
                    Toast.LENGTH_LONG).show()
            }
            startMonitorService()
        }
    }

    private fun startMonitorService() {
        try {
            startForegroundService(Intent(this, MonitorService::class.java))
        } catch (e: Exception) {
            CmLog.e("MainActivity", "failed to start MonitorService", e)
            Toast.makeText(this, "Service start failed: $e", Toast.LENGTH_LONG).show()
        }
    }

    private companion object { const val REQ_PERMS = 41 }
}
