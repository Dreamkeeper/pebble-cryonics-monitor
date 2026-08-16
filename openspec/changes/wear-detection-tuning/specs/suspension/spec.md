# Delta: suspension — trustworthy auto-resume

## MODIFIED Requirements

### Requirement: Optional auto-resume on wear signals
When auto-resume is enabled (default on), monitoring SHALL resume
before expiry only on sustained motion: resume_motion_s consecutive
seconds (default 15), evaluated after an arming grace of resume_grace_s
(default 60 s) from the suspension start. During the grace period all
signals are ignored — the wearer is usually still wearing or handling
the watch when the suspension begins. Pulse readings SHALL NOT resume a
suspension at any point: the optical sensor phantom-reads when pressed
against a surface, so the accelerometer is the only trusted wear
signal.

#### Scenario: Putting the watch back on resumes early
- **WHEN** suspension is active with auto-resume enabled and the grace
  period has passed
- **AND** the wearer wears the watch with sustained motion
- **THEN** AUTO_RESUMED is emitted and detectors re-arm

#### Scenario: Suspending while still wearing does not instantly resume
- **WHEN** the wearer starts a suspension without taking the watch off
  and keeps moving
- **THEN** no resume occurs within the grace period

#### Scenario: Stale pulse does not resume
- **WHEN** a suspended watch lies against a surface and the HR sensor
  produces readings (phantom or stale)
- **THEN** the suspension continues; only sustained motion resumes it
