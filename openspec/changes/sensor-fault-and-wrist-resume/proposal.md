# Sensor-fault nag + wrist-evidence auto-resume

## Why

Two field findings from the 2026-08-29 night (soak day 0):

1. **A dead HR sensor while worn reads as "Not worn?".** The HRM
   failed to start on 2 of 3 consecutive boots; the wearer — watch on
   wrist, moving — got an off-wrist nag. The evidence actually says
   *"no pulse signal but motion continues"*: either the sensor died
   while worn or the watch is being carried off-wrist. Both mean pulse
   monitoring is blind; neither is what "Not worn?" implies, and the
   wearer's correct response (reboot the watch / suspend) is different
   from re-wearing it.
2. **Motion-only auto-resume breaks the carried-watch case (owner
   decision 2026-08-29).** A suspended watch carried in a bag or
   pocket moves for minutes at a time — and being carried off-wrist
   is a *valid* reason to be suspended. The original "pulse never
   resumes" rule predates the S4 liveness finding (liveness = a
   CHANGING bpm; frozen/zero values prove nothing) and the off-wrist
   firmware fix. With those, requiring motion AND a live pulse is
   strictly better: a bag ride cannot fake a changing bpm, and a
   re-worn wrist produces one within ~30 s of warm-up.

## What changes

- **New detector `CM_DET_SENSOR` (nag-only):** fires once per episode
  when the bpm value has not changed for `sensor_fault_after_min`
  (default 10) while motion stays recent — the moving-wearer twin of
  the not-worn nag (which keeps requiring BOTH stale). Watch shows
  "No pulse signal" with reboot/suspend guidance; phone raises a
  FAULT notification (new `PMSG_SENSOR_FAULT`); contacts are never
  involved. A bpm change re-arms it.
- **Auto-resume needs wrist evidence on HR hardware:** sustained
  motion (unchanged: 15 s past the 60 s grace) AND a bpm change that
  happened during this suspension (past the grace) and within
  `resume_pulse_fresh_s` (default 150 s). Non-HR hardware keeps
  motion-only resume. The suspend timer remains the backstop when the
  sensor is dead (and the sensor-fault nag then fires after resume).
- Companion counts sensor faults in the soak card.

## Impact

- Affected specs: `detector-ladder` (ADDED requirement),
  `suspension` (MODIFIED auto-resume requirement)
- Affected code: `watchapp/src/core/detectors.{h,c}` (+ host tests),
  `watchapp/src/core/protocol.h`, worker + app shells, companion
  (`Protocol.kt`, `MonitorService.kt`, `SoakStats.kt`, Debug soak card)
- Versions: watchapp 0.4.8, companion 0.5.2 (31)
- Persisted watch config blob changes size → old blob is discarded by
  the existing size check; defaults apply on first boot (safe).
