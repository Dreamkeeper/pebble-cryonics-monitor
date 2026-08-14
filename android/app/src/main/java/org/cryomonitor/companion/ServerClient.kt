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

    /**
     * Human-readable outcome of the last call, for the notification and the
     * log. "Cannot reach the server" and "the server rejected my token" are
     * different problems with different fixes, so they must never look alike.
     */
    @Volatile
    var lastResult: String = "not tried yet"
        private set

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
        // Token goes in the Authorization header, never the query string:
        // a public reverse proxy writes full URLs to its access log.
        val url = "${settings.serverUrl}/api/v1/alarm/$escalationId/resolve" +
            "?resolution=$resolution"
        return runCatching {
            http.newCall(Request.Builder().url(url)
                .header("Authorization", "Bearer ${settings.apiToken}")
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
        if (!configured) {
            lastResult = "no server URL set"
            return null
        }
        val url = settings.serverUrl + path
        val result = runCatching {
            http.newCall(
                Request.Builder()
                    .url(url)
                    .header("Authorization", "Bearer ${settings.apiToken}")
                    .post(body.toString()
                        .toRequestBody("application/json".toMediaType()))
                    .build())
                .execute().use { r ->
                    lastResult = when {
                        r.isSuccessful -> "ok"
                        r.code == 401 -> "token rejected (401)"
                        else -> "HTTP ${r.code}"
                    }
                    // INFO, not DEBUG: this is the line you need when the
                    // connection misbehaves, without turning debug mode on.
                    CmLog.i(TAG, "POST $url -> ${r.code}")
                    if (r.isSuccessful) r.body?.string() else null
                }
        }.onFailure {
            lastResult = "unreachable: ${it.javaClass.simpleName}"
            CmLog.i(TAG, "POST $url failed: $it")
        }
        return result.getOrNull()
    }

    private companion object { const val TAG = "ServerClient" }
}
