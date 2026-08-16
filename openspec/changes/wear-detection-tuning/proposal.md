# Proposal: wear-detection-tuning

## Why

Owner field testing (E2E round 2, 2026-08-16) exposed three wear-logic
defects with a common root: the detectors treated "still" and "off-wrist"
as interchangeable, and treated any pulse reading as trustworthy.

1. **Still-but-alive pinged the wearer.** Non-motion started a check-in
   after 40/90 min of stillness even while the HR sensor was reading a
   perfectly healthy pulse — punishing sleep, meditation, and TV.
2. **Removal was indistinguishable from arrest, and handled worse than
   either.** Taking the watch off armed the pulse-loss alarm ladder
   (contacts-facing) while the not-worn nag waited a useless 15 minutes.
3. **Suspension auto-resume trusted the wrong signals.** A fresh pulse
   reading — including the one you generate by *wearing the watch while
   pressing suspend*, and phantom readings the optical sensor produces
   against a surface — resumed monitoring instantly, sometimes within one
   second of suspending.

## What changes

- **Pulse is proof of life** (HR hardware): a valid pulse within
  `pulse_proof_min` (default 5 min) suppresses non-motion check-ins.
  Non-motion remains fully active on motion-only hardware (flint,
  gabbro) and as a stale-pulse backstop band on HR hardware.
- **Removal vs. arrest discrimination**: motion observed within
  `removal_window_s` (default 45 s) *after* the last valid pulse
  classifies a signal loss as probable removal → routed to the not-worn
  nag. Loss with no motion after the last pulse keeps the full alarm
  ladder (possible arrest). Rationale: a dead wearer does not move after
  the pulse stops; removing a watch necessarily moves it.
- **Not-worn nag at 3 minutes** (default, was 15), still wearer-only
  (watch + phone), never contacts, once per episode.
- **Auto-resume is accelerometer-only** with a 60 s arming grace
  (`resume_grace_s`): pulse readings never resume a suspension, and no
  signal counts during the first minute.
- Remaining-suspension displays round minutes **up**.

## Documented residual risks (accepted)

- A collapse whose motion lands just after the last pulse reading is
  misread as removal → nag only. Mitigated by the impact detector; to be
  characterized on real hardware (M0 spike: off-body raw-HR signature).
- A perfectly gentle removal (no detected handling motion) still runs
  the ladder → absorbed by the phone cancel, as before.
- Phantom pulse readings while off-body can postpone the not-worn nag.
  M0 spike S6 (new): record what HealthMetricHeartRateRawBPM reports on
  a table / against fabric on the Time 2.

## Impact

- Specs: detector-ladder (3 requirements), suspension (1 requirement)
- Code: watchapp/src/core/detectors.{h,c}, worker (display rounding),
  tests (80 → 105 checks)
- No server or protocol changes.
