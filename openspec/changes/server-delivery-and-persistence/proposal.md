# Proposal: server-delivery-and-persistence

## Why

The server currently *decides* who should be alerted (tiering, repeats,
ACK gating all work and are tested) but *delivers nothing*: the
escalation pump only writes log events, and every alarm, ACK token and
dead-man baseline lives in process memory, so a container restart —
which happened twice in one day during routine maintenance — silently
forgets active escalations. Until delivery and persistence exist, the
phone is the only real alert path and the server tier is bookkeeping.
The owner has also widened the product: one server instance must serve
multiple wearers (a family, a small cryonics company, or a response
group), with contacts managed from the Android app rather than files on
the NAS — so the data model and auth must be multi-wearer from the
start, not retrofitted.

## What Changes

- Wire the escalation pump to the channel plugins so due `Send`s are
  actually delivered: Telegram (bot message with inline ACK button),
  ntfy (push with ACK action), email (SMTP with ACK link). Per-send ACK
  tokens are minted, stored, and honored end to end.
- Receive Telegram acknowledgements via bot **long-polling** (no inbound
  webhook, so the public attack surface does not grow).
- Persist all alarm state in SQLite under the already-mounted
  `/srv/data` volume: wearers, contacts, tiers, escalations with
  per-contact state, ACK tokens, the event log, and per-wearer dead-man
  baselines. On startup the server restores unresolved escalations and
  resumes their timers honestly (elapsed downtime counts).
- **Multi-wearer data model and auth**: every monitored person is a
  `wearer` with their own bearer token, contacts, tiers, dead-man state
  and escalations, fully isolated from other wearers. An admin
  credential (env-provided for now; operator accounts arrive with the
  web dashboard change) creates wearers and issues one-time enrollment
  codes; the Android app exchanges an enrollment code for its per-wearer
  token. **BREAKING** for the API: phone endpoints now authenticate with
  per-wearer tokens; the legacy single `CM_API_TOKEN` deployment is
  migrated automatically into a default wearer on first boot so the
  existing phone keeps working.
- **Contacts managed via API, not files**: CRUD endpoints for a wearer's
  contacts and tiers, callable with that wearer's token (self-service
  from the Android app) or the admin credential (response-group
  operators). No `contacts.yaml`, no hardcoded placeholder tiers. A
  wearer with no deliverable contacts puts that wearer — not the server
  — into a visible DEGRADED state.
- Channel delivery failures are retried on the existing repeat cadence
  and surfaced in status; a send is "delivered" only when the transport
  accepted it.

Alarm-path impact (required disclosure): detection and the ladder are
untouched, so detection false-positive/negative rates do not change.
Delivery-layer false negatives strictly decrease (today 100% of
server-side alerts are lost). New risk introduced by multi-tenancy —
cross-wearer leakage — is addressed in the specs (strict per-wearer
isolation requirement). At-least-once delivery can duplicate one send
after a crash mid-send; duplicates carry the same escalation id.

## Non-goals

- Web dashboard and operator accounts — follow-up change
  `server-web-dashboard` (depends on this one).
- Android contact-management UI — follow-up change
  `companion-enrollment-and-contacts` (depends on this one); this change
  ships the API it will call.
- Contact opt-in confirmation flow (dashboard-era work).
- Voice calls, SMS-from-server, SIP/Twilio plugins, gateway-phone role.
- OwnTracks/Dawarich liveness probes.
- Postgres; SQLite only (single writer, NAS deployment).
- Any watchapp change.

## Capabilities

### New Capabilities

- `wearer-management`: wearer lifecycle (create/disable), enrollment
  codes and per-wearer token issuance/revocation, admin authentication,
  contact/tier CRUD, legacy single-token migration, per-wearer isolation
  guarantees.

### Modified Capabilities

- `escalation-and-deadman`: delivery becomes real (sends reach
  transports with per-channel semantics and failure handling), ACKs
  become receivable (Telegram long-poll callbacks, ntfy/email links),
  state becomes durable across restarts, and escalation/dead-man logic
  becomes per-wearer with contacts sourced from the store instead of
  operator files.

## Impact

- `server/app/`: new `store.py` (SQLite), new `wearers.py` (tenancy,
  enrollment, auth resolution), new `telegram_poll.py`; `channels.py`
  gains real async delivery; `main.py` gains wearer-scoped auth,
  contact/tier CRUD and admin endpoints; pump becomes per-wearer.
- `server/tests/`: persistence round-trip, restart recovery, wearer
  isolation (the critical new suite), enrollment/auth, contact CRUD,
  channel dispatch with fake transports, Telegram callback parsing.
- Android companion: no code change required for existing function (its
  bearer token keeps working via migration); the enrollment flow and
  contact screens land in `companion-enrollment-and-contacts`.
- Deployment: new env keys (`CM_ADMIN_TOKEN`, SMTP/ntfy already
  stubbed); no new ports; NAS data volume now holds state — add to SSD
  boot guard / daily backup per the homelab runbook note.
