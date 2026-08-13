package org.cryomonitor.companion

/**
 * In-process bridge between [PebbleKit2ListenerService] (instantiated by the
 * Core app's binder) and [MonitorService]. Buffers a few messages so nothing
 * is lost while MonitorService is still starting.
 */
object Pk2Bus {
    interface Consumer {
        fun onPk2Message(data: Map<Int, Any>)
        fun onPk2AppOpened()
        fun onPk2AppClosed()
    }

    private const val MAX_PENDING = 16
    private val pending = ArrayDeque<Map<Int, Any>>()

    @Volatile private var consumer: Consumer? = null

    @Synchronized
    fun attach(c: Consumer) {
        consumer = c
        while (pending.isNotEmpty()) c.onPk2Message(pending.removeFirst())
    }

    @Synchronized
    fun detach(c: Consumer) {
        if (consumer === c) consumer = null
    }

    @Synchronized
    fun deliverMessage(data: Map<Int, Any>) {
        val c = consumer
        if (c != null) {
            c.onPk2Message(data)
        } else {
            pending.addLast(data)
            while (pending.size > MAX_PENDING) pending.removeFirst()
        }
    }

    fun deliverAppOpened() { consumer?.onPk2AppOpened() }
    fun deliverAppClosed() { consumer?.onPk2AppClosed() }
}
