# Delta: watch-phone-protocol — acknowledged alarm delivery

## ADDED Requirements

### Requirement: Alarm episodes are identified end-to-end
Every ladder episode SHALL carry a 16-bit episode ID minted by the
detector core when the episode starts (persisted sequence, unique
across worker restarts; 0 reserved for none/legacy). The ID rides in
ladder AppMessages (KEY_EPISODE), in heartbeat record v3, and in the
worker↔app status/action messages. The companion deduplicates
escalation and alarm UI by episode ID: the same episode never
escalates twice, regardless of which channel delivered it or how many
times.

#### Scenario: Duplicate delivery does not duplicate escalation
- **WHEN** the same ALARM reaches the phone twice (retry + DataLogging
  recovery)
- **THEN** exactly one escalation and one alarm UI result

### Requirement: Alarm delivery is acknowledged and retried
PRE_ALARM and ALARM AppMessages SHALL be retried (outbox-failure and
missing app-level ACK both count as non-delivery) with bounded
backoff for as long as the stage is active, and the companion SHALL
answer them with PMSG_ALARM_ACK carrying the episode ID. A
fire-and-forget confirmed alarm is prohibited.

#### Scenario: Bluetooth blip during the alarm
- **WHEN** the ALARM send fails or is never ACKed because the link
  dropped for 30 s
- **THEN** the watch keeps retrying and the phone receives the alarm
  after the link returns, within one backoff step

### Requirement: DataLogging is an authoritative alarm recovery channel
Heartbeat record v3 SHALL carry stage, detector, and episode ID. When
the companion receives a record reporting COUNTDOWN or ALARM for an
episode it has not escalated, it SHALL run the full corresponding
alarm path (pre-alarm UI for COUNTDOWN; siren + UI + server
escalation for ALARM), deduplicated by episode. v2 (14-byte) records
remain parseable with time-based recovery dedup.

#### Scenario: Live channel lost entirely
- **WHEN** the alarm AppMessage never arrives (app crashed, link out)
  but a spooled record with stage=ALARM flushes later
- **THEN** the phone escalates from the record alone

### Requirement: Watch cancel is acknowledged, never assumed
The watch app SHALL show a cancelling state and clear the alert UI
only on the worker's ALERT_CANCELLED echo for that episode. An
unacknowledged WMSG_USER_OK is retried; on continued failure the UI
says so and the ladder proceeds (fail toward false alarm, never
toward missed alarm).

#### Scenario: Worker IPC fails during cancel
- **WHEN** the wearer presses SELECT and the worker message cannot be
  delivered
- **THEN** the screen shows the failure (not a successful-looking
  dismissal) and the ladder continues unless a retry succeeds

## MODIFIED Requirements

### Requirement: Worker alarms survive the launch gap
Because the background worker cannot vibrate, draw UI, or send
AppMessage, any action that does launch the app SHALL be parked in
persist storage (PK_PENDING_ACTION=2) before `worker_launch_app()`;
the foreground app consumes it on launch, or live via WMSG_ACTION if
already open. Parked LADDER actions (CHECKIN/COUNTDOWN/ALARM) never
expire by age; parked informational actions keep the 60 s staleness
expiry. WMSG_STATUS SHALL carry the current stage, its detector, the
episode ID, and seconds remaining in the stage, and on foreground
start (and any status receipt) the app SHALL reconstruct the alert UI
and phone notification for an active ladder stage it is not already
showing — status reconciliation, not parked-action age, corrects
staleness. Worker↔app messages use AppWorkerMessage types: ACTION=1,
USER_OK=2, SUSPEND=3, RESUME=4, SOS=5, STATUS_REQ=6, STATUS=7,
SET_DEBUG=8, DRILL=9, HR_LAB=10, HR_SAMPLE=11, DIAG=12,
SET_QMETRIC=13. Persist keys: CONFIG=1, PENDING_ACTION=2, MODE=3,
SUSPEND_UNTIL=4, SUSPEND_AUTORESUME=5, DEBUG=6, DRILL_FIRE_MS=7,
DRILL_ARM_MS=8, BUILD_ID=9, PENDING_ACTION_T=10, QMETRIC=11,
EPISODE_SEQ=12.

#### Scenario: Alarm reaches the phone from a closed watchapp
- **WHEN** the worker's ladder needs the CHECKIN UI while the foreground
  app is closed
- **THEN** the action is persisted, the app is launched by the worker,
  picks the action up, vibrates, and sends the corresponding PMSG

#### Scenario: Launch delayed past the informational window
- **WHEN** the worker parks an ALARM but the app launch takes 3 min
- **THEN** the app still raises the alarm — from the parked action or
  from status reconciliation, whichever runs first

#### Scenario: Stale parked nag after reboot
- **WHEN** a parked informational nag from a previous session is found
  at startup
- **THEN** it is discarded (existing behavior preserved)
