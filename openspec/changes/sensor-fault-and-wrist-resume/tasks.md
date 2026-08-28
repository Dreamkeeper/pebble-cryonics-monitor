# Tasks

- [x] 1. Core: CM_DET_SENSOR + CM_ACT_SENSOR_FAULT + sensor_nagged
       state + sensor_fault_after_min cfg; tick_sensorfault; re-arm on
       bpm change and baseline resets (not on motion).
- [x] 2. Core: auto-resume pulse conjunction (resume_pulse_fresh_s,
       change-after-grace check); non-HR hardware unchanged.
- [x] 3. Host tests: sensor-fault fire/once/re-arm/exclusions;
       bag-carry no-resume; motion+pulse resume; grace still blocks;
       update tests broken by the new rule. All green.
- [x] 4. Shell: PMSG_SENSOR_FAULT=16; worker launches app on the
       action; app shows "No pulse signal" guidance + forwards to
       phone.
- [x] 5. Companion 0.5.2 (31): handle PMSG_SENSOR_FAULT (FAULT
       notification + soak counter + soak-card line); build release
       APK to dist.
- [x] 6. Watchapp 0.4.8: build pbw to dist.
- [ ] 7. Owner hardware verification: sensor-fault nag wording on a
       failed-HR boot; bag-carry suspension does not auto-resume;
       re-wear resumes within ~1 min.
- [x] 8. Carry mode (owner follow-up 2026-08-29): long-press UP =
       timer-only 120 min suspension — hand-carry puts real skin on
       the sensor, indistinguishable from wear; watchapp 0.4.9.
