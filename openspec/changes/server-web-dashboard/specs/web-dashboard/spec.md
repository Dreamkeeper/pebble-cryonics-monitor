# Delta: web-dashboard — new capability

## ADDED Requirements

### Requirement: Operators authenticate with accounts, not shared tokens
The web UI SHALL require operator login (username + password, hashed
with a memory-hard KDF; session cookies that are HttpOnly, Secure, and
SameSite). Two roles exist: `admin` (full administration) and
`responder` (view everything, acknowledge/resolve escalations, no
wearer or account administration). The first admin account is
bootstrapped once from environment configuration; afterwards accounts
are managed by admins in the UI. Session lifetime is bounded (default
7 days, configurable) and logout invalidates the session server-side.

#### Scenario: Responder cannot administrate
- **WHEN** a responder session calls any admin action (wearer create,
  enrollment issuance, account management)
- **THEN** it is refused and the attempt is audited

### Requirement: The fleet view makes the worst thing obvious
Authenticated operators SHALL see every wearer with live state — OK,
LATE, SILENT, OFFLINE_DECLARED, suspended, DEGRADED, active escalation —
ordered worst-first, refreshing automatically (default every 15 s,
configurable) without manual reload. A wearer in active escalation or
SILENT SHALL be visually dominant; an empty-contact (DEGRADED) wearer
SHALL be flagged as a standing defect.

#### Scenario: A silent wearer cannot be missed
- **WHEN** any wearer's dead-man state becomes SILENT while an operator
  has the fleet view open
- **THEN** within one refresh interval that wearer is at the top with
  an unmistakable state indicator

### Requirement: Escalations can be acknowledged and resolved from the web
Operators SHALL be able to acknowledge an active escalation (same
ACK-gating semantics as contact acknowledgements) and resolve it as
"handled" or "false_alarm" with a required free-text reason. Both
actions record the operator identity and appear in the event log and in
subsequent channel messages' context. Resolution from the web follows
the same rules as API resolution — resolved escalations stop all sends.

#### Scenario: Operator takes ownership
- **WHEN** an operator acknowledges an escalation from the dashboard
- **THEN** tier promotion stops exactly as if a contact had
  acknowledged, and the audit log names the operator

### Requirement: Wearer administration happens in the browser
Admins SHALL create and disable wearers, issue enrollment codes
(displayed once), revoke/reissue wearer tokens, edit any wearer's
contacts and tiers with the same validation as the API, and trigger a
fire drill for any wearer — all from the UI, with no SSH or curl.

#### Scenario: Onboarding a member without a terminal
- **WHEN** an admin creates a wearer and issues an enrollment code in
  the UI
- **THEN** the code is shown once for hand-off, and the new member's
  app can enroll with it immediately

### Requirement: Every operator action is audited
Login success/failure, acknowledgements, resolutions, configuration
edits, account changes, and enrollment issuance SHALL be recorded in
the persistent event log with operator identity and timestamp, and be
visible (read-only) to admins in the UI.

#### Scenario: Who resolved it?
- **WHEN** an escalation was resolved from the web last night
- **THEN** the audit view shows which operator, when, and the typed
  reason

### Requirement: The dashboard shares the API's exposure discipline
The UI SHALL live under the same hostname and TLS termination as the
API, add no new unauthenticated routes beyond the login form and its
static assets, apply rate limiting to login attempts, and leak no
wearer data on any pre-authentication surface.

#### Scenario: Logged-out dashboard is a blank door
- **WHEN** an unauthenticated client requests any `/ui/` route
- **THEN** it receives the login form (or a redirect to it) and zero
  wearer-derived bytes
