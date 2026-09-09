# Sustained motion, not a single jerk, dismisses the ladder

## Why

Field event 2026-09-09 11:46 (desk test, worker records + companion
log): the watch lay on a desk that was being used. The pulse-loss
ladder started a CHECKIN at 11:46:33 and a desk bump cancelled it
within one second (reason MOTION). Earlier bumps had also repeatedly
stood the silent hunt down, and one of them — 147 s after the watch
was set down — erased the "removal" classification, which is why the
ladder ran at all instead of the not-worn nag.

On a desk that is harmless. In bed it is not: the same single-jerk
rule lets a partner turning over, a train seat, or a washing machine
next door keep cancelling the check-in of a wearer with no pulse. After
ten minutes the system downgrades to a wearer-only sensor-fault nag and
contacts are never reached. The ladder's premise "motion means alive"
is sound for a wrist that moves itself; it breaks for motion
transmitted through a surface.

## What changes

- **Sustained motion** is defined in the core: jerk above
  motion_jerk_mg in at least 3 distinct seconds within a 10 s window
  (compile-time constants, same reason as the not-worn cooldown:
  cm_config is size-checked and phone-pushed).
- **Only sustained motion** dismisses a pulse/impact/non-motion
  CHECKIN (reason MOTION) or stands a pulse hunt down. Single bumps are
  ignored by the ladder.
- **Ladder stillness and removal classification** use the last
  *sustained* motion: bumps no longer delay a hunt, and a bump at the
  moment the pulse stops no longer routes a collapse to the not-worn
  nag (a documented residual risk shrinks). Handling a watch to take it
  off is sustained motion and still classifies as removal.
- Unchanged: the non-motion detector, the not-worn and sensor-fault
  nags, suspension auto-resume and the impact settle window keep using
  raw motion (micro-movement liveness is their whole point).

## Impact

- Specs: `detector-ladder` (MODIFIED "Staged escalation with
  implicit-then-explicit cancellation", MODIFIED "Pulse-loss detection
  on HR hardware").
- Code: `watchapp/src/core/detectors.{h,c}` (sustained-motion window,
  `last_sustained_ms`, note_motion, tick_pulse, removal_suspected,
  baselines).
- Tests: `test_bumps_do_not_dismiss_pulse_ladder` (bed-partner
  scenario through to ALARM), `test_sustained_motion_still_dismisses`;
  three existing single-jerk dismissals updated. 
- Release: watchapp 0.5.5. No protocol change.

## Trade-off accepted

A live wearer whose pulse hunt failed (rare by S4 data) now has to
either press the button or move for three seconds; a single twitch no
longer cancels the check-in. The phone-side cancel window absorbs the
rest. Fewer false negatives at the cost of a slightly louder rare case.
