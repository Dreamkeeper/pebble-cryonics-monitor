# Delta: detector-ladder — sustained motion dismisses, bumps do not

## MODIFIED Requirements

### Requirement: Staged escalation with implicit-then-explicit cancellation
An alert SHALL pass through CHECKIN (default 30 s, configurable) and
COUNTDOWN (default 30 s; 20 s for impact; 5 s for manual SOS) before the
ALARM action is emitted. During CHECKIN, SUSTAINED wearer motion SHALL
auto-dismiss the alert (except for scheduled check-ins and SOS); during
COUNTDOWN, only an explicit button press cancels. Sustained motion is
a jerk above motion_jerk_mg in at least three distinct seconds within
a ten-second window. A single jerk is a bump — a desk in use, a bed
partner turning, a vehicle — and SHALL NOT dismiss a check-in (field
2026-09-09: a desk bump cancelled a pulse-loss CHECKIN one second
after it started).

#### Scenario: Motion dismisses a check-in
- **WHEN** a pulse-loss, impact, or non-motion alert is in CHECKIN stage
- **AND** sustained wrist motion is detected (three motion-seconds
  within ten seconds)
- **THEN** the alert is cancelled with reason MOTION and no alarm fires

#### Scenario: A bump does not dismiss a check-in
- **WHEN** a pulse-loss alert is in CHECKIN stage
- **AND** isolated jerks arrive (one every 15 s, as from a surface
  being bumped)
- **THEN** the check-in continues to COUNTDOWN and ALARM unless a
  button press or a changed pulse value cancels it

#### Scenario: Motion does NOT dismiss a countdown
- **WHEN** any alert has advanced to COUNTDOWN stage
- **AND** motion is detected
- **THEN** the countdown continues; only an explicit "I'm OK" cancels

#### Scenario: Ladder exhaustion latches the alarm
- **WHEN** a COUNTDOWN expires without user cancellation
- **THEN** CM_ACT_ALARM is emitted with the originating detector
- **AND** the alarm state persists until the user presses "I'm OK"

### Requirement: Pulse-loss detection on HR hardware
On watches with a heart-rate sensor (emery, diorite), pulse LIVENESS
SHALL mean a CHANGING raw value: the S4 sensor lab (2026-08-27, Time 2)
proved that off-body the firmware keeps serving the last computed bpm
with fresh events — bit-identical for many minutes — while a living
wearer's raw bpm always jitters. A frozen reading is only evidence the
watch was recently worn (grace), never evidence of life.

The system SHALL start a silent "pulse hunt" (HR burst sampling at 1 s
for pulse_hunt_s, default 45 s — burst spin-up was measured at ~23 s)
when EITHER readings have been absent for pulse_lost_after_s (default
150 s) OR the value has not changed for pulse_flat_after_s (default
300 s) while the wearer is still (no SUSTAINED motion for
pulse_still_s, default 20 s — single bumps do not count) and the
watch was recently worn (a reading within pulse_worn_grace_min, default
10 min). Only a CHANGED value ends a hunt or dismisses a pulse-loss
CHECKIN; a frozen feed lets the ladder proceed — the 1 Hz hunt is the
arbiter between alive-at-rest (jitters within seconds) and frozen.
Readings below pulse_min_bpm (default 25) count as no signal. Sustained
motion during a hunt stands it down silently; single bumps do not.

Before starting a hunt, the episode SHALL be classified: SUSTAINED
motion within removal_window_s (default 45 s) of the last VALUE CHANGE
is PROBABLE REMOVAL — a dead wearer does not move as the pulse stops,
while removing a watch is seconds of handling — and the pulse ladder
SHALL NOT run; the not-worn nag owns the episode. A freeze with no
sustained motion near the change moment (still wearer) keeps the full
ladder; a single bump at that moment no longer diverts a collapse to
the nag. Residual risks are documented and accepted: a removal too
gentle to register sustained motion still runs the ladder (the phone
cancel absorbs it).

#### Scenario: Pulse loss escalates through the full ladder
- **WHEN** the pulse stops or freezes while the wearer is still
- **AND** no sustained motion occurred near the last value change
- **AND** the silent hunt sees no CHANGING value
- **THEN** CHECKIN starts, then COUNTDOWN, then ALARM, and HR burst
  sampling is released when the ladder ends

#### Scenario: Bumped surface does not block the ladder
- **WHEN** the pulse is absent and the surface receives an isolated
  jerk every 15 s
- **THEN** the hunt still starts and concludes, and CHECKIN follows

#### Scenario: Taking the watch off routes to the nag, not the ladder
- **WHEN** the pulse value freezes (readings may continue frozen)
- **AND** sustained motion was observed within removal_window_s of the
  last value change (handling: unbuckling, setting the watch down)
- **THEN** no hunt and no CHECKIN start for this episode
- **AND** the not-worn nag fires at its own threshold despite the
  continuing frozen readings

#### Scenario: Pulse returns during the hunt
- **WHEN** a pulse hunt is running
- **AND** a reading with a CHANGED value arrives
- **THEN** the hunt ends silently with no user-visible alert

#### Scenario: Returning pulse dismisses the check-in
- **WHEN** a pulse-loss alert is in CHECKIN stage
- **AND** a reading with a CHANGED value arrives
- **THEN** the alert cancels with reason PULSE

#### Scenario: User cancellation snoozes re-triggering
- **WHEN** the user cancels a pulse-loss alert
- **THEN** pulse-loss SHALL NOT re-trigger for pulse_snooze_min
  (default 10 min) unless a valid pulse is seen first
