# Design: server-delivery-and-persistence

## Context

See proposal.md — Why. Relevant current state: the escalation engine
(`escalation.py`) and dead-man monitor (`deadman.py`) are pure,
injected-clock state machines with pytest coverage; `channels.py` has
transport stubs using blocking `urllib`/`smtplib` inside `async`
signatures; the pump in `main.py` mints ACK tokens and logs but delivers
nothing; all state is module-level dicts. Deployment is Docker Compose
on a Synology NAS with `/srv/data` already bind-mounted (SSD). The
public hostname exists and terminates TLS at DSM; auth is
`Authorization: Bearer`.

Constraint carried from the pure-core design: `escalation.py` and
`deadman.py` stay IO-free. Persistence and delivery wrap them; they do
not grow database or network awareness.

## Goals / Non-Goals

Goals (design-level):
- At-least-once delivery with a minimal duplicate window and honest
  clocks across restarts.
- Every state transition that matters for recovery is committed before
  or immediately after the side effect it records, in a defined order.
- The pure state machines remain the single source of escalation logic;
  the store only snapshots and restores them.

Non-Goals: see proposal Non-goals. Additionally out of design scope:
schema migrations (fresh schema, version stamp for later), connection
pooling (single writer), and any queue abstraction — the repeat cadence
IS the retry queue.

## Decisions

**D1 — SQLite via stdlib `sqlite3`, WAL mode, single async writer.**
No ORM, no new dependency; the NAS deployment is single-process and
single-wearer. All DB access goes through one `Store` class called from
the event loop via `asyncio.to_thread`, keeping the loop unblocked
without a second concurrency model. Alternatives: SQLModel/SQLAlchemy
(dependency weight, no benefit at this size), Postgres (operationally
heavier than the thing it protects), JSON snapshot files (no atomicity
across related writes — rejected because ACK tokens and contact state
must commit together).

**D2 — Snapshot-restore, not event-sourcing.** The `Escalation` object
serializes to a row (kind, tiers config hash, started_t, resolution) plus
per-contact rows (attempts, last_sent_t, acked). On startup, rows are
rehydrated into `Escalation` instances and the pump resumes stepping
them with `time.time()` — elapsed downtime therefore counts naturally,
satisfying the "no clock reset" requirement with zero replay logic.
Alternative (event log replay) rejected: more moving parts to get the
same clocks.

**D3 — Commit-then-send for mints, send-then-commit for attempts.**
Order per due send: (1) mint + persist ACK token, (2) call transport,
(3) persist attempt result. A crash between 2 and 3 re-sends after
restart — that is the documented at-least-once window, and the re-send
carries the same escalation id and a still-valid ACK link. The reverse
order (persist attempt before sending) was rejected: it converts the
duplicate window into a silent-loss window, which is the wrong failure
mode for a safety monitor.

**D4 — Telegram long-polling task, not webhook.** A background task
long-polls `getUpdates` (default 30 s cycle, configurable via
`CM_TG_POLL_S`), filters `callback_query` updates whose data matches
`ack:<token>`, records the ACK, and answers the callback. Rationale: no
new inbound route on the public hostname, works even if the operator
never exposes the server publicly. Trade-off: up to one polling interval
of ACK latency — acceptable against promote_after_s=600. Offset is
persisted so restarts do not replay old updates.

**D5 — Channels get real async transport via `asyncio.to_thread`
around the existing blocking clients.** Keeps zero new dependencies
(no httpx). Each `deliver()` returns accepted/failed; the pump maps that
to `record_sent` (accepted) or leaves the contact unsent (failed) so the
repeat cadence retries it. Per-send timeout stays inside the channel.

**D6 — `contacts.yaml` on the data volume, validated with plain code.**
Format: tiers list with name/promote_after_s/repeat_after_s and
contacts carrying per-channel addressing (telegram chat_id, ntfy topic,
email address). Loaded once at startup; `openspec`-style hot reload is
out of scope. Missing/invalid → `health` gains `"degraded":
"no_contact_config"` while the phone API keeps working (the phone path
must not die because the server tier is misconfigured). PyYAML is the
one new dependency; alternative JSON was rejected because the operator
edits this file by hand over SSH.

**D7 — Fire-drill parity.** TEST alarms traverse the identical
pump/store/channel path with `[TEST]` prefixes injected at message
rendering only — no code path may branch on kind before rendering.

## Risks / Trade-offs

- **At-least-once duplicates** (D3): bounded to one send per
  crash-during-send; mitigated by same-escalation-id tagging in message
  text. Accepted.
- **Telegram poll latency** (D4): ≤ poll interval; irrelevant vs human
  response times. Accepted.
- **Blocking IO in threads** (D5): SMTP worst-case timeout (15 s) per
  send occupies a thread; with single-wearer contact counts this is
  noise. Revisit only if multi-wearer lands.
- **contacts.yaml has no UI** (D6): operator error surface; mitigated by
  strict startup validation with line-precise error messages and a
  documented example file. The dashboard change later replaces this.
- **WAL on Btrfs/NAS**: WAL is safe on the SSD volume; the daily backup
  must use the SQLite backup API or stop-copy (documented in tasks), not
  a live file copy of the WAL pair.

## Migration Plan

Fresh deployment: first start creates the schema (with a
`schema_version` row) and, absent `contacts.yaml`, comes up DEGRADED but
serving. No data to migrate from v0.1 (its state was in-memory by
definition). Rollback = previous image + ignore the new files; the DB
and contacts.yaml are inert to v0.1.

## Open Questions

- ntfy ACK action: `view` action opening the ACK URL is universally
  supported; `http` action would ACK without opening a browser but has
  spottier client support. Ship `view` first; revisit after field use.
- Whether the wearer should also receive a Telegram copy of escalation
  sends (self-notification). Deferred to the dashboard change.
