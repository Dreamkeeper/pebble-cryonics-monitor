# Delta: suspension — hold sensor cadence and manual resume

## MODIFIED Requirements

### Requirement: Suspension silences all detectors
While suspended, no detector SHALL trigger and no pulse hunts SHALL
run. Starting a suspension cancels an active CHECKIN or COUNTDOWN with
reason SUSPEND — but a latched ALARM is NOT cleared by suspending; an
alarm in progress must be explicitly cancelled. While suspended (any
mode) or on the charger, the shell SHALL lower the HR sensor sample
cadence to one sample per 300 s (from the 60 s monitoring cadence),
restoring the monitoring cadence on resume, expiry or unplug; the
sensor's only job while held is to feed auto-resume and the expiry
re-arm. Auto-resume MAY therefore lag by up to one sample period plus
the pulse freshness window; the manual resume below is the fast path.

#### Scenario: Watch on the shelf stays silent
- **WHEN** monitoring is suspended and the watch sees no pulse and no
  motion for the whole period
- **THEN** no detector actions are emitted until expiry

#### Scenario: Held watch samples the sensor sparingly
- **WHEN** a suspension starts, or the watch is docked
- **THEN** the HR sample period becomes 300 s until the hold ends,
  and returns to 60 s when monitoring resumes

## ADDED Requirements

### Requirement: Manual resume from the watch (SELECT)
A SELECT press on the watch's main screen while suspended SHALL end
the suspension immediately in every mode, including timer-only carry
mode, resetting detector baselines and rescheduling the check-in
exactly as an auto-resume does; the phone SHALL learn of the resume
through the existing resume path. A latched ALARM SHALL take
precedence: the first press cancels the alarm and the suspension
continues; the next press resumes. Charger holds are not ended by
SELECT.

#### Scenario: Wearer resumes by check-in
- **WHEN** a suspension (auto-resume or carry) is active and the
  wearer presses SELECT
- **THEN** AUTO_RESUMED is emitted at once, the remaining suspension
  is zero, and no detector fires from the stillness accumulated while
  suspended

#### Scenario: Alarm latched during suspension
- **WHEN** an ALARM is latched and the watch is then suspended, and
  the wearer presses SELECT
- **THEN** the alarm is cancelled and the suspension remains active
- **AND** a further SELECT press resumes monitoring
