# Delta: detector-ladder — not-worn nag arbitrates with the hunt

## MODIFIED Requirements

### Requirement: Not-worn nag never escalates to contacts
When the watch appears off-wrist (HR hardware: no pulse AND no motion
for notworn_after_min, default 3), the system SHALL first run the
silent 1 Hz pulse hunt (pulse_hunt_s) rather than nag: at the 60 s
idle cadence a sleeping wearer's steady bpm repeats exactly like a
frozen off-body reading (field 2026-09-09). A bpm value change during
that hunt SHALL end it silently, re-arm the episode, and suppress
further not-worn hunts for a cooldown (10 min). Only if the 1 Hz
stream stays flat or absent for the whole hunt SHALL the nag be
emitted to the wearer (watch + phone), exactly once per episode, and
it SHALL NOT notify contacts. Removal-classified pulse losses route
here. A not-worn hunt SHALL never escalate into the alert ladder. The
watch-side nag screen SHALL release the display after 3 min if
unanswered; the phone notification persists.

#### Scenario: Watch left on the nightstand nags once
- **WHEN** the watch sees neither pulse nor motion for notworn_after_min
  without a suspension
- **AND** the arbiter hunt sees no changing value for pulse_hunt_s
- **THEN** one CM_ACT_NOTWORN_NAG is emitted
- **AND** no repeat nag occurs until pulse or motion returns

#### Scenario: Sleeping wearer with a steady pulse is not nagged
- **WHEN** the wearer is motionless and the idle-cadence bpm repeats
  the same value for notworn_after_min
- **THEN** a silent hunt starts, the 1 Hz stream shows a changing
  value within seconds, the hunt ends with no user-visible alert
- **AND** no further not-worn hunt starts within the cooldown

#### Scenario: Unanswered nag hands the screen back
- **WHEN** a not-worn or sensor-fault nag has been on the watch screen
  for 3 min without a button press
- **THEN** the watch returns to the watchface while the phone
  notification remains
