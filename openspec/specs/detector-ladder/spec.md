# Detector Ladder Specification

## Purpose

Defines the detectors that decide when the wearer may be unresponsive and
the multi-stage alert ladder that turns a detection into an alarm while
absorbing false positives. Implemented in `watchapp/src/core/detectors.{h,c}`
(platform-independent, integer-only, runs in the 10.5 kB background
worker); scenario coverage lives in `watchapp/tests/test_detectors.c`
(80 checks). Ladder stages: SILENT GATES → CHECKIN ("Are you OK?") →
COUNTDOWN → ALARM (latched).

## Requirements

### Requirement: Staged escalation with implicit-then-explicit cancellation
An alert SHALL pass through CHECKIN (default 30 s, configurable) and
COUNTDOWN (default 30 s; 20 s for impact; 5 s for manual SOS) before the
ALARM action is emitted. During CHECKIN, wearer motion SHALL auto-dismiss
the alert (except for scheduled check-ins and SOS); during COUNTDOWN,
only an explicit button press cancels.

#### Scenario: Motion dismisses a check-in
- **WHEN** a pulse-loss, impact, or non-motion alert is in CHECKIN stage
- **AND** deliberate wrist motion is detected
- **THEN** the alert is cancelled with reason MOTION and no alarm fires

#### Scenario: Motion does NOT dismiss a countdown
- **WHEN** any alert has advanced to COUNTDOWN stage
- **AND** motion is detected
- **THEN** the countdown continues; only an explicit "I'm OK" cancels

#### Scenario: Ladder exhaustion latches the alarm
- **WHEN** a COUNTDOWN expires without user cancellation
- **THEN** CM_ACT_ALARM is emitted with the originating detector
- **AND** the alarm state persists until the user presses "I'm OK"

### Requirement: One alert at a time
While an alert is active (any stage), other detectors SHALL NOT start a
second ladder.

#### Scenario: Impact during a pulse-loss check-in is not double-alerted
- **WHEN** a pulse-loss alert is in CHECKIN stage
- **AND** an impact candidate occurs
- **THEN** no second ladder starts while the first alert is active

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
300 s) while the wearer is still (pulse_still_s, default 20 s) and the
watch was recently worn (a reading within pulse_worn_grace_min, default
10 min). Only a CHANGED value ends a hunt or dismisses a pulse-loss
CHECKIN; a frozen feed lets the ladder proceed — the 1 Hz hunt is the
arbiter between alive-at-rest (jitters within seconds) and frozen.
Readings below pulse_min_bpm (default 25) count as no signal.

Before starting a hunt, the episode SHALL be classified: motion within
removal_window_s (default 45 s) of the last VALUE CHANGE is PROBABLE
REMOVAL — a dead wearer does not move as the pulse stops, while
removing a watch necessarily moves it — and the pulse ladder SHALL NOT
run; the not-worn nag owns the episode. A freeze with no motion near
the change moment (still wearer) keeps the full ladder. Residual risks
are documented and accepted: a collapse whose motion lands at the
freeze moment routes to the nag (the impact detector covers falls), and
a removal too gentle to register motion still runs the ladder (the
phone cancel absorbs it).

#### Scenario: Pulse loss escalates through the full ladder
- **WHEN** the pulse stops or freezes while the wearer is still
- **AND** no motion occurred near the last value change
- **AND** the silent hunt sees no CHANGING value
- **THEN** CHECKIN starts, then COUNTDOWN, then ALARM, and HR burst
  sampling is released when the ladder ends

#### Scenario: Taking the watch off routes to the nag, not the ladder
- **WHEN** the pulse value freezes (readings may continue frozen)
- **AND** motion was observed within removal_window_s of the last value
  change (handling: unbuckling, setting the watch down)
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

### Requirement: Impact detection
The system SHALL detect (a) freefall (magnitude < freefall_below_mg,
default 300) followed by impact (> impact_above_mg, default 2400) within
freefall_window_ms (default 1500), and (b) single shocks > crash_above_mg
(default 3800). After a candidate impact, a settle window
(impact_settle_s, default 5 s) is ignored, then an immobility window
(impact_immobile_s, default 60 s) must pass with no motion before CHECKIN
starts. Samples flagged did_vibrate SHALL be discarded. All thresholds
are user-configurable; defaults derive from OpenSeizureDetector and are
subject to field-trial tuning.

#### Scenario: Fall followed by immobility alarms with the fast fuse
- **WHEN** freefall→impact is detected and no motion occurs through the
  settle + immobility window
- **THEN** CHECKIN starts with detector IMPACT
- **AND** the COUNTDOWN uses the impact fuse (20 s)

#### Scenario: Getting up after a fall stays silent
- **WHEN** freefall→impact is detected
- **AND** motion occurs after the settle window
- **THEN** the candidate is discarded with no user-visible alert

### Requirement: Non-motion detection
The system SHALL start CHECKIN when no micro-movement has been detected
for nonmotion_day_min (default 40) during day or nonmotion_night_min
(default 90) during night (night window default 23:00–07:00,
configurable), while the watch is worn. Motion is a magnitude jerk ≥
motion_jerk_mg (default 60) between consecutive samples.

On HR hardware, a valid pulse is proof of life: while a pulse was read
within pulse_proof_min (default 5 min), non-motion SHALL NOT start a
ladder — stillness alone (sleep, meditation, television) must never
ping a wearer whose pulse is visible. Non-motion on HR hardware remains
only as a backstop for a silently failing sensor: it may fire in the
band where the pulse is staler than pulse_proof_min but the worn grace
(10 min) has not lapsed. On non-HR hardware (flint, gabbro) the watch
is assumed worn and non-motion is the primary detector — a documented
limitation.

#### Scenario: Still with a live pulse stays silent
- **WHEN** a wearer on HR hardware is perfectly still past the day
  threshold
- **AND** valid pulse readings continue to arrive
- **THEN** no CHECKIN, hunt, nag, or alarm is emitted

#### Scenario: Sensor goes stale during long stillness
- **WHEN** stillness has exceeded the day threshold
- **AND** the last valid pulse is older than pulse_proof_min but
  within the worn grace
- **THEN** CHECKIN starts with detector NONMOTION

#### Scenario: Daytime stillness triggers, motion dismisses
- **WHEN** a wearer on motion-only hardware (flint/gabbro) is perfectly
  still for nonmotion_day_min
- **THEN** CHECKIN starts with detector NONMOTION
- **AND** subsequent motion cancels it with reason MOTION

#### Scenario: Night threshold is longer
- **WHEN** the local time is inside the night window
- **THEN** stillness shorter than nonmotion_night_min does not trigger

### Requirement: Scheduled check-in requires a deliberate button press
Scheduled check-in SHALL default to disabled: it is the only detector
that demands the wearer's attention while nothing is wrong, so it is
opt-in. When enabled, the wearer SHALL receive a reminder (checkin_remind_min,
default 5 min before due) and must press the check-in button within
checkin_interval_min + checkin_grace_min (defaults 240 + 15 min). A
missed deadline starts the ladder. Motion SHALL NOT satisfy a scheduled
check-in. Any check-in button press reschedules the next round.

#### Scenario: Missed check-in escalates despite motion
- **WHEN** the check-in deadline + grace passes without a button press
- **THEN** CHECKIN starts with detector CHECKIN
- **AND** wearer motion does not dismiss it — only the button does

### Requirement: Not-worn nag never escalates to contacts
When the watch appears off-wrist (HR hardware: no pulse AND no motion
for notworn_after_min, default 3), the system SHALL emit a nag to the
wearer (watch + phone) exactly once per episode, and SHALL NOT notify
contacts. Removal-classified pulse losses route here. The nag SHALL not
fire while a pulse hunt is in progress.

#### Scenario: Watch left on the nightstand nags once
- **WHEN** the watch sees neither pulse nor motion for notworn_after_min
  without a suspension
- **THEN** one CM_ACT_NOTWORN_NAG is emitted
- **AND** no repeat nag occurs until pulse or motion returns

### Requirement: Manual SOS
A deliberate long-press SHALL start a COUNTDOWN (countdown_sos_s, default
5 s, mis-press protection) that escalates to ALARM; it skips silent gates
and CHECKIN, and motion does not cancel it.

#### Scenario: SOS fires after the short fuse
- **WHEN** the wearer triggers manual SOS
- **THEN** a 5 s COUNTDOWN starts and, uncancelled, emits ALARM(SOS)

### Requirement: Detector core stays worker-safe
The detector core SHALL use no dynamic allocation, no floating point, and
no Pebble APIs, and SHALL remain host-compilable for the test suite.
Timestamps are uint32 milliseconds with wrap-safe comparisons valid for
spans < 24 days.

#### Scenario: Core compiles and passes on a host toolchain
- **WHEN** watchapp/tests/test_detectors.c is compiled with MSVC or gcc
- **THEN** the suite builds without Pebble headers and all checks pass
