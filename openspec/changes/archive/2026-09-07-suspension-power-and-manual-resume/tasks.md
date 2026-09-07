# Tasks

- [x] 1. Protocol: `CM_HR_PERIOD_HOLD_S = 300` with rationale.
- [x] 2. Worker: idle cadence derived from `suspended || charging`;
       re-applied on SUSPEND_STARTED / SUSPEND_EXPIRED / AUTO_RESUMED /
       CHARGING_STARTED / CHARGING_ENDED.
- [x] 3. Core: `cm_user_ok` resumes any suspension (after the ALARM
       branch), reschedules the check-in.
- [x] 4. App: hint text "SELECT check-in/resume".
- [x] 5. Tests: `test_manual_resume_by_select` — auto-resume mode,
       carry mode, alarm-latch precedence, no instant triggers after
       resume (178 checks, 0 failures).
- [x] 6. Build watchapp 0.5.3 (worker text 5,865 B, heap budget
       intact), dist updated.
- [ ] 7. Owner verification: LED cadence visibly ~5 min while
       suspended; SELECT resumes and the phone shows monitoring
       within seconds; S6 drain trend after a few days.
