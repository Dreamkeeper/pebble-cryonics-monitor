# Design: server-web-dashboard

## Context

See proposal.md — Why. Builds directly on `server-delivery-and-
persistence`: the store, wearer model, escalation delivery and audit
event log all exist by the time this change starts. The server is one
FastAPI process behind DSM TLS on one public hostname.

## Goals / Non-Goals

Goals: an operator UI a NAS can serve forever — no build pipeline, no
node_modules, no separate service to babysit. Non-Goals: SPA
architecture, websockets, theming, mobile-first polish.

## Decisions

**D1 — Server-rendered Jinja2 + vendored htmx, same process.** Fleet
and detail views are plain templates; htmx (one static file, vendored,
no CDN — consistent with the self-hosting principle) provides polling
auto-refresh (`hx-get` every `CM_UI_REFRESH_S`, default 15 s) and
inline form posts. Alternatives rejected: React/Vite SPA (build
toolchain + API surface doubling for zero operator benefit at this
scale), separate dashboard container (another thing to keep alive on
the NAS).

**D2 — Sessions in the store, scrypt via stdlib `hashlib.scrypt`.** No
new crypto dependency. Session ids are 256-bit random, stored hashed,
HttpOnly + Secure + SameSite=Lax cookies, TTL `CM_SESSION_TTL_S`
(default 7 days), server-side revocation on logout and on password
change. Login endpoint rate-limited (per-account + per-IP counters in
the store — DSM has no per-route limiter we control).

**D3 — Roles are two booleans away from being an enum.** `admin` and
`responder` stored as a role column; authorization is a dependency that
takes the required role. No permission matrix framework — two roles,
one check.

**D4 — Operator ACK/resolve reuse engine methods.** The UI routes call
exactly `record_ack` / `resolve` with an `operator:<id>` actor tag —
no parallel logic. Resolve requires a typed reason (D: the risky
action gets friction proportional to risk).

**D5 — Admin-token retirement.** After the first admin account exists,
`CM_ADMIN_TOKEN` stops authenticating API admin routes (single release
of overlap: bootstrap boot accepts both, then env token is refused with
a pointed error). Wearer tokens are untouched. The wearer-management
spec's env-admin requirement gets superseded when this change's deltas
sync — the archive step must edit that requirement, and the proposal
marks this BREAKING.

**D6 — CSRF**: session-cookie + form-post model needs it; htmx posts
carry a per-session token in a header via `hx-headers`; state-changing
routes verify it. Cheaper than switching to token-in-header auth for
the UI.

## Risks / Trade-offs

- **Login becomes the public hostname's richest attack surface**: rate
  limiting, scrypt, generic failure messages, audit of failures.
  Accepted for self-hosted scale; fail2ban-style lockout is a follow-up
  if logs show abuse.
- **Polling refresh** (D1): up to `CM_UI_REFRESH_S` staleness on the
  fleet view; matches the dead-man's own granularity. Accepted.
- **Two auth systems in one process** (wearer tokens + operator
  sessions): kept apart by route namespace (`/api/v1/` vs `/ui/`) and
  separate dependencies; isolation tests cover the cross-cases (session
  on API route, token on UI route — both refused).

## Migration Plan

Ships as a normal image update. First boot after upgrade: admin
bootstrap from env (`CM_UI_ADMIN_USER`/`CM_UI_ADMIN_PASSWORD`, consumed
once, then flagged for removal from `.env`). `CM_ADMIN_TOKEN` overlap
per D5. Rollback: previous image ignores the new tables.

## Open Questions

- ~~Responder fire drills~~ — resolved by owner 2026-08-14: admin-only
  (a new responder must not be able to accidentally page a family's
  real contacts); revisit only if group practice demands it.
