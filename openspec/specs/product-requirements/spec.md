# Product Requirements Specification

## Purpose

The top-level product truth for the Pebble Cryonics Monitor, written down
from the owner's initial brief (2026-08-05) and the two decision rounds
that refined it. Mechanism-level behavior lives in the sibling specs
(`detector-ladder`, `watch-phone-protocol`, `escalation-and-deadman`,
`suspension`); this spec holds the requirements that outrank them —
mission, scope, platforms, and the safety principles every future change
must respect.

Initial-scope items accepted but **not yet delivered**, tracked here so
they surface as future changes rather than silently expiring: learning
mode (2-week shadow observation, pattern-mined suggestions), server
outbound channel delivery + persistent alarm state, automated voice-call
fallback, gateway-phone role (spare Android as SMS/call gateway),
SIP/telephony plugin, SMS heartbeat for 2G-only connectivity,
OwnTracks/Dawarich as secondary liveness signals, persistent-foreground
watchface mode (YaForecasWatch2 merge), Pebble Round 2 round-layout UI,
community onboarding flow, and Rebble appstore submission.

## Requirements

### Requirement: Detect deanimation and incapacitation, then alert humans
The system SHALL detect, within the limits of wrist-worn sensors, that
the wearer may have suffered cardiac arrest or an incapacitating
accident (fall, vehicle crash), and SHALL alert the wearer's relatives
and cryonics organization with location and context. Detection uses the
accelerometer and, where present, the heart-rate sensor, fused — neither
signal alone triggers contact-level escalation.

#### Scenario: The core promise
- **WHEN** the wearer becomes unresponsive and does not cancel the
  multi-stage confirmation ladder
- **THEN** relatives and the cryonics organization receive an alarm with
  the wearer's last known location, over more than one channel

### Requirement: Humans decide about emergency services
The initial brief said "alert emergency services"; the settled decision
(round 1) is humans-first: the system SHALL NOT auto-dial EMS anywhere.
It gives the wearer a one-tap dialer and gives contacts the location and
context they need to call EMS themselves. This is a standing safety
policy, not an implementation gap.

#### Scenario: No autonomous EMS contact
- **WHEN** any alarm escalates fully with no human response
- **THEN** the system continues re-alerting humans per the escalation
  spec and never places a call to 911/112 itself

### Requirement: Pebble platform coverage with honest degradation
Pebble Time 2 (emery) is the primary target. Pebble 2 HR (diorite),
Pebble 2 Duo (flint), and Pebble Round 2 (gabbro) SHALL be supported by
the same .pbw. Watches without a heart-rate sensor (flint, gabbro) get
the motion-only detector set; pulse-loss detection SHALL be gated on
hardware capability, never assumed.

#### Scenario: No-HR hardware still protects
- **WHEN** the watchapp runs on a Pebble 2 Duo or Round 2
- **THEN** impact, non-motion, check-in, and SOS work, pulse-loss is
  unavailable, and nothing misrepresents pulse monitoring as active

### Requirement: Android-only companion for now
The phone companion SHALL target Android only. iOS is an explicit
non-goal until the owner widens scope; nothing in the watchapp or server
may hard-depend on Android-only behavior in a way that would preclude a
future iOS companion (the protocol spec is the boundary).

#### Scenario: Scope stays honest
- **WHEN** a proposed change requires a phone-side capability
- **THEN** it is designed against the Android companion, and any
  Android-specific dependency is confined behind the watch-phone
  protocol

### Requirement: Monitoring must not claim the watch
Monitoring SHALL run in the Pebble background worker so the wearer keeps
their chosen watchface; only events requiring immediate wearer action
may take the screen. The worker's single-slot eviction risk SHALL be
watchdogged from the phone. (A persistent-foreground watchface mode
remains a planned opt-in alternative, not the default.)

#### Scenario: The watchface stays the wearer's
- **WHEN** monitoring is active and no alert needs attention
- **THEN** the wearer's watchface is displayed, not this app

### Requirement: Self-hosted server, owner-controlled
The optional server SHALL be deployable on infrastructure the user
controls (NAS, VPS, home server) via Docker Compose, with no dependency
on a vendor cloud for alarm-path function. The phone SHALL remain a
functional (degraded) alarm path when the server is unreachable.

#### Scenario: Vendor-cloud outage is survivable
- **WHEN** the self-hosted server is down or unreachable
- **THEN** watch-to-phone detection and phone-direct escalation still
  operate, and the wearer is told the server tier is out

### Requirement: Deterministic alarm path; learning suggests, user approves
Everything that can trigger, suppress, or delay an alarm SHALL be
deterministic, auditable configuration. The learning layer (planned:
~2-week shadow observation, then pattern-mined suggestions such as
recurring suspension windows) and any LLM integration SHALL only produce
suggestions the user explicitly approves, and SHALL NOT sit in the
runtime alarm path.

#### Scenario: No model ever gates an alarm
- **WHEN** an alarm ladder is running
- **THEN** its progression depends only on sensor data, timers, and
  user-approved configuration — never on a model's runtime judgment

### Requirement: Every detector is optional and configurable
Each detector SHALL be individually enable-able and its thresholds
user-configurable; attention-demanding features (scheduled check-in)
default off, passive protection defaults on.

#### Scenario: The wearer shapes the monitor
- **WHEN** a wearer disables a detector or adjusts its thresholds
- **THEN** the change takes effect without rebuilding, and defaults suit
  a wearer who configured nothing

### Requirement: Community release under GPL-3.0
The project SHALL remain GPL-3.0 (inheriting from its OpenSeizureDetector
lineage) and is intended for release to the cryonics community:
watchapp via the Rebble appstore, companion via APK sideload (full
functionality) and optionally Play Store (policy-constrained flavor).
Release-readiness includes onboarding (permissions, battery exemptions,
contact opt-in) suitable for non-technical wearers.

#### Scenario: A stranger can adopt it
- **WHEN** a cryonicist who has never seen this chat installs the
  released artifacts
- **THEN** setup succeeds from documentation alone, with no secret or
  hardcoded dependency on the original owner's infrastructure

### Requirement: Positioned as a personal alarm, never a medical device
All naming, documentation, and store listings SHALL present the system
as a personal alarm / unresponsiveness detector. Claims of cardiac
arrest detection or medical-grade monitoring are prohibited; the PPG
ambiguity (no signal ≈ loose strap) SHALL be documented wherever
detection quality is described.

#### Scenario: Marketing stays inside the line
- **WHEN** any user-facing text describes pulse-loss detection
- **THEN** it describes loss of pulse *signal* with its known failure
  modes, and never promises detection of cardiac arrest
