# Delta: watch-phone-protocol — worker liveness via DataLogging

## ADDED Requirements

### Requirement: Worker heartbeats reach the phone while the watchapp is closed
The background worker SHALL log a compact heartbeat record (epoch,
ladder stage, watch battery, last bpm, suspension flag) to a
DataLogging session every 60 s, and the companion SHALL receive these
records via the DataLogging transport, acknowledging each one. Each
received record SHALL refresh the watch-data age (notification and
server heartbeat), update the reported watch battery, and record its
flush latency for the S5 measurement. Once records have ever been
received, their absence for longer than a threshold (default 10 min)
while the Bluetooth link is up SHALL raise a wearer-facing FAULT
("worker stopped reporting — possibly evicted") and trigger a watchapp
relaunch to re-arm the worker. The companion SHALL request a flush of
buffered records periodically. If the phone-side Pebble app never
delivers DataLogging broadcasts, the receiver SHALL stay inert — no
faults, no degradation of existing behavior.

#### Scenario: Watch liveness without opening the watchapp
- **WHEN** the watchapp stays closed for an hour while the worker runs
- **THEN** the phone's watch-data age keeps refreshing from worker
  records instead of growing unboundedly

#### Scenario: Worker eviction is detected and healed
- **WHEN** records have been flowing and then stop for the threshold
  while the link is up
- **THEN** a FAULT notification names the worker as the failed leg and
  the companion relaunches the watchapp to restart it
