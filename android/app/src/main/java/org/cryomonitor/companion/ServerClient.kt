package org.cryomonitor.companion

import okhttp3.MediaType.Companion.toMediaType
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.RequestBody.Companion.toRequestBody
import org.json.JSONObject
import java.util.concurrent.TimeUnit

/**
 * HTTPS client for the self-hosted server (server/ in this repo).
 * All calls are blocking — invoke from a coroutine on Dispatchers.IO.
 * Every method returns false on any failure; the caller decides whether
 * phone-direct fallback escalation takes over.
 */
class ServerClient(private val settings: SettingsStore) {

    private val http = OkHttpClient.Builder()
        .connectTimeout(10, TimeUnit.SECONDS)
        .callTimeout(20, TimeUnit.SECONDS)
        .build()

    val configured: Boolean get() = settings.serverUrl.isNotEmpty()

    fun heartbeat(phoneBatteryPct: Int, watchBatteryPct: Int?, watchDataAgeS: Int?,
                  lowBatteryWarning: Boolean): Boolean =
        post("/api/v1/heartbeat", JSONObject().apply {
            put("phone_battery_pct", phoneBatteryPct)
            watchBatteryPct?.let { put("watch_battery_pct", it) }
            watchDataAgeS?.let { put("watch_data_age_s", it) }
            put("low_battery_warning", lowBatteryWarning)
        })

    /** Returns the escalation id, or null if the server is unreachable. */
    fun alarm(detector: String, kind: String, lat: Double?, lon: Double?): String? {
        val body = JSONObject().apply {
            put("detector", detector)
            put("kind", kind)
            lat?.let { put("lat", it) }
            lon?.let { put("lon", it) }
        }
        val resp = postForBody("/api/v1/alarm", body) ?: return null
        return JSONObject(resp).optString("escalation_id").ifEmpty { null }
    }

    fun resolve(escalationId: String, resolution: String): Boolean {
        val url = "${settings.serverUrl}/api/v1/alarm/$escalationId/resolve" +
            "?resolution=$resolution&token=${settings.apiToken}"
        return runCatching {
            http.newCall(Request.Builder().url(url)
                .post(ByteArray(0).toRequestBody(null)).build())
                .execute().use { it.isSuccessful }
        }.getOrDefault(false)
    }

    fun offlineWindow(durationS: Int): Boolean =
        post("/api/v1/offline-window",
             JSONObject().put("duration_s", durationS))

    private fun post(path: String, body: JSONObject): Boolean =
        postForBody(path, body) != null

    private fun postForBody(path: String, body: JSONObject): String? {
        if (!configured) return null
        body.put("token", settings.apiToken)
        return runCatching {
            http.newCall(
                Request.Builder()
                    .url(settings.serverUrl + path)
                    .post(body.toString()
                        .toRequestBody("application/json".toMediaType()))
                    .build())
                .execute().use { r -> if (r.isSuccessful) r.body?.string() else null }
        }.getOrNull()
    }
}
