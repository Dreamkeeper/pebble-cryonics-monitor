# Design: server-delivery-and-persistence

## Context

See proposal.md — Why. Relevant current state: the escalation engine
(`escalation.py`) and dead-man monitor (`deadman.py`) are pure,
injected-clock state machines with pytest coverage; `channels.py` has
transport stubs using blocking `urllib`/`smtplib` inside `async`
signatures; the pump in `main.py` mints ACK tokens and logs but delivers
nothing; all state is module-level dicts assuming exactly one wearer.
Deployment is Docker Compose on a Synology NAS with `/srv/data` already
bind-mounted (SSD). The public hostname exists and terminates TLS at
DSM; auth is `Authorization: Bearer`.

Constraint carried from the pure-core design: `escalation.py` and
`deadman.py` stay IO-free and single-wearer-ignorant — tenancy lives in
the layer that instantiates them (one `DeadmanMonitor` and N
`Escalation` objects per wearer), not inside them.

## Goals / Non-Goals

Goals (design-level):
- At-least-once delivery with a minimal duplicate window and honest
  clocks across restarts.
- Every state transition that matters for recovery is committed before
  or immediately after the side effect it records, in a defined order.
- Strict wearer isolation enforced at one chokepoint (auth resolution →
  wearer id → store queries all keyed by it), not sprinkled through
  handlers.
- The pure state machines remain the single source of escalation logic.

Non-Goals: see proposal Non-goals. Additionally out of design scope:
schema migrations beyond the legacy-token bootstrap (fresh schema,
version stamp for later), connection pooling (single writer), queue
abstractions, and password-based accounts (dashboard change).

## Decisions

**D1 — SQLite via stdlib `sqlite3`, WAL mode, single async writer.**
No ORM, no new dependency; single-process deployment. All DB access goes
through one `Store` class called via `asyncio.to_thread`. Alternatives:
SQLModel/SQLAlchemy (dependency weight), Postgres (operationally heavier
than the thing it protects), JSON snapshots (no atomicity across related
writes). Multi-wearer stays comfortably inside SQLite's envelope: a
response group is tens of wearers, not thousands.

**D2 — Snapshot-restore, not event-sourcing.** Each `Escalation`
serializes to one row (wearer id, resolved flag, started_t for
indexing) whose payload is the full `to_state()` JSON snapshot — tier
config and per-contact attempts/acks included. One blob per escalation
is atomic by construction; per-contact rows were considered and dropped
because nothing queries contact state outside its escalation. On
startup, rows rehydrate into `Escalation` instances per wearer and the
pump resumes stepping them with `time.time()` — elapsed downtime counts
naturally, satisfying "no clock reset" with zero replay logic. The tier
config is snapshotted into the escalation at creation so mid-escalation
contact edits do not mutate a running alarm (edits apply from the next
escalation; simpler to reason about during an emergency).

**D3 — Commit-then-send for mints, send-then-commit for attempts.**
Order per due send: (1) mint + persist ACK token, (2) call transport,
(3) persist attempt result. A crash between 2 and 3 re-sends after
restart — the documented at-least-once window; the re-send carries the
same escalation id and a still-valid ACK link. The reverse order was
rejected: it converts the duplicate window into a silent-loss window,
the wrong failure mode for a safety monitor.

**D4 — Telegram long-polling task, not webhook.** One background task
long-polls `getUpdates` (default 30 s cycle, `CM_TG_POLL_S`), filters
`callback_query` data matching `ack:<token>`, records the ACK (the token
itself resolves wearer + escalation + contact), answers the callback.
One bot serves all wearers; contacts store per-contact chat ids. No new
inbound route. Offset persisted so restarts do not replay updates.

**D5 — Channels get real async transport via `asyncio.to_thread` around
the existing blocking clients.** Zero new dependencies. Each `deliver()`
returns accepted/failed; the pump records the attempt for every due
contact either way — an unsent contact would come due again every pump
cycle and hammer a failing transport, so failures advance the clock too
and the retry lands on the tier's repeat cadence. Per-send timeout
inside the channel.

**D6 — Contacts and tiers live in the store, managed via API.** CRUD
endpoints under `/api/v1/contacts` + `/api/v1/tiers` (wearer token =
own data; admin = any wearer). Validation is plain code (channel
addressing shapes, ≥1 channel per contact). No contacts.yaml, no PyYAML,
no restart to apply edits. The Android app is the intended editor
(follow-up change); until it ships, curl against the documented API is
the operator path. Alternative (file-based config) was the previous
design — rejected by the owner in favor of app-managed contacts.

**D7 — Fire-drill parity.** TEST alarms traverse the identical
pump/store/channel path with `[TEST]` prefixes injected at message
rendering only — no code path may branch on kind before rendering.

**D8 — Tenancy resolves at the auth boundary, once.** A single
dependency resolves `Authorization` → `(role, wearer_id)`: wearer tokens
map to their wearer, `CM_ADMIN_TOKEN` maps to admin (wearer chosen via
explicit path/query parameter on admin endpoints). Every store method
takes `wearer_id` as its first argument; nothing reads a module-level
"current wearer". Isolation tests assert cross-wearer invisibility on
every endpoint. Tokens are stored hashed (SHA-256 — high-entropy random
tokens don't need a slow KDF; constant-time compare on lookup).

**D9 — Enrollment codes over hand-typed tokens.** Admin issues a
single-use code (default TTL 24 h, `CM_ENROLL_TTL_S`); `POST
/api/v1/enroll {code}` returns the wearer token exactly once and burns
the code atomically (same transaction). Codes are short enough to type
on a phone (format `XXXX-XXXX`, crockford base32, ~40 bits — acceptable
for single-use + TTL + rate limit). Token revocation = new token row +
old row invalidated; history keeps wearer id, not token.

**D10 — Legacy bootstrap.** First boot, empty wearer table, legacy
`CM_API_TOKEN` set → create wearer "default" bound to (the hash of)
that token, marked migrated in the event log. Keeps the currently
deployed phone working with zero action. Afterwards `CM_API_TOKEN` is
ignored for auth (admin token is `CM_ADMIN_TOKEN`, new).

**D11 — Self-notification via configured address, no separate toggle.**
Owner decision (2026-08-14): a wearer may receive copies of their own
escalation notifications. Design: the wearer record carries optional
self-notification channel addresses (Telegram chat id / ntfy topic /
email); configuring an address IS the opt-in — no separate boolean to
drift out of sync. When set, the wearer is an implicit recipient on
their own escalations (clearly labeled "copy to wearer", no ACK button
— wearer awareness must not satisfy ACK gating; cancelling from the
phone remains the wearer's action). Especially valuable for
phone-silent advisories read on a second device.

## Risks / Trade-offs

- **At-least-once duplicates** (D3): bounded to one send per
  crash-during-send; mitigated by same-escalation-id tagging. Accepted.
- **Telegram poll latency** (D4): ≤ poll interval; irrelevant vs human
  response times. Accepted.
- **Blocking IO in threads** (D5): SMTP worst-case 15 s per send
  occupies a thread; fine at response-group scale. Revisit past ~50
  wearers.
- **Cross-wearer leakage is the new top risk** (D8): mitigated by the
  single auth chokepoint, wearer-id-first store signatures, and a
  dedicated isolation test suite that hits every endpoint with the
  wrong wearer's token.
- **Interim contact editing UX** (D6): until the companion change
  ships, contact edits are curl/API calls. Accepted by the owner in
  choosing API-managed contacts; documented examples reduce the pain.
- **Enrollment code entropy** (D9): ~40 bits, single-use, TTL-bound,
  rate-limited on the enroll endpoint. Accepted; codes are not tokens.
- **WAL on Btrfs/NAS**: safe on the SSD volume; the daily backup must
  use the SQLite backup API or stop-copy, never a live copy of the WAL
  pair.

## Migration Plan

First start creates the schema (with `schema_version` row) and runs the
D10 legacy bootstrap if applicable. Rollback = previous image; the DB is
inert to v0.1. The phone needs no update for existing function; the
enrollment flow only matters for newly added wearers until the
companion change lands.

## Open Questions

- ntfy ACK action: ship `view` (universal support) first; revisit
  `http` action after field use.
- ~~Wearer self-notification~~ — resolved by owner 2026-08-14: yes,
  optional per wearer (D11).
