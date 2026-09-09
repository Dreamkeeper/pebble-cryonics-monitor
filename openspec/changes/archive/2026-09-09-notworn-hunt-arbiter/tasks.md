# Tasks

- [x] 1. Core: `hunt_purpose` + `notworn_hunt_next_ms`; `tick_notworn`
       starts a NOTWORN-tagged hunt, nags only on hunt timeout; a change
       during the hunt sets the 10 min cooldown; `tick_pulse` ignores
       hunts it did not start; teardown paths reset the purpose.
- [x] 2. Worker: flag bit 6 = last HR event quality-gated.
- [x] 3. App: nag hold expires after 180 s (watchface returns via the
       auto-launch guard).
- [x] 4. Companion: GATED/NAGGED/HUNT in the record log line; soak
       counter `notworn-nags`; version 0.6.2 (41).
- [x] 5. Tests: sleeping-flat scenario with a burst-aware sensor model
       (arbiter hunt, no nag, bounded hunts over 30 min, nightstand
       still nags once); removal-nag test updated. 192 checks.
- [x] 6. `tools/worker_log_timeline.py` reproduces the post-mortem from
       shared companion logs (no ADB needed).
- [x] 7. Builds: watchapp 0.5.4, companion 0.6.2; dist updated.
- [ ] 8. Owner verification: a night without a nag; soak report shows
       `notworn-nags=0`; a deliberate nightstand test still nags (after
       ~3 min 45 s) and the watch returns to the watchface 3 min later.
