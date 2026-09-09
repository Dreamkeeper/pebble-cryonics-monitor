# Tasks

- [x] 1. Core: 1.5 s post-vibration guard in `cm_accel_feed` (motion,
       freefall, impact, shock all excluded while ringing).
- [x] 2. Core: `tick_impact` discards an impact with no valid reading
       since the shock (HR hardware, previously worn).
- [x] 3. Tests: own vibration is not motion; impact CHECKIN survives
       its buzzes on a desk and reaches COUNTDOWN; impact on an unworn
       watch is silent; impact ladder tests feed a worn wearer.
- [x] 4. Build watchapp 0.5.6, worker size checked, dist updated.
- [ ] 5. Owner verification: set the watch down briskly on the desk —
       no impact check-in; a deliberate impact test while worn (drop
       the wrist onto a cushion, stay still) reaches COUNTDOWN despite
       buzzing on a hard surface; a night in bed without a self-cancel.
