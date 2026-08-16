# Delta: detector-ladder — wear discrimination

## MODIFIED Requirements

### Requirement: Pulse-loss detection on HR hardware
On watches with a heart-rate sensor (emery, diorite), the system SHALL
start a silent "pulse hunt" (HR burst sampling at 1 s for pulse_hunt_s,
default 30 s) when the raw HR signal has been absent for
pulse_lost_after_s (default 150 s — MUST exceed 2x the normal HR sample
period, user-configurable) while the wearer is still (pulse_still_s,
default 20 s) and the watch was recently worn (pulse seen within
pulse_worn_grace_min, default 10 min). Only a failed hunt escalates to
CHECKIN. Readings below pulse_min_bpm (default 25) count as no signal.

Before starting a hunt, the episode SHALL be classified: if motion was
observed within removal_window_s (default 45 s) AFTER the last valid
pulse reading, the loss is PROBABLE REMOVAL — a dead wearer does not
move after the pulse stops, while removing a watch necessarily moves
it — and the pulse ladder SHALL NOT run; the not-worn nag owns the
episode. A loss with no motion after the last pulse (still wearer)
keeps the full ladder. Residual risks are documented and accepted: a
collapse whose motion lands just after the last pulse reading routes to
the nag (the impact detector covers falls), and a removal too gentle to
register motion still runs the ladder (the phone cancel absorbs it).

#### Scenario: Pulse loss escalates through the full ladder
- **WHEN** the pulse signal disappears while the wearer is still
- **AND** no motion occurred after the last valid pulse reading
- **AND** the silent hunt finds no pulse
- **THEN** CHECKIN starts, then COUNTDOWN, then ALARM, and HR burst
  sampling is released when the ladder ends

#### Scenario: Taking the watch off routes to the nag, not the ladder
- **WHEN** the pulse signal disappears
- **AND** motion was observed within removal_window_s after the last
  valid pulse (handling: unbuckling, setting the watch down)
- **THEN** no hunt and no CHECKIN start for this episode
- **AND** the not-worn nag fires at its own threshold

#### Scenario: Pulse returns during the hunt
- **WHEN** a pulse hunt is running
- **AND** a valid HR reading arrives
- **THEN** the hunt ends silently with no user-visible alert

#### Scenario: Returning pulse dismisses the check-in
- **WHEN** a pulse-loss alert is in CHECKIN stage
- **AND** a valid HR reading arrives
- **THEN** the alert cancels with reason PULSE

#### Scenario: User cancellation snoozes re-triggering
- **WHEN** the user cancels a pulse-loss alert
- **THEN** pulse-loss SHALL NOT re-trigger for pulse_snooze_min
  (default 10 min) unless a valid pulse is seen first

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
