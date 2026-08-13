# Escalation & Dead-Man Specification

## Purpose

Defines how a confirmed alarm reaches humans, and how the system notices
that the phone itself has gone dark. Implemented in
`server/app/escalation.py` and `server/app/deadman.py` (pure state
machines, injected clocks); scenario coverage in `server/tests/`.
Phone-direct fallback lives in the Android companion (`Escalator.kt`).

Settled product principles: humans first (the system never auto-dials
emergency services — the wearer gets a one-tap dialer; contacts get
location and context so THEY can call EMS), and acknowledged-and-retried
delivery, never fire-and-forget.

Alert kinds: watch_alarm (full escalation), phone_silent (advisory),
fault (wearer first), test (fire drill, tagged TEST).

## Requirements

### Requirement: Tiered fan-out with ACK-gated promotion
Escalation SHALL proceed in ordered tiers (default: relatives → CSO
standby team). Each tier fans out to every contact over all of that
contact's channels. If NO contact has acknowledged, the next tier SHALL
activate after promote_after_s (default 600 s, configurable per tier).
Unacknowledged contacts SHALL be re-sent every repeat_after_s (default
1800 s).

#### Scenario: Tier 1 fans out immediately
- **WHEN** a watch_alarm escalation starts
- **THEN** every Tier-1 contact receives one send per configured channel

#### Scenario: No acknowledgement promotes the next tier
- **WHEN** promote_after_s elapses with zero acknowledgements
- **THEN** the next tier's contacts start receiving sends

#### Scenario: First acknowledgement stops promotion but not repeats
- **WHEN** one contact acknowledges
- **THEN** no further tiers are activated
- **AND** other unacknowledged contacts continue receiving repeats
- **AND** duplicate ACKs from the same contact are idempotent

#### Scenario: Resolution silences everything
- **WHEN** the escalation is resolved ("false_alarm" by wearer cancel, or
  "handled" by a responder)
- **THEN** no further sends occur for that escalation

### Requirement: Every outbound message carries a one-tap ACK
Each send SHALL include a unique acknowledgement affordance (Telegram
inline button, signed ACK URL in email/ntfy) that records which contact
acknowledged and when.

#### Scenario: ACK link records the contact
- **WHEN** a contact taps their ACK link/button
- **THEN** the server marks that contact acknowledged for that
  escalation and logs the event

### Requirement: Server-primary with phone-direct fallback
The companion SHALL post alarms to the server, which owns tiered
escalation over telegram/email/ntfy. Phone-direct SMS fires redundantly
regardless (it reaches contacts without data connectivity); phone-direct
Telegram fires only when the server is unreachable. Wearer cancellation
SHALL retract everywhere: server resolve plus retraction messages on any
phone-direct channel that fired.

#### Scenario: Server down does not lose the alarm
- **WHEN** an ALARM arrives from the watch and the server is unreachable
- **THEN** the companion escalates phone-direct (SMS + Telegram) and
  marks the server unreachable for the mutual watchdog

### Requirement: Graduated dead-man response to phone silence
With phone heartbeats expected every heartbeat_interval_s (default 300),
the server SHALL mark LATE after late_after_missed intervals (default 2;
quiet re-ping, no human contact) and SILENT after silent_after_s (default
1800), raising a phone_silent ADVISORY escalation — explicitly
distinguished in wording from a confirmed watch alarm.

#### Scenario: Missed heartbeats escalate gradually
- **WHEN** the phone misses two heartbeat intervals
- **THEN** the state is LATE and no contact is notified
- **AND** when silent_after_s elapses the state is SILENT and Tier 1
  receives an advisory

#### Scenario: A never-registered phone is not an emergency
- **WHEN** the server has never received a heartbeat (fresh install)
- **THEN** the dead-man state is OK, not SILENT

### Requirement: Legitimate offline situations soften the dead-man
A low-battery warning from the phone SHALL extend the SILENT threshold by
low_battery_extra_s (default 3600), cleared when a heartbeat shows
battery above 20%. A wearer-declared offline window (airplane, subway)
SHALL suppress dead-man escalation until the window ends, after which the
full silence grace restarts from the window end.

#### Scenario: Dying battery buys grace
- **WHEN** the phone sends low_battery_warning and then goes dark
- **THEN** SILENT is deferred by low_battery_extra_s beyond the normal
  threshold

#### Scenario: Declared offline window suppresses escalation
- **WHEN** an offline window is declared for N seconds
- **THEN** no dead-man escalation occurs during the window
- **AND** after it ends the phone has the full normal grace before
  SILENT

### Requirement: Mutual watchdog
The phone SHALL warn the wearer when the server becomes unreachable (and
switch to phone-direct escalation); the server SHALL escalate when the
phone goes silent. When configured (planned, M4), secondary liveness
signals (OwnTracks/Dawarich last location update) SHALL be consulted
before raising phone_silent.

#### Scenario: Server outage is surfaced to the wearer
- **WHEN** server heartbeats start failing
- **THEN** the wearer receives a FAULT notification exactly once per
  outage, and recovery is logged

### Requirement: Endpoints are exposed according to what they reveal
Any endpoint returning wearer data (locations, contact identities, event
log) SHALL require the API token. A liveness endpoint carrying no wearer
data MAY be unauthenticated so uptime monitors and health checks work on
a publicly reachable deployment. Contact acknowledgement links SHALL
authenticate by unguessable per-send token rather than the API token,
because contacts have no credentials.

#### Scenario: Public health probe leaks nothing
- **WHEN** an unauthenticated client requests the health endpoint
- **THEN** it receives only service liveness fields
- **AND** the same client is refused (401) on the status endpoint

### Requirement: Fire drills exercise the real chain
A TEST alarm SHALL traverse the identical code path as watch_alarm —
tiers, channels, ACK tracking — with every message visibly tagged TEST.

#### Scenario: Fire drill reaches contacts with TEST tagging
- **WHEN** the wearer triggers the fire-drill action
- **THEN** contacts receive normally-routed messages prefixed TEST and
  ACKs are tracked as usual
