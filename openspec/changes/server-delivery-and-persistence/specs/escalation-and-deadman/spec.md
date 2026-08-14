# Delta: escalation-and-deadman — real delivery, receivable ACKs, durable multi-wearer state

## ADDED Requirements

### Requirement: Due sends are delivered through configured channels
When the escalation engine declares a `Send` due, the server SHALL
deliver it through the contact's configured channels: Telegram (bot
message with an inline acknowledge button), ntfy (push notification with
an acknowledge action), and email (message with an acknowledge link). A
send counts as delivered only when the transport accepts it; transport
failures SHALL NOT be silently dropped — the send is retried on the
tier's existing repeat cadence (default 1800 s, configurable per tier)
and the failure is visible in the status payload and the log.

#### Scenario: An alarm reaches a real contact
- **WHEN** an escalation makes a send due for a contact configured with
  Telegram
- **THEN** that contact's Telegram receives a message containing the
  wearer's name, alert kind, detector, location link when available,
  and an inline acknowledge button — TEST-tagged when the alert kind is
  test

#### Scenario: A down transport does not lose the alert
- **WHEN** a channel transport rejects or times out
- **THEN** the failure is recorded and visible in status, other channels
  for the same contact still fire, and the failed channel is retried on
  the repeat cadence

### Requirement: Alarm and dead-man state survive restarts
Escalation state (per-contact sends, acknowledgements, resolution), ACK
tokens, the event log, and every wearer's dead-man baseline SHALL be
persisted to the server's data volume. On startup the server SHALL
restore unresolved escalations and continue their repeat/promotion
timers with elapsed downtime counted (a restart must never reset an
escalation's clocks or resurrect a resolved one). Delivery is
at-least-once across a crash: in the worst case a send accepted just
before a crash may be repeated, and any duplicate SHALL carry the same
escalation identifier.

#### Scenario: Restart during an active escalation
- **WHEN** the server restarts while an unacknowledged escalation is
  active
- **THEN** after startup the escalation is still active, prior
  acknowledgements are still honored, previously issued ACK links still
  work, and the next repeat/promotion happens no later than it would
  have without the restart

#### Scenario: Dead-man survives the restart honestly
- **WHEN** the server restarts after some downtime
- **THEN** each wearer's last-heartbeat baseline is restored from disk
  and the downtime counts toward that wearer's phone silence rather
  than resetting it

### Requirement: Escalation and dead-man state are per-wearer and isolated
Every escalation, dead-man baseline, contact list, tier definition, and
event-log entry SHALL belong to exactly one wearer. One wearer's alarm,
silence, suspension, or configuration SHALL never affect another
wearer's escalation behavior, and no API response authenticated with one
wearer's token may contain another wearer's data. Contacts and tiers
come from the store (managed via the wearer-management API); a wearer
whose configuration has no deliverable contact SHALL be individually
DEGRADED — visible in authenticated status with the wearer named, and on
the public health endpoint only as a count, never a name.

#### Scenario: Two wearers, one alarm
- **WHEN** wearer A's ladder exhausts while wearer B is quiet
- **THEN** only A's tiers escalate, B's contacts receive nothing, and
  B's status is unchanged

#### Scenario: Unconfigured wearer is loud about it, privately
- **WHEN** a wearer has no contact with at least one working channel
  address
- **THEN** authenticated status marks that wearer DEGRADED with a
  reason, public health exposes at most a degraded count, and no
  escalation for that wearer pretends to fan out to nobody

## MODIFIED Requirements

### Requirement: Every outbound message carries a one-tap ACK
Each send SHALL include a unique acknowledgement affordance (Telegram
inline button, signed ACK URL in email/ntfy) that records which contact
acknowledged and when. Acknowledgements SHALL be receivable end to end:
Telegram button presses arrive via bot long-polling (no inbound webhook,
so the public surface does not grow) and are answered so the contact
sees confirmation; ntfy and email ACK links resolve against the public
hostname and confirm in the response. ACK tokens are single-purpose,
unguessable, durable across restarts, scoped to one wearer's one
escalation, and invalid or replayed tokens are rejected without side
effects.

#### Scenario: ACK link records the contact
- **WHEN** a contact taps their ACK link/button
- **THEN** the server marks that contact acknowledged for that
  escalation and logs the event

#### Scenario: Telegram acknowledgement round-trip
- **WHEN** a contact presses the inline acknowledge button in Telegram
- **THEN** the server records the acknowledgement within one polling
  interval (default 30 s, configurable), stops promotion per the
  ACK-gating rules, and the contact receives visible confirmation

#### Scenario: Stale token is inert
- **WHEN** an ACK token for a resolved or unknown escalation is used
- **THEN** nothing changes server-side and the response says the
  acknowledgement is no longer applicable
