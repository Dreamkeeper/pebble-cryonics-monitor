# Delta: detector-ladder — sensor-fault nag (no pulse signal, motion continues)

## ADDED Requirements

### Requirement: Sensor-fault nag
On HR hardware, when the bpm value has not changed for
sensor_fault_after_min (default 10) while motion remains recent
(within the not-worn threshold), the watch SHALL nag the wearer once
per episode with a sensor-fault message ("no pulse signal — sensor
dead, or watch carried off-wrist; reboot or suspend") and notify the
phone (`PMSG_SENSOR_FAULT`), which SHALL raise a FAULT notification
and count the event in the soak counters. Contacts are never alerted.
The episode re-arms on a bpm change or a baseline reset
(charging/lab-hold release, suspension end). Motion alone MUST NOT
re-arm it — motion does not disprove a dead sensor. The not-worn nag
keeps requiring BOTH pulse and motion stale; the two nags are
mutually exclusive by construction (motion recency splits them).

#### Scenario: Sensor dies while worn
- **WHEN** the wearer keeps moving normally but the bpm value stays
  frozen (or absent) beyond sensor_fault_after_min
- **THEN** the watch shows the sensor-fault nag (not "Not worn?"),
  the phone shows a FAULT with reboot/suspend guidance, and no
  pre-alarm or contact escalation occurs

#### Scenario: One nag per episode
- **WHEN** the condition persists after the nag
- **THEN** no further sensor-fault nags fire until a bpm change (or
  baseline reset) re-arms the detector

#### Scenario: Still wearer stays with the pulse ladder
- **WHEN** the pulse signal is absent and the wearer is also still
  beyond the stillness threshold
- **THEN** the pulse-loss hunt/ladder (or the not-worn nag, per the
  removal signature) owns the episode — the sensor-fault nag stays
  silent during hunts and active stages
