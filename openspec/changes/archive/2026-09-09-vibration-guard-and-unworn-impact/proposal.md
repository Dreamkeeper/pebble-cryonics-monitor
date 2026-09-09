# Own vibration is not motion; an impact on an unworn watch is not a fall

## Why

Field event 2026-09-09 12:33 (desk test, watchapp 0.5.5): setting the
watch on the desk registered as a shock; 60 s of immobility later the
impact CHECKIN started, buzzed at 0, 5 and 10 s — and cancelled itself
at the third buzz. The watch case rings on a hard surface after the
vibration motor stops; the OS flags only the samples taken while the
motor runs, so each buzz's aftershock counted as one motion-second, and
three buzzes in ten seconds satisfied the new sustained-motion rule
exactly. The previous day's one-second cancel of a pulse CHECKIN was
the same mechanism under the old single-jerk rule (first buzz).

Two things are wrong here, one dangerous and one merely noisy:

1. **The ladder can cancel itself.** Any check-in that vibrates on a
   hard surface — a wearer collapsed on a floor included — could be
   dismissed by its own buzzes. That must never be possible.
2. **A set-down is not a fall.** The impact detector ran a check-in for
   a watch that was demonstrably off the wrist (no reading since the
   shock). The existing off-wrist suppression only engages 5 min after
   the last value change, too late for a watch just taken off.

## What changes

- **Vibration guard.** After any accelerometer sample flagged as taken
  during our own vibration, jerks are ignored for 1.5 s — for motion,
  for freefall/impact and for shock detection. Applies to every
  detector because it changes what "motion" means at the source.
- **Unworn impact.** On HR hardware that has ever seen a pulse, an
  impact whose settle-plus-immobility window contains no valid reading
  is discarded silently; a fallen wearer keeps producing readings at
  the 60 s idle cadence through that window. A fall that also breaks
  sensor contact defers to the pulse ladder and the nags (documented).

## Impact

- Specs: `detector-ladder` (MODIFIED "Staged escalation with
  implicit-then-explicit cancellation", MODIFIED "Impact detection").
- Code: `watchapp/src/core/detectors.{h,c}` (vibe_guard_until_ms,
  cm_accel_feed, tick_impact).
- Tests: own-vibration guard + impact CHECKIN surviving its buzzes on a
  desk; impact on an unworn watch silent; impact tests now model a
  worn (reading-producing) fallen wearer.
- Release: watchapp 0.5.6. No protocol change.
