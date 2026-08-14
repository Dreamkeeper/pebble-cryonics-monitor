# Proposal: server-delivery-and-persistence

## Why

The server currently *decides* who should be alerted (tiering, repeats,
ACK gating all work and are tested) but *delivers nothing*: the
escalation pump only writes log events, and every alarm, ACK token and
dead-man baseline lives in process memory, so a container restart —
which happened twice in one day during routine maintenance — silently
forgets active escalations. Until delivery and persistence exist, the
phone is the only real alert path and the server tier is bookkeeping;
this is the gap called out in the deployment runbook ("not yet
production-ready as a safety monitor") and in the product-requirements
pending-scope ledger.

## What Changes

- Wire the escalation pump to the channel plugins so due `Send`s are
  actually delivered: Telegram (bot message with inline ACK button),
  ntfy (push with ACK action), email (SMTP with ACK link). Per-send ACK
  tokens are minted, stored, and honored end to end.
- Receive Telegram acknowledgements via bot **long-polling** (no inbound
  webhook, so the public attack surface does not grow).
- Persist alarm state in SQLite under the already-mounted `/srv/data`
  volume: escalations with per-contact state, ACK tokens, the event
  log, and the dead-man baseline. On startup the server restores
  unresolved escalations and resumes their timers honestly (elapsed
  downtime counts toward repeat/promotion clocks).
- Replace the hardcoded placeholder tiers with operator-provided
  contact configuration (`contacts.yaml` in the data volume), validated
  at startup; a missing or invalid file puts the server into a visible
  DEGRADED state on `/api/v1/health` instead of silently escalating to
  nobody.
- Channel delivery failures are retried on the existing repeat cadence
  and surfaced in `/api/v1/status`; a send is "delivered" only when the
  transport accepted it.

Alarm-path impact (required disclosure): this change does not alter
detection or the ladder, so false-negative/false-positive *detection*
rates are untouched. It strictly reduces effective false negatives at
the delivery layer (today: 100% of server-side alerts are lost) and can
introduce duplicate notifications after a restart in one edge case
(send accepted by transport, crash before the persist commit) — the
design keeps that window minimal and duplicates are tagged with the
same escalation id.

## Non-goals

- Web dashboard / contact management UI (M2 scope, separate change).
- Multi-wearer support; the schema stores a wearer id but all logic
  remains single-wearer.
- Voice calls, SMS-from-server, SIP/Twilio plugins, the gateway-phone
  role (later versions per the product ledger).
- OwnTracks/Dawarich liveness probes.
- Postgres; SQLite only (single writer, NAS deployment).
- Any watchapp or Android companion change.

## Capabilities

### New Capabilities

(none — this delivers behavior already owed by the escalation capability)

### Modified Capabilities

- `escalation-and-deadman`: delivery becomes real (sends reach
  transports, with per-channel semantics and failure handling), ACKs
  become receivable (Telegram callbacks, ntfy/email links), state
  becomes durable across restarts, and contacts become operator
  configuration with a fail-visible degraded mode.

## Impact

- `server/app/`: new `store.py` (SQLite), new `contacts.py` (config
  load/validation), new `telegram_poll.py` (ACK long-poll task);
  `channels.py` gains real async delivery (offloaded blocking IO);
  `main.py` pump wires sends → channels and persists all state
  transitions.
- `server/tests/`: new suites for persistence round-trip, restart
  recovery, contact-config validation, channel dispatch (fake
  transports), Telegram callback parsing.
- Deployment: one new file for the operator (`/srv/data/contacts.yaml`),
  documented in `docs/DEPLOY-SYNOLOGY.md`; `.env` gains SMTP/ntfy keys
  already stubbed in `.env.example`. No new ports, no schema change to
  the phone API, no companion update required.
- After this change the NAS deployment must be added to the SSD boot
  guard / backup mirror per the homelab runbook note (the data volume
  now holds state worth protecting).
