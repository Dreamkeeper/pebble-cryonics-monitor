# Delta: watch-phone-protocol — S4 sensor lab

## ADDED Requirements

### Requirement: Guided sensor lab streams raw HR telemetry on demand
The companion SHALL be able to start and stop a sensor-lab mode on the
watch (PMSG_HR_LAB). While active: the worker SHALL sample the raw HR
metric in burst mode and relay, every 2 s, the raw peek value, the
seconds since the last HealthService HR event, and its free heap to the
companion via the open watchapp; the detector core SHALL be held
silently (pre-alarm stages cancel, a latched ALARM survives, baselines
reset on release) so deliberate off-wrist test stages cannot start
ladders or nags; and the watchapp SHALL stay open (exempt from the
auto-launch guard) to relay samples, returning to the watchface when
the lab ends. The companion SHALL guide the wearer through the staged
wear conditions, record all samples, and produce a shareable per-stage
summary with the full sample log.

#### Scenario: Off-wrist lab stages raise no alarms
- **WHEN** the sensor lab is active and the watch lies on the table for
  minutes without pulse or motion
- **THEN** no check-in, hunt, nag, or alarm is emitted, and monitoring
  resumes with fresh baselines when the lab ends

#### Scenario: The lab yields a per-stage sensor characterization
- **WHEN** the wearer completes the guided stages
- **THEN** the companion stores and can share a summary showing, per
  stage, the sample count, nonzero-reading share, bpm range, and median
  event age — the data that decides the phantom-pulse question
