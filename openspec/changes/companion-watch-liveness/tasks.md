# Tasks: companion-watch-liveness

- [x] 1.1 DataLogReceiver: legacy broadcast protocol (RECEIVE_DATA,
      ACK per record, FINISH_SESSION, REQUEST_DATA flush helper),
      defensive parsing, loud debug logging (the S5 experiment)
- [x] 1.2 cm_heartbeat_rec parsing (8-byte LE) -> MonitorService via
      ACTION_WORKER_HEARTBEAT (startForegroundService: delivers to the
      live FGS and revives a dead one)
- [x] 1.3 Liveness accounting: lastWatchDataT + watch battery refresh
      from records; flush-latency ring + running median in CmLog
- [x] 1.4 Worker-eviction watchdog (armed after first record; 10 min
      threshold; FAULT + watchapp relaunch self-heal); per-minute
      REQUEST_DATA flush
- [x] 1.5 Manifest receiver; builds green
- [x] 2.1 S5 field run (adb-monitored, 2026-08-27): ~10 min, worker
      running, BT up, REQUEST_DATA broadcast every 60 s — ZERO
      DataLogging broadcasts received. The Core app does not forward
      the legacy protocol; PK2 has no DataLogging API. NO-GO.
- [ ] 2.2 Recorded in M0-SPIKES. Receiver stays (inert, future-proof);
      eviction watchdog correctly dormant. Owner decision pending on
      the fallback (periodic worker status sync vs. accept app-open
      sync ages) — then rescope or archive this change accordingly
