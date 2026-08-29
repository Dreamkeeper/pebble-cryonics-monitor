# Delta: detector-ladder — episode identity and clock-jump immunity

## ADDED Requirements

### Requirement: Ladder episodes have durable identity
The detector core SHALL assign a 16-bit episode ID (shell-persisted
sequence, 0 reserved) when a ladder episode starts, carried in every
ladder-related action it emits (CHECKIN_START, COUNTDOWN_START,
ALARM, ALERT_CANCELLED). One episode spans first stage to resolution;
a worker restart voids the old episode and any later one gets a new
ID.

#### Scenario: Cancel targets the right episode
- **WHEN** an episode is cancelled and a new episode starts seconds
  later
- **THEN** the two carry different IDs and downstream consumers can
  never conflate them

### Requirement: Detector timing is immune to wall-clock corrections
Detector and ladder intervals SHALL be driven by a monotonic
millisecond clock supplied by the shell (worker tick stream), never
by raw wall-clock time. Wall clock is used only for display, record
epochs, and persisted suspension deadlines, converted at the shell
boundary. A wall-clock correction (phone time sync, DST) MUST NOT
shorten an active countdown, lengthen or shorten any detector window,
or fire any detector.

#### Scenario: Time sync during a countdown
- **WHEN** the watch clock jumps +2 h while a 30 s countdown runs
- **THEN** the countdown still takes its full 30 s and no other
  detector fires from the jump

#### Scenario: Suspension keeps wall-clock semantics
- **WHEN** a 30-min suspension is active across a time correction
- **THEN** it ends 30 wall-clock minutes after it began (converted at
  restore; documented choice)
