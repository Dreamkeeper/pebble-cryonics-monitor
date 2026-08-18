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
- [ ] 2.1 S5 field run: wear the watch ≥1 h with the watchapp CLOSED;
      then read View logs for "WORKER HEARTBEAT via DataLogging" and
      "S5: ... median=Ns". Go-gate: records arrive at all AND median
      flush < 60 s. Also: notification "synced" age stays fresh with
      the app closed
- [ ] 2.2 If go: record S5 GO in M0-SPIKES; archive (updates
      watch-phone-protocol). If no records: record NO-GO + fall back
      to hourly worker status sync (new change)
