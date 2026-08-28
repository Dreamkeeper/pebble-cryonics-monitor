# Delta: suspension — auto-resume requires wrist evidence, not just motion

## MODIFIED Requirements

### Requirement: Optional auto-resume on wear signals
When auto-resume is enabled (default on), monitoring SHALL resume
before expiry only on combined wrist evidence: resume_motion_s
consecutive seconds of motion (default 15), evaluated after an arming
grace of resume_grace_s (default 60 s) from the suspension start,
AND — on HR hardware — a live pulse: a bpm VALUE CHANGE recorded
during this suspension after the grace, no older than
resume_pulse_fresh_s (default 150 s) when the motion run completes.
Motion alone MUST NOT resume on HR hardware: a suspended watch
carried in a bag or pocket moves for minutes at a time, and being
carried off-wrist is a valid reason to be suspended (owner decision
2026-08-29). Liveness is a CHANGING value (S4): the patched firmware
reads 0 off-body and a frozen reading never changes, so a bag ride
cannot fake the pulse half. On hardware without an HR sensor,
sustained motion alone still resumes (documented limitation). During
the grace period all signals are ignored — the wearer is usually
still wearing or handling the watch when the suspension begins. If
the sensor is dead, auto-resume never fires and the suspend timer
remains the backstop; the sensor-fault nag then covers the blind
window after expiry.

#### Scenario: Putting the watch back on resumes early
- **WHEN** suspension is active with auto-resume enabled and the grace
  period has passed
- **AND** the wearer wears the watch with sustained motion and the HR
  sensor produces changing readings (~30 s warm-up)
- **THEN** AUTO_RESUMED is emitted and detectors re-arm

#### Scenario: Suspending while still wearing does not instantly resume
- **WHEN** the wearer starts a suspension without taking the watch off
  and keeps moving
- **THEN** no resume occurs within the grace period

#### Scenario: Stale pulse does not resume
- **WHEN** a suspended watch lies against a surface and the HR sensor
  produces readings (phantom or stale)
- **THEN** the suspension continues; readings without sustained motion
  never resume it

#### Scenario: Carried in a bag stays suspended
- **WHEN** a suspended watch rides in a bag or pocket with sustained
  motion but no changing bpm
- **THEN** the suspension continues to its timer; motion alone is not
  wrist evidence on HR hardware
