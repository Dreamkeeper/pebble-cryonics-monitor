# Delta: watch-phone-protocol — worker liveness while the watchapp is closed

## ADDED Requirements

### Requirement: Watch liveness while the watchapp is closed
The worker cannot reach the phone while the watchapp is closed (no
AppMessage from workers; the DataLogging path was measured dead in the
current phone stack — M0 S5, 2026-08-27). The companion SHALL therefore
offer a user-configurable periodic sync (default 60 min, 0 = off,
stored on the phone): when the Bluetooth link is up and the watch data
age exceeds the interval, the companion launches the watchapp briefly —
it heartbeats fresh state (liveness, battery, suspension) and the
auto-launch guard returns the watchface within seconds. Sync launches
respect the global self-heal throttle.

The worker SHALL continue writing DataLogging heartbeat records
(epoch, stage, battery, bpm, suspension, change/motion ages, flags —
14-byte v2), and the companion SHALL keep its DataLogging receiver
registered and inert: if a future phone stack starts forwarding
records, liveness upgrades to per-minute automatically, each record is
ACKed, and the worker-eviction watchdog (FAULT + relaunch after 10 min
of record silence with the link up) arms only after the first record —
never crying wolf on stacks without support.

#### Scenario: Sync age stays bounded without wearer action
- **WHEN** the watchapp stays closed and the sync interval is N minutes
- **THEN** the notification's watch-data age never exceeds ~N minutes
  plus seconds, at the cost of a brief screen flash per interval

#### Scenario: Sync off means honest staleness, not fiction
- **WHEN** the wearer sets the sync interval to 0
- **THEN** no periodic launches occur, and stale values (battery older
  than an hour) are dropped from the notification rather than shown as
  current
