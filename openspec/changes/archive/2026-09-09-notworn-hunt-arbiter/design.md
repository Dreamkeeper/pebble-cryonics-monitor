# Design

## D1. One hunt mechanism, two purposes

The core already owns a single hunt (`pulse_phase`, `hunt_start_ms`,
HR_BURST_ON/OFF) used by the pulse ladder. Rather than a second
machine, a `hunt_purpose` byte tags who started it (0 = ladder,
1 = not-worn). Every teardown path (`end_pulse_machinery`, detector
hold) resets it. The ladder's phase-1 conclusion ignores hunts it did
not start, so a not-worn hunt can never turn into CHECKIN; the
not-worn tick concludes its own hunt and nags only if it timed out.
Motion during either hunt stands it down silently (existing rule).
`tick_sensorfault` and the not-worn tick already wait for any hunt
to finish, so the two nags stay mutually exclusive.

## D2. Cooldown after a confirming hunt

A steady sleeper would otherwise be re-hunted every ~4 min (the 3 min
flat window restarts at the confirming change). A change seen during
a not-worn hunt sets `notworn_hunt_next_ms = now + 10 min`; the
not-worn condition is not even evaluated before then. The ladder's
5 min flat trigger still runs its own silent hunt in between, so a
sleeping wearer sees at most ~one short burst per 5 min, each ending
within seconds at 1 Hz. Cost measured against last night: a burst is
~2 s of LED versus the 60 s cadence's 10–20 s per sample.

The cooldown is a compile-time constant (`CM_NOTWORN_HUNT_COOLDOWN_MS`)
on purpose: `cm_config` is persisted with a strict size check and
pushed by the phone as a sized blob; adding a field would silently
invalidate stored and pushed configs.

## D3. Delay for a real nightstand watch

Previously the nag fired at exactly `notworn_after_min`; now it fires
after that plus `pulse_hunt_s` (45 s). The nag is informational and
never reaches contacts; 45 s is acceptable. During the hunt the sensor
bursts for 45 s on a table — once per episode, then silence until
motion or a pulse re-arms the episode.

## D4. Nag screen expiry

`s_nag_hold` now carries a tick budget (180 s). When it expires the
hold clears and the existing auto-launch guard returns the watchface
on the next status poll. The phone-side notification and the worker's
one-per-episode nag state are unaffected; the wearer loses nothing
except a screen they were not looking at.

## D5. Diagnosability

Record flag bit 6 = "last HR event was zeroed by the quality gate".
Without it, "bpm=0" in a record cannot be told apart from "no reading
at all" — last night's diagnosis had to infer it from context. The
companion also names NAGGED and HUNT bits in its log line, counts
not-worn nags in the soak report, and ships a timeline tool that turns
a shared log into the same view used for this post-mortem.
