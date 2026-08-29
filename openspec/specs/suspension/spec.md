# Suspension Specification

## Purpose

Lets the wearer pause monitoring for watch-off periods (shower, swim,
sauna, charging) without generating false alarms — and resumes protection
automatically when the watch is worn again. Implemented in the detector
core (`cm_suspend` / `tick_suspension` in `watchapp/src/core/detectors.c`);
covered by the suspension tests in `watchapp/tests/test_detectors.c`.

## Requirements

### Requirement: Preset and custom durations
Suspension SHALL offer presets of 30 min, 1 h, and 2 h plus a custom
duration, startable from the watch menu or the phone app.

#### Scenario: Preset suspension from the watch
- **WHEN** the wearer selects a 30-minute suspension on the watch
- **THEN** monitoring pauses and the remaining time is queryable on both
  watch and phone

### Requirement: Suspension silences all detectors
While suspended, no detector SHALL trigger and no pulse hunts SHALL run.
Starting a suspension cancels an active CHECKIN or COUNTDOWN with reason
SUSPEND — but a latched ALARM is NOT cleared by suspending; an alarm in
progress must be explicitly cancelled.

#### Scenario: Watch on the shelf stays silent
- **WHEN** monitoring is suspended and the watch sees no pulse and no
  motion for the whole period
- **THEN** no detector actions are emitted until expiry

### Requirement: Optional auto-resume on wear signals
When auto-resume is enabled (default on), monitoring SHALL resume
before expiry only on combined wrist evidence: resume_motion_s
consecutive seconds of motion (default 15), evaluated after an arming
grace of resume_grace_s (default 60 s) from the suspension start,
AND — on HR hardware — a live pulse: a bpm VALUE CHANGE recorded
during this suspension after the grace, no older than
resume_pulse_fresh_s (default 150 s) when the motion run completes.
Motion alone MUST NOT resume on HR hardware: a suspended watch
carried in a bag or pocket moves for minutes at a time, and being
carried off-wrist is a valid reason to be suspended (owner decision
2026-08-29). Liveness is a CHANGING value (S4): the patched firmware
reads 0 off-body and a frozen reading never changes, so a bag ride
cannot fake the pulse half. On hardware without an HR sensor,
sustained motion alone still resumes (documented limitation). During
the grace period all signals are ignored — the wearer is usually
still wearing or handling the watch when the suspension begins. If
the sensor is dead, auto-resume never fires and the suspend timer
remains the backstop; the sensor-fault nag then covers the blind
window after expiry.

#### Scenario: Putting the watch back on resumes early
- **WHEN** suspension is active with auto-resume enabled and the grace
  period has passed
- **AND** the wearer wears the watch with sustained motion and the HR
  sensor produces changing readings (~30 s warm-up)
- **THEN** AUTO_RESUMED is emitted and detectors re-arm

#### Scenario: Suspending while still wearing does not instantly resume
- **WHEN** the wearer starts a suspension without taking the watch off
  and keeps moving
- **THEN** no resume occurs within the grace period

#### Scenario: Stale pulse does not resume
- **WHEN** a suspended watch lies against a surface and the HR sensor
  produces readings (phantom or stale)
- **THEN** the suspension continues; readings without sustained motion
  never resume it

#### Scenario: Carried in a bag stays suspended
- **WHEN** a suspended watch rides in a bag or pocket with sustained
  motion but no changing bpm
- **THEN** the suspension continues to its timer; motion alone is not
  wrist evidence on HR hardware

### Requirement: Expiry re-arms cleanly
At expiry or resume, all detector baselines SHALL reset to "now" so the
stillness and pulse-absence accumulated during suspension cannot
instantly trigger an alert. The wearer is notified (vibration + phone
message); if wear signals remain absent after expiry, the not-worn nag
ladder applies — never a contact alarm.

#### Scenario: Expiry does not instantly alarm
- **WHEN** a suspension expires while the watch is still off-wrist
- **THEN** SUSPEND_EXPIRED is emitted with no immediate pulse-loss or
  non-motion trigger, and the not-worn nag follows if wear signals stay
  absent

### Requirement: Suspension survives restarts and is visible remotely
The suspension end-time and auto-resume flag SHALL be persisted on the
watch (the worker restores them on relaunch), reported to the phone
(PMSG_SUSPENDED), and included in server heartbeats so the dashboard can
show "suspended until HH:MM". The server SHALL keep expecting phone
heartbeats during suspension — suspension pauses detectors, not the
dead-man monitor.

#### Scenario: Worker restart preserves an active suspension
- **WHEN** the worker restarts (reboot, relaunch) during a suspension
- **THEN** the remaining suspension window is restored from persist
  storage rather than resuming monitoring early

### Requirement: Recurring suspension schedules are user-approved
Recurring windows (e.g. pool every Tue/Thu 18:00–19:30) SHALL be
supported via phone-side configuration. The learning layer MAY propose
such windows from observed patterns, but only explicit user approval
activates them (deterministic-alarm-path principle).

#### Scenario: Learning proposes, the user disposes
- **WHEN** the pattern miner detects a weekly watch-off window
- **THEN** a suggestion is presented to the user
- **AND** no automatic suspension occurs unless the user approves it

### Requirement: Charging is an implicit suspension
When the watch reports charger power (`is_plugged`), monitoring SHALL
hold automatically: no detector may trigger and no hunts may run,
exactly as during a suspension. Entering the hold SHALL cancel an
active CHECKIN or COUNTDOWN with reason SUSPEND; a latched ALARM SHALL
NOT be cleared by docking the watch. On unplug, all detector baselines
SHALL reset so time on the charger cannot trigger anything instantly,
and the not-worn nag re-arms. The hold state SHALL be visible on the
watch ("Charging") and in the phone notification ("ON CHARGER"). The
hold ends with the charger — an unplugged, unworn watch is subject to
the normal removal/arrest rules thereafter.

#### Scenario: Watch charges overnight in silence
- **WHEN** the watch sits on its charger with no pulse and no motion
  for hours
- **THEN** no nag, hunt, check-in, or alarm is emitted, and the phone
  shows the charging hold

#### Scenario: Docking mid-check-in cancels it, docking mid-alarm does not
- **WHEN** the wearer docks the watch during a CHECKIN stage
- **THEN** the alert cancels with reason SUSPEND
- **AND** if a latched ALARM was active instead, it remains latched

#### Scenario: Unplugging does not instantly alert
- **WHEN** the watch comes off the charger and is worn again
- **THEN** no detector fires from the stillness accumulated while
  charging

### Requirement: Carry suspension (timer-only) from the watch
The watch UI SHALL offer a timer-only suspension ("carry" mode,
long-press UP, 120 min default) alongside the auto-resuming presets.
Rationale (field finding 2026-08-29): a hand-carried watch puts real
skin on the optical sensor — a palm reads as a pulse exactly like a
wrist, so no resume heuristic can distinguish hand-carry from wear.
The wearer knows why they suspended; deliberate off-wrist transport
gets a suspension that only the timer (or an explicit resume) ends.

#### Scenario: Hand-carried watch stays suspended in carry mode
- **WHEN** the wearer starts a carry suspension and transports the
  watch in hand (motion + real pulse from the palm)
- **THEN** monitoring stays suspended until the timer expires or the
  wearer resumes explicitly
