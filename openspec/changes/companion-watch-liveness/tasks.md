# Tasks: companion-watch-liveness

- [x] 1.1 DataLogReceiver: legacy broadcast protocol, defensive
      parsing, ACK per record, REQUEST_DATA flush helper; v2 14-byte
      record with change/motion ages + diag flags
- [x] 1.2 Liveness accounting from records; flush-latency median in
      CmLog; worker-eviction watchdog (armed only after first record)
- [x] 1.3 S5 field measurement (adb, 2026-08-27): zero records in 10
      min with per-minute flush requests — Core app does not forward
      the legacy protocol; PK2 1.2.0 has no DataLogging API. NO-GO
      recorded in M0-SPIKES; upstream request drafted
      (docs/UPSTREAM-PK2-DATALOGGING.md)
- [x] 1.4 Fallback per owner decision (2026-08-27): user-configurable
      periodic sync — watchSyncIntervalMin (default 60, 0 = off) in
      the Advanced settings; MonitorService launches the watchapp via
      the throttled self-heal path when watch data age exceeds the
      interval; delta spec rescoped to match
- [ ] 2.1 Owner field check: with the interval at 60, the notification
      sync age stays ≤ ~1 h across an afternoon with the watchapp
      closed (one brief watchface flash per hour); setting 0 stops the
      launches. Then archive.
