# Delta: detector-ladder — own vibration is not motion; unworn impact is not a fall

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
after it started). The watch's OWN vibration SHALL never count as
motion: samples flagged as taken during the motor run are discarded,
and jerks within 1.5 s after such a sample are ignored as the case
ringing on a hard surface (field 2026-09-09 12:33: an impact CHECKIN's
own buzzes at 0, 5 and 10 s counted as sustained motion and cancelled
it).

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

#### Scenario: A check-in survives its own buzzes on a hard surface
- **WHEN** a CHECKIN vibrates every 5 s while the watch lies on a hard
  surface that rings after each buzz
- **THEN** the aftershocks are not motion, the check-in is not
  cancelled, and COUNTDOWN follows

#### Scenario: Motion does NOT dismiss a countdown
- **WHEN** any alert has advanced to COUNTDOWN stage
- **AND** motion is detected
- **THEN** the countdown continues; only an explicit "I'm OK" cancels

#### Scenario: Ladder exhaustion latches the alarm
- **WHEN** a COUNTDOWN expires without user cancellation
- **THEN** CM_ACT_ALARM is emitted with the originating detector
- **AND** the alarm state persists until the user presses "I'm OK"

### Requirement: Impact detection
The system SHALL detect (a) freefall (magnitude < freefall_below_mg,
default 300) followed by impact (> impact_above_mg, default 2400) within
freefall_window_ms (default 1500), and (b) single shocks > crash_above_mg
(default 3800). After a candidate impact, a settle window
(impact_settle_s, default 5 s) is ignored, then an immobility window
(impact_immobile_s, default 60 s) must pass with no motion before CHECKIN
starts. Samples flagged did_vibrate SHALL be discarded, and no
freefall, impact or shock SHALL be recognised within 1.5 s after such a
sample (the case ringing after our own vibration). On HR hardware that
has ever seen a pulse, an impact with no valid reading between the
shock and the end of the immobility window SHALL be discarded silently:
the watch is not on a readable wrist (a set-down on a desk registers as
a shock, field 2026-09-09) and the pulse ladder and not-worn nag own
what follows; a fallen wearer keeps producing readings at the idle
cadence through that window. All thresholds are user-configurable;
defaults derive from OpenSeizureDetector and are subject to field-trial
tuning.

#### Scenario: Fall followed by immobility alarms with the fast fuse
- **WHEN** freefall→impact is detected and no motion occurs through the
  settle + immobility window
- **AND** the wearer's readings continue through that window
- **THEN** CHECKIN starts with detector IMPACT
- **AND** the COUNTDOWN uses the impact fuse (20 s)

#### Scenario: Getting up after a fall stays silent
- **WHEN** freefall→impact is detected
- **AND** motion occurs after the settle window
- **THEN** the candidate is discarded with no user-visible alert

#### Scenario: Setting the watch down is not a fall
- **WHEN** a shock is detected and no valid reading arrives through the
  settle + immobility window
- **THEN** no CHECKIN starts and no alarm fires
