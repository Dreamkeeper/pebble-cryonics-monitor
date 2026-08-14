# Design: companion-enrollment-and-contacts

## Context

See proposal.md — Why. Current companion state: programmatic UI (no
resource layouts), `SettingsStore` on SharedPreferences, `ServerClient`
with OkHttp + bearer header, CmLog for diagnostics. The server API this
consumes is specified in `server-delivery-and-persistence` (enroll,
contacts/tiers CRUD, per-wearer status incl. DEGRADED).

## Goals / Non-Goals

Goals: smallest UI that makes onboarding and contact upkeep phone-first
and mistake-resistant. Non-Goals: visual design system, Compose
migration, offline editing queues (edits require connectivity; the app
says so).

## Decisions

**D1 — Stay with programmatic views, add screens not frameworks.** The
app has four programmatic screens today; adding three more keeps the
zero-resource, zero-Compose approach consistent. Compose migration is a
separate quality change if the app grows further.

**D2 — Token handling**: enrollment response token goes straight into
`SettingsStore` (SharedPreferences, as today) and is displayed only as
"configured". Moving secrets to EncryptedSharedPreferences is noted as
a follow-up hardening item — not done here to keep this change's blast
radius UI-only.

**D3 — Server is the single source of truth for contacts.** No local
contact cache beyond the last fetched list for display; every edit is a
synchronous API call with inline error mapping (field-level 422 payload
from the server). Conflict handling is last-write-wins — acceptable at
"one wearer + maybe one operator" concurrency.

**D4 — DEGRADED surfacing** reuses the existing status-line/notification
plumbing: per-wearer DEGRADED from the status payload joins the FAULT
notification channel (high importance) and colors the main screen
banner. No new notification concepts.

## Risks / Trade-offs

- Editing safety-critical data on a phone: mitigated by server
  validation, tier view, DEGRADED prominence, adjacent fire drill.
- SharedPreferences token storage (D2): unchanged from today's posture;
  flagged as follow-up, not a regression.
- Last-write-wins (D3): fine at this concurrency; dashboard change can
  add etags if operators and wearers start colliding.

## Migration Plan

Existing installs keep their manually entered token (advanced path
remains). New installs are steered to enrollment. No data migration on
the phone.

## Open Questions

- None blocking; visual refinements deferred until the flows are used
  in anger.
