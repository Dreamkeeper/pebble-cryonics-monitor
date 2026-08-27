# Delta: watch-phone-protocol — S1 latency drill

## ADDED Requirements

### Requirement: Latency drill measures the real alarm path on demand
The system SHALL provide an operator-triggered latency drill that
exercises the production alarm path end to end: worker fire → persist
handoff → `worker_launch_app()` → foreground vibration → AppMessage to
the phone. The drill SHALL be triggerable from the web dashboard
(admin; queued via the heartbeat command channel, delivered exactly
once) and from the companion app (immediate). The worker SHALL wait a
fixed arming delay (default 10 s) after the watchapp exits so the
measured launch is a genuine cold start. Results SHALL be recorded as
a server event carrying the watch-measured launch milliseconds, the
phone-side round-trip estimate, and the phone model.

#### Scenario: Dashboard-triggered drill produces a measurement
- **WHEN** an admin queues a latency drill for a wearer
- **THEN** the phone receives the command on its next heartbeat, at
  most once
- **AND** within the arming delay plus a few seconds the watch
  vibrates, shows the launch latency, and a `latency_drill` event with
  launch_ms and phone_model appears in the wearer's event feed

#### Scenario: Drill leaves no residue
- **WHEN** a latency drill completes
- **THEN** no ladder stage, escalation, or persisted pending action
  remains, and the watchapp returns to the watchface shortly after
  showing the result
