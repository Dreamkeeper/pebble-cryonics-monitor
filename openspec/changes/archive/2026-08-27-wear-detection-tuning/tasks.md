# Tasks: wear-detection-tuning

## 1. Detector core

- [x] 1.1 Config: pulse_proof_min (5), removal_window_s (45),
      resume_grace_s (60); notworn_after_min default 15 → 3
- [x] 1.2 Non-motion: pulse-proof-of-life gate on HR hardware
- [x] 1.3 Pulse loss: removal_suspected() classification before a hunt;
      not-worn owns removal episodes; nag waits out a running hunt
- [x] 1.4 Auto-resume: motion-only + arming grace; pulse path removed
- [x] 1.5 Suspension minutes round up in the status push

## 2. Verification

- [x] 2.1 Test suite updated: still+pulse silence, stale-pulse
      backstop, removal→nag, grace blocks instant resume, phantom
      pulse never resumes (80 → 105 checks, green)
- [x] 2.2 Clean pbw build
- [x] 2.3 Owner on-watch validation: T3b nag at ~3 min off-wrist;
      T4 suspension survives the first minute and resumes on a
      15 s walk after the grace; still+pulse evening stays silent

## 3. Wrap-up

- [x] 3.1 Archive after owner validation (updates detector-ladder and
      suspension living specs)
