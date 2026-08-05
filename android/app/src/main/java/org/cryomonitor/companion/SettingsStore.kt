package org.cryomonitor.companion

import android.content.Context

/** SharedPreferences-backed settings. TODO(M3): real settings UI + validation. */
class SettingsStore(context: Context) {
    private val p = context.getSharedPreferences("cm_settings", Context.MODE_PRIVATE)

    var serverUrl: String
        get() = p.getString("server_url", "") ?: ""
        set(v) = p.edit().putString("server_url", v.trimEnd('/')).apply()

    var apiToken: String
        get() = p.getString("api_token", "") ?: ""
        set(v) = p.edit().putString("api_token", v).apply()

    /** Comma-separated phone numbers for SMS fallback escalation. */
    var smsContacts: List<String>
        get() = (p.getString("sms_contacts", "") ?: "")
            .split(',').map { it.trim() }.filter { it.isNotEmpty() }
        set(v) = p.edit().putString("sms_contacts", v.joinToString(",")).apply()

    var telegramBotToken: String
        get() = p.getString("tg_token", "") ?: ""
        set(v) = p.edit().putString("tg_token", v).apply()

    /** Comma-separated Telegram chat ids for direct (server-less) alerts. */
    var telegramChatIds: List<String>
        get() = (p.getString("tg_chats", "") ?: "")
            .split(',').map { it.trim() }.filter { it.isNotEmpty() }
        set(v) = p.edit().putString("tg_chats", v.joinToString(",")).apply()

    /** Local emergency number offered to the WEARER for one-tap dialing. */
    var emergencyNumber: String
        get() = p.getString("emergency_number", "112") ?: "112"
        set(v) = p.edit().putString("emergency_number", v).apply()

    var wearerName: String
        get() = p.getString("wearer_name", "the wearer") ?: "the wearer"
        set(v) = p.edit().putString("wearer_name", v).apply()

    /** Extensive debug logging (phone ring/file log + watch APP_LOG). */
    var debugLogging: Boolean
        get() = p.getBoolean("debug_logging", false)
        set(v) = p.edit().putBoolean("debug_logging", v).apply()
}
