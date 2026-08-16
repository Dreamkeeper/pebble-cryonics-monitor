package org.cryomonitor.companion

import android.app.Activity
import android.content.Intent
import android.graphics.Typeface
import android.os.Bundle
import android.text.InputType
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import kotlin.concurrent.thread

/**
 * Primary onboarding: server URL + one-time enrollment code -> per-wearer
 * token. Failure modes are deliberately distinct (spec): unreachable
 * server, rejected (invalid/expired/used) code, malformed code, rate
 * limit. The token is stored and never displayed.
 */
class EnrollActivity : Activity() {

    private lateinit var settings: SettingsStore
    private lateinit var status: TextView
    private lateinit var urlField: EditText
    private lateinit var codeField: EditText
    private lateinit var enrollBtn: Button

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        settings = SettingsStore(this)

        val col = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(32, 32, 32, 32)
        }
        col.addView(TextView(this).apply {
            text = "Enroll this phone"
            textSize = 22f
            typeface = Typeface.DEFAULT_BOLD
        })
        col.addView(TextView(this).apply {
            text = "Ask your operator (or use the admin API) for a one-time " +
                "enrollment code, then enter it with the server address."
            setPadding(0, 8, 0, 24)
        })

        col.addView(TextView(this).apply { text = "Server URL" })
        urlField = EditText(this).apply {
            hint = "https://cm.example.com"
            setText(settings.serverUrl)
            inputType = InputType.TYPE_TEXT_VARIATION_URI
        }
        col.addView(urlField)

        col.addView(TextView(this).apply { text = "Enrollment code" })
        codeField = EditText(this).apply {
            hint = "XXXX-XXXX"
            inputType = InputType.TYPE_CLASS_TEXT or
                InputType.TYPE_TEXT_FLAG_CAP_CHARACTERS
        }
        col.addView(codeField)

        enrollBtn = Button(this).apply {
            text = "Enroll"
            setOnClickListener { doEnroll() }
        }
        col.addView(enrollBtn)

        status = TextView(this).apply { setPadding(0, 16, 0, 0) }
        col.addView(status)

        val root = ScrollView(this).apply { addView(col) }
        Ui.applySystemInsets(root)
        setContentView(root)
    }

    private fun doEnroll() {
        val url = urlField.text.toString().trim().trimEnd('/')
        val code = codeField.text.toString().trim()
        if (!url.startsWith("http")) {
            urlField.error = "enter the full https:// address"
            return
        }
        if (code.replace("-", "").length != 8) {
            codeField.error = "codes look like XXXX-XXXX"
            return
        }
        enrollBtn.isEnabled = false
        status.text = "Contacting server…"

        thread {
            val result = ServerClient(settings).enroll(url, code)
            runOnUiThread {
                enrollBtn.isEnabled = true
                when (result) {
                    is ServerClient.EnrollResult.Success -> {
                        settings.serverUrl = url
                        settings.apiToken = result.token
                        status.text = "Enrolled ✓ — this phone now monitors " +
                            "wearer '${result.wearerId}'."
                        startService(Intent(this, MonitorService::class.java)
                            .setAction(MonitorService.ACTION_HEARTBEAT_NOW))
                        setResult(RESULT_OK)
                        finish()
                    }
                    is ServerClient.EnrollResult.CodeRejected -> {
                        status.text = "This code was already used, expired, " +
                            "or is not valid. Ask for a new one — codes work " +
                            "exactly once."
                    }
                    is ServerClient.EnrollResult.Malformed ->
                        codeField.error = "not a valid code"
                    is ServerClient.EnrollResult.RateLimited ->
                        status.text = "Too many attempts from this network — " +
                            "wait a while and try again."
                    is ServerClient.EnrollResult.Unreachable ->
                        status.text = "Cannot reach $url (${result.why}). " +
                            "Check the address and your connection " +
                            "(VPN/Tailscale if the server is private)."
                    is ServerClient.EnrollResult.ServerError ->
                        status.text = "Server error HTTP ${result.code} — " +
                            "check the server logs."
                }
            }
        }
    }
}
