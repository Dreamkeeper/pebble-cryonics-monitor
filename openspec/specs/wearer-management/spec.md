# wearer-management Specification

## Purpose
TBD - created by archiving change server-delivery-and-persistence. Update Purpose after archive.
## Requirements
### Requirement: Wearers are first-class, isolated tenants
The server SHALL support multiple wearers on one instance (a family, a
small cryonics organization, or a response group). Each wearer has an
identity (name, id), their own bearer token, contacts, tiers, dead-man
state, escalations, and event history. Wearers can be disabled without
deleting history; a disabled wearer's tokens stop authenticating and
their dead-man monitoring stops without alarming.

#### Scenario: A response group hosts several members
- **WHEN** an admin creates three wearers on one server
- **THEN** each wearer's phone authenticates only as itself, sees only
  its own data, and escalates only to its own contacts

### Requirement: Admin authentication is separate from wearer authentication
Administrative operations (wearer create/disable, enrollment-code
issuance, token revocation, cross-wearer reads, editing any wearer's
contacts) SHALL require an admin credential distinct from every wearer
token — provided via environment for now, replaced by operator accounts
in the web dashboard change. Wearer tokens SHALL NOT authorize admin
operations, and the admin credential SHALL never be required on the
phone's routine paths (heartbeat, alarm, own-contact management).

#### Scenario: A wearer token cannot administrate
- **WHEN** a request bearing a wearer token calls an admin endpoint
- **THEN** it is refused (403), and the attempt is logged

### Requirement: Phones enroll with one-time codes, not hand-typed tokens
The admin SHALL be able to issue a short-lived, single-use enrollment
code for a wearer. Exchanging the code SHALL return that wearer's bearer
token exactly once and burn the code; expired (default 24 h,
configurable) or reused codes are refused. Tokens SHALL be revocable and
reissuable per wearer without affecting other wearers.

#### Scenario: New member onboarding
- **WHEN** an admin issues an enrollment code and the member's app
  exchanges it
- **THEN** the app holds a working per-wearer token, the code no longer
  works for anyone, and a second exchange attempt is refused

### Requirement: Contacts and tiers are managed through the API
A wearer's escalation contacts and tier structure SHALL be readable and
editable via authenticated API: the wearer's own token manages that
wearer's contacts (self-service from the Android app); the admin
credential manages any wearer's. Edits validate channel addressing
(Telegram chat id, ntfy topic, email address — at least one channel per
contact) and take effect on the next escalation step without a restart.
Contact data appears only in responses authorized for that wearer.

#### Scenario: Wearer edits contacts from the phone
- **WHEN** a wearer's app replaces a contact's Telegram chat id
- **THEN** validation accepts it, the change persists, and the next
  send for that contact uses the new address — no file edits, no
  restart, no admin involvement

### Requirement: Legacy single-token deployments migrate automatically
On first boot with an empty wearer table and a configured legacy
`CM_API_TOKEN`, the server SHALL create a default wearer bound to that
token, so an existing phone keeps authenticating without reconfiguration.
The migration happens once; afterwards the legacy variable is inert as
an API credential.

#### Scenario: Existing deployment upgrades in place
- **WHEN** the current NAS deployment restarts on this version
- **THEN** the already-configured phone's heartbeats keep succeeding,
  now scoped to the auto-created default wearer

