# Suspension power: 5-minute sensor cadence while held, SELECT resumes

## Why

Two owner requests from the field (2026-09-07), both about the
suspended state the watch spends hours a day in (shower, sleep-off,
charger, carry):

1. **Power.** While suspended the worker kept the optical HR sensor on
   its monitoring cadence (one sample per 60 s) purely to feed
   auto-resume. That is the same LED duty cycle as active monitoring,
   for a state whose whole point is that nothing is being monitored.
   The S6 estimate sits at 4.7 projected days against a 7-day gate;
   the hold state is the cheapest place to claw some back.
2. **Control.** The only ways out of a suspension were the timer and
   the auto-resume heuristic. A wearer who has just put the watch back
   on had no direct "I am here, resume now" — and with a slower sensor
   cadence, auto-resume gets slower still, so a manual fast path is
   required, not optional.

## What changes

- **Hold cadence.** While suspended (any mode) or on the charger, the
  worker sets the HR sample period to 300 s instead of 60 s; it
  restores 60 s on resume, expiry and unplug. Pulse hunts remain
  forbidden while held (unchanged). Auto-resume semantics are
  unchanged but may lag by up to one sample period plus the freshness
  window (documented).
- **SELECT = check-in = resume.** A SELECT press on the watch's main
  screen while suspended ends the suspension immediately in every
  mode, including timer-only carry, resets baselines and reschedules
  the check-in. A latched ALARM keeps precedence: the first press
  cancels the alarm, the suspension continues, the next press resumes.
- Watch hint text: "SELECT check-in/resume".

## Out of scope

Changing the auto-resume freshness window (150 s) or the resume rule
itself; a phone-side resume button (already exists via WMSG_RESUME);
charger-hold changes beyond the sample cadence.

## Impact

- Specs: `suspension` (MODIFIED hold cadence; ADDED manual resume).
- Code: `watchapp/src/core/detectors.c` (`cm_user_ok`),
  `watchapp/worker_src/c/worker.c` (`set_hr_burst` + hold
  transitions), `watchapp/src/core/protocol.h`
  (`CM_HR_PERIOD_HOLD_S`), `watchapp/src/c/main.c` (hint).
- Tests: `test_manual_resume_by_select` (+15 checks, 178 total).
- Release: watchapp 0.5.3 (worker text 5,865 B). No protocol change;
  companion unaffected.
