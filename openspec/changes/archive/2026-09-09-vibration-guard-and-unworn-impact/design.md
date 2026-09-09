# Design

## D1. Where the guard lives

In `cm_accel_feed`, the only place samples are interpreted. A flagged
sample resets the jerk baseline (as before) and stamps
`vibe_guard_until_ms = now + 1500 ms`. While the stamp is in the
future, unflagged samples still update the baseline but produce
neither motion nor freefall/impact/shock candidates. Batches arrive
once per second with one timestamp, so the guard reliably covers the
remainder of the buzz batch and the following one. Cost: one runtime
32-bit field, no config change.

## D2. Why not shorten the check-in vibration instead

The app owns the buzz cadence, the core owns motion; coupling them
through a message would add protocol and still leave every other
vibration (nags, suspension feedback, phone-triggered patterns) able
to fake motion. Filtering at the source fixes the whole class.

## D3. Unworn impact

`tick_impact` already discards an impact when the pulse has been flat
for pulse_flat_after_s (a watch long off the wrist). The new check
runs at the immobility deadline: if `last_pulse_ms` precedes
`impact_ms`, no valid reading arrived during settle + immobility
(≥ 65 s, i.e. more than one idle-cadence sample period), so the watch
is not on a wrist that the sensor can read. Gated readings (quality
below Acceptable) count as absent, consistent with liveness.

Accepted limitation: a fall that knocks the sensor off the skin gets no
impact check-in; the pulse ladder's absent-reading trigger (150 s) and
the not-worn nag cover it, later than the impact fuse would have.

## D4. Tests model the wearer, not the table

The impact ladder tests fed no readings during immobility and passed
only because nothing checked for them. They now feed a jittering pulse
(`secs_still_worn`), and a dedicated test feeds buzz-plus-aftershock
seconds through a live CHECKIN to prove it reaches COUNTDOWN.
