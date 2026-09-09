# Not-worn nag arbitrates with the 1 Hz hunt before firing

## Why

Field event 2026-09-09 02:05:56 (companion daily log, per-minute
worker records): the wearer was asleep, 16 min without motion, raw
bpm 66/67/67/67/67 at the 60 s idle cadence. At the third identical
sample the not-worn rule ("no bpm *value change* for 3 min and no
motion for 3 min") fired "Not worn?", vibrated the wearer awake, and
kept the watchapp on screen until 07:07 (5 h; the phone pushed two
settings messages a minute the whole time).

Root cause: the not-worn nag is the only detector that skips the
1 Hz hunt. The pulse ladder treats a flat value as *ambiguous* and
hunts silently before doing anything; the nag treated three identical
60 s samples as proof of an off-wrist watch. At 60 s cadence a
sleeping wearer's steady integer bpm repeats exactly like a nightstand
watch's frozen reading does. The liveness premise ("a living wearer's
raw bpm always jitters", S4) is true at 1 Hz, not at one sample per
minute.

## What changes

- **Hunt before nag.** When the flat-and-still condition is met, the
  core starts the silent 45 s burst hunt (detector NOTWORN) instead
  of nagging. A bpm change during the hunt ends it silently, clears
  the episode and starts a 10 min cooldown before the next not-worn
  hunt. If the 1 Hz stream stays flat or absent for the whole hunt,
  the nag fires as before — 45 s later than it used to.
- **Ladder unaffected.** A not-worn hunt never escalates into
  CHECKIN; the ladder's own flat/lost triggers and the removal
  classification are unchanged.
- **Nag screen expiry.** The watchapp returns to the watchface 3 min
  after an unanswered nag (the phone notification remains). A nag
  must never squat the screen all night.
- **Diagnosability.** Heartbeat record flags gain bit 6 = "last HR
  event was zeroed by the quality gate"; the companion prints GATED /
  NAGGED / HUNT on its record lines; the soak report counts not-worn
  nags (`notworn-nags=`); `tools/worker_log_timeline.py` rebuilds
  the true per-minute timeline from shared companion logs, so the
  next post-mortem needs no ADB.

## Out of scope

Night-time threshold profiles; changing the quality gate; the
ladder's flat threshold (300 s) — a steady sleeper already gets a
silent ladder hunt at 5 min, which is the same arbiter and was never
the problem.

## Impact

- Specs: `detector-ladder` (MODIFIED "Not-worn nag never escalates
  to contacts").
- Code: `watchapp/src/core/detectors.{h,c}` (hunt_purpose,
  notworn_hunt_next_ms, tick_notworn, tick_pulse guard),
  `watchapp/worker_src/c/worker.c` (gated flag bit),
  `watchapp/src/c/main.c` (nag hold expiry), companion
  `DataLogReceiver`/`SoakStats`/`MonitorService`/`DebugActivity`.
- Tests: `test_sleeping_flat_bpm_hunts_before_nag` (+14 checks),
  `test_frozen_pulse_removal_nags` updated (one arbiter hunt is now
  expected before the nag). 192 checks.
- Release: watchapp 0.5.4, companion 0.6.2. Record format unchanged
  (a previously unused flag bit is now meaningful).
