package org.cryomonitor.companion

/**
 * HTTPS client for the self-hosted server (server/ in this repo).
 *
 * TODO(M2): OkHttp implementation of:
 *   POST /api/v1/heartbeat        (battery, watch-data age, suspension)
 *   POST /api/v1/alarm            (detector, location)
 *   POST /api/v1/alarm/{id}/resolve
 *   POST /api/v1/offline-window   (airplane/subway declarations)
 * plus low-battery pre-notification and server-reachability tracking
 * (mutual watchdog: the phone warns the wearer when the server is gone).
 */
class ServerClient(private val baseUrl: String, private val token: String)
