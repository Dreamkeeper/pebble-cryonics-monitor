package org.cryomonitor.companion

import android.content.Intent
import io.rebble.pebblekit2.client.BasePebbleListenerService
import io.rebble.pebblekit2.common.model.PebbleDictionary
import io.rebble.pebblekit2.common.model.PebbleDictionaryItem
import io.rebble.pebblekit2.common.model.ReceiveResult
import io.rebble.pebblekit2.common.model.WatchIdentifier
import java.util.UUID

/**
 * PebbleKit2 inbound path: the Core mobile app binds this service (declared
 * with the RECEIVE_DATA_FROM_WATCH intent filter) whenever our watchapp is
 * open on the watch, and delivers AppMessages here.
 *
 * The service instance is owned by the binder, not by us — messages are
 * funneled through [Pk2Bus] to whoever is consuming (MonitorService), with
 * a small buffer for messages that arrive before the service is up.
 */
class PebbleKit2ListenerService : BasePebbleListenerService() {

    override suspend fun onMessageReceived(
        watchappUUID: UUID,
        data: PebbleDictionary,
        watch: WatchIdentifier,
    ): ReceiveResult {
        if (watchappUUID.toString() != Protocol.WATCHAPP_UUID) {
            CmLog.w(TAG, "message for foreign watchapp $watchappUUID — nack")
            return ReceiveResult.Nack
        }
        val map = data.entries.associate { (k, item) -> k.toInt() to unwrap(item) }
        CmLog.d(TAG, "pk2 rx $map")
        ensureMonitorRunning()
        Pk2Bus.deliverMessage(map)
        return ReceiveResult.Ack
    }

    override fun onAppOpened(watchappUUID: UUID, watch: WatchIdentifier) {
        if (watchappUUID.toString() != Protocol.WATCHAPP_UUID) return
        CmLog.i(TAG, "watchapp opened on watch ${watch}")
        ensureMonitorRunning()
        Pk2Bus.deliverAppOpened()
    }

    override fun onAppClosed(watchappUUID: UUID, watch: WatchIdentifier) {
        if (watchappUUID.toString() != Protocol.WATCHAPP_UUID) return
        CmLog.i(TAG, "watchapp closed on watch $watch")
        Pk2Bus.deliverAppClosed()
    }

    private fun unwrap(item: PebbleDictionaryItem): Any = when (item) {
        is PebbleDictionaryItem.Text -> item.value
        is PebbleDictionaryItem.Bytes -> item.value
        is PebbleDictionaryItem.Int8 -> item.value.toInt()
        is PebbleDictionaryItem.UInt8 -> item.value.toInt()
        is PebbleDictionaryItem.Int16 -> item.value.toInt()
        is PebbleDictionaryItem.UInt16 -> item.value.toInt()
        is PebbleDictionaryItem.Int32 -> item.value
        is PebbleDictionaryItem.UInt32 -> item.value.toInt()
    }

    /** We are bound by the (foreground) Core app, which permits FGS start. */
    private fun ensureMonitorRunning() {
        runCatching {
            startForegroundService(Intent(this, MonitorService::class.java))
        }.onFailure { CmLog.w(TAG, "could not start MonitorService: $it") }
    }

    companion object { private const val TAG = "PebbleKit2Listener" }
}
