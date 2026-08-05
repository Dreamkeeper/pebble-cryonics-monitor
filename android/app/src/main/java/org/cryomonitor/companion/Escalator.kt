package org.cryomonitor.companion

/**
 * Phone-direct fallback escalation, used when the server is unreachable
 * (and as the SMS layer even when it isn't — SMS reaches contacts without
 * data connectivity).
 *
 * TODO(M1):
 *  - primary path: POST alarm to server (ServerClient), let it escalate
 *  - fallback: rate-limited SMS to contacts with cancellation window
 *    (OSD pattern), second SMS with maps link once GPS converges,
 *    sequential auto-calls, direct Telegram bot API
 *  - all sends logged locally; delivery reports tracked
 * TODO(M4): gateway role — this same class executes send-SMS/place-call
 *  commands received FROM the server when the app runs on a spare phone
 *  (sideload flavor only).
 */
class Escalator {
    fun fire(detector: String, lat: Double?, lon: Double?) { /* TODO(M1) */ }
    fun cancel(reason: String) { /* TODO(M1): retraction SMS/messages */ }
}
