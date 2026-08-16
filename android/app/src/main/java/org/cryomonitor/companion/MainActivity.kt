package org.cryomonitor.companion

import android.Manifest
import android.app.Activity
import android.content.Intent
import android.content.pm.PackageManager
import android.graphics.Color
import android.graphics.Typeface
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
import kotlin.concurrent.thread

class MainActivity : Activity() {

    private lateinit var settings: SettingsStore
    private lateinit var degradedBanner: TextView
    private lateinit var connState: TextView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        settings = SettingsStore(this)
        CmLog.init(this)
        requestPermissionsThenStartService()

        val col = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(32, 32, 32, 32)
        }

        // The safety-net fault banner: as loud as a FAULT, because an alarm
        // that reaches nobody is one (spec requirement).
        degradedBanner = TextView(this).apply {
            text = "⚠ ALARMS REACH NOBODY — no emergency contacts configured.\n" +
                "Tap 'Contacts & safety net' and add at least one."
            setBackgroundColor(Color.rgb(140, 0, 0))
            setTextColor(Color.WHITE)
            textSize = 16f
            setPadding(24, 24, 24, 24)
            visibility = android.view.View.GONE
            setOnClickListener {
                startActivity(Intent(this@MainActivity, ContactsActivity::class.java))
            }
        }
        col.addView(degradedBanner)

        connState = TextView(this).apply { setPadding(0, 8, 0, 16) }
        col.addView(connState)

        fun header(t: String) = col.addView(TextView(this).apply {
            text = t; textSize = 18f; typeface = Typeface.DEFAULT_BOLD
            setPadding(0, 24, 0, 8)
        })

        header("Setup")
        col.addView(Button(this).apply {
            text = "Enroll with a code (recommended)"
            setOnClickListener {
                startActivity(Intent(this@MainActivity, EnrollActivity::class.java))
            }
        })
        col.addView(Button(this).apply {
            text = "Contacts & safety net"
            setOnClickListener {
                startActivity(Intent(this@MainActivity, ContactsActivity::class.java))
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

        header("Diagnostics")
        @Suppress("UseSwitchCompatOrMaterialCode")
        col.addView(Switch(this).apply {
            text = "Debug mode (extensive logs, phone + watch)"
            isChecked = settings.debugLogging
            setOnCheckedChangeListener { _, on ->
                settings.debugLogging = on
                startService(Intent(this@MainActivity, MonitorService::class.java)
                    .setAction(MonitorService.ACTION_SET_DEBUG)
                    .putExtra("enabled", on))
            }
        })
        col.addView(Button(this).apply {
            text = "View logs"
            setOnClickListener {
                startActivity(Intent(this@MainActivity, LogActivity::class.java))
            }
        })

        // Manual configuration is the fallback path, not the front door.
        header("Advanced")
        val advanced = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            visibility = android.view.View.GONE
        }
        col.addView(Button(this).apply {
            text = "Show manual configuration"
            setOnClickListener {
                advanced.visibility = if (advanced.visibility ==
                    android.view.View.GONE) android.view.View.VISIBLE
                else android.view.View.GONE
            }
        })
        col.addView(advanced)

        fun advLabel(t: String) = advanced.addView(TextView(this).apply { text = t })
        fun advField(hint: String, value: String): EditText {
            val e = EditText(this).apply { setHint(hint); setText(value) }
            advanced.addView(e)
            return e
        }

        advLabel("Server URL")
        val fUrl = advField("https://cm.example.com", settings.serverUrl)
        advLabel("API token (enrollment sets this automatically)")
        val fTok = advField("token", settings.apiToken)
        advLabel("SMS contacts (comma-separated numbers, phone-direct fallback)")
        val fSms = advField("+491701234567",
                            settings.smsContacts.joinToString(","))
        advLabel("Telegram bot token (phone-direct fallback channel)")
        val fTg = advField("12345:ABC…", settings.telegramBotToken)
        advLabel("Telegram chat ids (phone-direct fallback)")
        val fTgIds = advField("1122334455", settings.telegramChatIds.joinToString(","))
        advLabel("Emergency number for one-tap dial")
        val fEmg = advField("112 / 911", settings.emergencyNumber)
        advLabel("Wearer name (used in alert texts)")
        val fName = advField("name", settings.wearerName)

        advanced.addView(Button(this).apply {
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
                startService(Intent(this@MainActivity, MonitorService::class.java)
                    .setAction(MonitorService.ACTION_HEARTBEAT_NOW))
                Toast.makeText(this@MainActivity,
                    "Saved — check the notification for server status",
                    Toast.LENGTH_LONG).show()
            }
        })

        val root = ScrollView(this).apply { addView(col) }
        Ui.applySystemInsets(root)
        setContentView(root)
    }

    override fun onResume() {
        super.onResume()
        refreshStatus()
    }

    private fun refreshStatus() {
        if (settings.serverUrl.isEmpty()) {
            connState.text = "Not connected to a server yet — enroll below."
            return
        }
        thread {
            val client = ServerClient(settings)
            val s = client.fetchStatus()
            runOnUiThread {
                if (s == null) {
                    connState.text = "Server: ${client.lastResult}"
                    return@runOnUiThread
                }
                degradedBanner.visibility =
                    if (s.degraded) android.view.View.VISIBLE
                    else android.view.View.GONE
                connState.text = "Server connected · phone state '${s.phone}'" +
                    if (s.activeEscalations > 0)
                        " · ${s.activeEscalations} ACTIVE ESCALATION(S)" else ""
            }
        }
    }

    private fun requestPermissionsThenStartService() {
        val wanted = mutableListOf<String>()
        if (Build.VERSION.SDK_INT >= 31) wanted += Manifest.permission.BLUETOOTH_CONNECT
        if (Build.VERSION.SDK_INT >= 33) wanted += Manifest.permission.POST_NOTIFICATIONS
        wanted += Manifest.permission.ACCESS_FINE_LOCATION
        wanted += Manifest.permission.SEND_SMS
        val missing = wanted.filter {
            checkSelfPermission(it) != PackageManager.PERMISSION_GRANTED
        }
        if (missing.isEmpty()) startMonitorService()
        else requestPermissions(missing.toTypedArray(), REQ_PERMS)
    }

    override fun onRequestPermissionsResult(
        requestCode: Int, permissions: Array<out String>, grantResults: IntArray) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        if (requestCode == REQ_PERMS) startMonitorService()
    }

    private fun startMonitorService() {
        try {
            startForegroundService(Intent(this, MonitorService::class.java))
        } catch (e: Exception) {
            CmLog.e("MainActivity", "failed to start MonitorService", e)
        }
    }

    private companion object { const val REQ_PERMS = 41 }
}
