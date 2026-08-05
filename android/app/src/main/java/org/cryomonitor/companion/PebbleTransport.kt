package org.cryomonitor.companion

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.os.Build
import org.json.JSONArray
import org.json.JSONObject
import java.util.UUID

/**
 * Watch link via the classic PebbleKit broadcast-intent protocol, spoken by
 * the official Pebble/Core mobile app (and Gadgetbridge). Implemented
 * directly to avoid an external dependency; migrating to PebbleKitAndroid2
 * is TODO(M2) once its Maven coordinates stabilize.
 */
class PebbleTransport(
    private val context: Context,
    private val uuid: UUID = UUID.fromString(Protocol.WATCHAPP_UUID),
    private val listener: Listener,
) {
    interface Listener {
        fun onAppMessage(data: Map<Int, Any>)
        fun onConnectionChanged(connected: Boolean)
    }

    private var txId = 0

    private val receiver = object : BroadcastReceiver() {
        override fun onReceive(ctx: Context, intent: Intent) {
            when (intent.action) {
                INTENT_APP_RECEIVE -> {
                    val msgUuid = intent.getSerializableExtra("uuid") as? UUID
                    if (msgUuid != uuid) return
                    val tid = intent.getIntExtra("transaction_id", -1)
                    ack(tid)
                    val json = intent.getStringExtra("msg_data") ?: return
                    CmLog.d(TAG, "rx tid=$tid $json")
                    listener.onAppMessage(parseDict(json))
                }
                INTENT_PEBBLE_CONNECTED -> {
                    CmLog.i(TAG, "pebble connected")
                    listener.onConnectionChanged(true)
                }
                INTENT_PEBBLE_DISCONNECTED -> {
                    CmLog.i(TAG, "pebble disconnected")
                    listener.onConnectionChanged(false)
                }
            }
        }
    }

    fun start() {
        val filter = IntentFilter().apply {
            addAction(INTENT_APP_RECEIVE)
            addAction(INTENT_PEBBLE_CONNECTED)
            addAction(INTENT_PEBBLE_DISCONNECTED)
        }
        if (Build.VERSION.SDK_INT >= 33) {
            context.registerReceiver(receiver, filter, Context.RECEIVER_EXPORTED)
        } else {
            @Suppress("UnspecifiedRegisterReceiverFlag")
            context.registerReceiver(receiver, filter)
        }
    }

    fun stop() = context.unregisterReceiver(receiver)

    /** Ask the Pebble app to launch our watchapp (also relaunches the worker). */
    fun startWatchapp() {
        context.sendBroadcast(Intent(INTENT_APP_START).putExtra("uuid", uuid))
    }

    /** Send a dictionary of (messageKey -> Int|String|ByteArray) to the watch. */
    fun send(data: Map<Int, Any>) {
        val arr = JSONArray()
        for ((key, value) in data) {
            val o = JSONObject().put("key", key)
            when (value) {
                is Int -> o.put("type", "int").put("length", 4).put("value", value)
                is String -> o.put("type", "string").put("length", value.length)
                    .put("value", value)
                is ByteArray -> o.put("type", "bytes").put("length", value.size)
                    .put("value", android.util.Base64.encodeToString(
                        value, android.util.Base64.NO_WRAP))
                else -> error("unsupported type for key $key")
            }
            arr.put(o)
        }
        txId = (txId + 1) and 0xff
        CmLog.d(TAG, "tx tid=$txId $arr")
        context.sendBroadcast(Intent(INTENT_APP_SEND).apply {
            putExtra("uuid", uuid)
            putExtra("transaction_id", txId)
            putExtra("msg_data", arr.toString())
        })
    }

    private fun ack(tid: Int) {
        context.sendBroadcast(Intent(INTENT_APP_RECEIVE_ACK).apply {
            putExtra("transaction_id", tid)
        })
    }

    private fun parseDict(json: String): Map<Int, Any> {
        val out = mutableMapOf<Int, Any>()
        val arr = JSONArray(json)
        for (i in 0 until arr.length()) {
            val o = arr.getJSONObject(i)
            val key = o.getInt("key")
            out[key] = when (o.getString("type")) {
                "string" -> o.getString("value")
                "bytes" -> android.util.Base64.decode(
                    o.getString("value"), android.util.Base64.NO_WRAP)
                else -> o.getLong("value").toInt() // int/uint of any width
            }
        }
        return out
    }

    companion object {
        private const val TAG = "PebbleTransport"
        const val INTENT_APP_SEND = "com.getpebble.action.app.SEND"
        const val INTENT_APP_RECEIVE = "com.getpebble.action.app.RECEIVE"
        const val INTENT_APP_RECEIVE_ACK = "com.getpebble.action.app.RECEIVE_ACK"
        const val INTENT_APP_START = "com.getpebble.action.app.START"
        const val INTENT_PEBBLE_CONNECTED = "com.getpebble.action.PEBBLE_CONNECTED"
        const val INTENT_PEBBLE_DISCONNECTED = "com.getpebble.action.PEBBLE_DISCONNECTED"

        // Message keys — MUST match watchapp/package.json "messageKeys".
        const val KEY_MSG_TYPE = 1
        const val KEY_DETECTOR = 2
        const val KEY_STAGE = 3
        const val KEY_SECONDS = 4
        const val KEY_CANCEL_REASON = 5
        const val KEY_HEARTBEAT_SEQ = 6
        const val KEY_WATCH_BATTERY = 7
        const val KEY_HR_BPM = 8
        const val KEY_SUSPEND_REMAINING_S = 9
        const val KEY_CFG_BLOB = 10
    }
}
