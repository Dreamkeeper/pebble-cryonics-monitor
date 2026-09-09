# Design

## D1. Where "sustained" is computed

`note_motion` already runs once per jerk; it now counts at most one
motion-second per second (the existing `motion_this_second` flag,
reset by `cm_tick`) inside a 10 s window: the window restarts when
the previous one is older than 10 s, and the third motion-second in a
window stamps `last_sustained_ms` and performs the ladder actions
(CHECKIN dismissal, hunt stand-down). Continuous motion keeps stamping
it every second after the third. Cost: two 32-bit and one 8-bit
runtime fields, no persisted state, no config change.

## D2. Which consumers switch to sustained motion

Ladder only: CHECKIN auto-dismissal, hunt stand-down, the ladder's
stillness gate (`pulse_still_s`) and `removal_suspected`. Everything
that treats micro-movement as liveness stays on raw motion — the
non-motion detector, the not-worn and sensor-fault nags, suspension
auto-resume (already requires 15 consecutive seconds), the post-impact
settle window. Rationale: a bump proves someone or something moved the
surface; it does not prove the wearer is alive, so it must not cancel
an escalation — but it is still micro-movement evidence for the
long-window detectors, whose thresholds are minutes, not seconds.

## D3. Removal classification

`removal_suspected` compares the last *sustained* motion with the last
value change. Taking a watch off is seconds of handling and still
classifies as removal (test_frozen_pulse_removal_nags feeds 10 s).
A single jerk at the freeze moment — the collapse case the spec listed
as a residual risk — no longer diverts the episode to the nag.

## D4. Not changed on purpose

The impact detector's settle window ("motion after a fall = the wearer
got up") stays on raw motion: after a fall, even a small movement is
evidence worth honouring, and the impact ladder has its own 60 s
immobility requirement before CHECKIN.
