# Tasks

- [x] 1. BootReceiver (`BOOT_COMPLETED` + `MY_PACKAGE_REPLACED`) +
       manifest permission/receiver; start only when configured;
       mark the start reason for the counters.
- [x] 2. Soak counters in MonitorService (SharedPreferences-backed
       `SoakStats`): service starts by reason, disconnects + downtime,
       DL records, worker faults, self-heals, pre-alarms/alarms,
       server send failures.
- [x] 3. Debug: Soak card (window since reset, counters, share, reset).
- [x] 4. Debug: Recovery lab — reboot stage (persisted arm marker,
       verdict vs 2-min gate on next open, pure `RebootDrill` verdict
       unit-tested) and watch-outage stage (disconnect/reconnect
       latencies), sensor-lab UI pattern.
- [x] 5. `docs/SOAK-TEST.md` — 7-day protocol, PASS gates, daily
       checklist; HyperOS Autostart setup note.
- [x] 6. Version bump 0.5.0 (29); build signed release APK to `dist/`.
- [ ] 7. Hardware verification by owner: reboot drill PASS on the
       Xiaomi 17 Ultra (HyperOS Autostart on), watch-outage drill
       reports sane latencies, counters tick over a day of wear.
