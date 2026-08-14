# Proposal: server-web-dashboard

## Why

With multi-wearer support, the server serves response groups — and a
response group needs a shared pane of glass: who is OK, who is silent,
what is escalating, who acknowledged. Today that exists only as an
authenticated JSON endpoint and per-contact messages. Operators also
need to administer wearers (create, enroll, disable, fix contacts)
without SSH. This is the "web for the server" the owner asked for, and
it replaces the interim env-var admin token with real operator
accounts.

## What Changes

- **Web UI served by the existing FastAPI process** under `/ui/`:
  - Fleet view: every wearer's live state (OK / LATE / SILENT /
    OFFLINE_DECLARED / suspended / DEGRADED / active escalation),
    sorted worst-first, auto-refreshing.
  - Wearer detail: dead-man state, last heartbeat, battery trail,
    active + historical escalations, event log, contacts/tiers.
  - Escalation actions: acknowledge and resolve ("handled" /
    "false_alarm") from the browser, recorded with the operator's
    identity.
  - Admin screens: wearer create/disable, enrollment-code issuance,
    contact/tier editing for any wearer, fire drill for any wearer.
- **Operator accounts** with username + password (scrypt-hashed),
  session cookies, and two roles: `admin` (everything) and `responder`
  (view + acknowledge/resolve, no wearer administration). First-boot
  bootstrap creates the initial admin from env once. **BREAKING** for
  administration: `CM_ADMIN_TOKEN` API access is retired after
  bootstrap; the API keeps serving wearer tokens and (new)
  operator-session auth for admin routes.
- Audit trail: every operator action (login, ack, resolve, config edit,
  enrollment issuance) lands in the persistent event log with the
  operator id.

Alarm-path impact (required disclosure): no detection change. Two new
human paths into a running escalation (web ack/resolve) use the same
engine methods as existing paths; resolve-from-web is the risky one —
mitigated by requiring a typed resolution reason and logging the
operator identity. False negatives decrease for response groups
(silent wearers become visible on a shared screen instead of in
nobody's Telegram scrollback).

## Non-goals

- Public signup, SSO/OIDC, password reset by email (self-hosted, admin
  resets passwords).
- Charts/analytics beyond the battery/heartbeat trail.
- Mobile-app parity (the app remains the wearer's tool; the web is the
  operator's).
- Real-time push (auto-refresh polling is sufficient at this scale).
- Exposing the dashboard on a separate hostname or port.

## Capabilities

### New Capabilities

- `web-dashboard`: operator-facing web UI — fleet monitoring, wearer
  administration, escalation acknowledge/resolve, operator accounts,
  roles, sessions, and audit.

### Modified Capabilities

(none directly; wearer-management's env-admin-token requirement is
superseded at archive time — noted in design Migration)

## Impact

- `server/app/`: new `ui/` (Jinja2 templates + htmx), `operators.py`
  (accounts, sessions, roles), route module for UI + admin actions;
  store gains operators/sessions tables and audit columns.
- New dependency: Jinja2 (+ python-multipart for form posts). No JS
  build toolchain — htmx vendored as a single static file.
- DSM reverse proxy: no change (same hostname/port); `/ui/` inherits
  TLS.
- Depends on `server-delivery-and-persistence` (store, wearers,
  escalation delivery) being implemented first.
