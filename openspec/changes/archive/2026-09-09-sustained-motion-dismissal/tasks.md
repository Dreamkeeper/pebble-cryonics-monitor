# Tasks

- [x] 1. Core: sustained-motion window (3 motion-seconds within 10 s),
       `last_sustained_ms`, `note_motion` gates CHECKIN dismissal and
       hunt stand-down on it; baselines reset it.
- [x] 2. Core: ladder stillness gate and `removal_suspected` use the
       last sustained motion.
- [x] 3. Tests: bed-partner scenario (bumps every 15 s: hunt runs,
       CHECKIN, COUNTDOWN, ALARM), sustained motion still dismisses and
       still stands a hunt down; three single-jerk dismissals updated.
- [x] 4. Build watchapp 0.5.5, worker size checked, dist updated.
- [ ] 5. Owner verification: repeat the desk test — a bumped desk must
       now reach COUNTDOWN (cancel on the phone or watch); a shelf test
       still gives "Not worn?"; a night in bed without a spurious
       check-in cancel.
