# companion-enrollment-and-contacts Specification

## Purpose
TBD - created by archiving change companion-enrollment-and-contacts. Update Purpose after archive.
## Requirements
### Requirement: Enrollment by code is the primary onboarding path
The app SHALL onboard against a server with a server URL plus a
single-use enrollment code, exchanging the code for the wearer's bearer
token and storing it. Failure modes are distinguished for the user:
unreachable server, invalid/expired/used code, and success. Manual
token entry remains available as an explicitly secondary, advanced
path.

#### Scenario: New member self-onboards
- **WHEN** a wearer enters the server URL and a valid enrollment code
- **THEN** the app holds a working token, shows the connected state,
  and never displays the token value by default

#### Scenario: A used code fails clearly
- **WHEN** an already-exchanged code is entered
- **THEN** the app says the code was already used and to request a new
  one — not a generic error

### Requirement: The wearer manages their own contacts from the app
The app SHALL list, add, edit, and delete the wearer's contacts and
their tier assignment through the server API. Each contact requires a
name and at least one channel address (Telegram chat id, ntfy topic, or
email); server-side validation errors are shown inline at the offending
field. Edits require no restart of anything and never touch another
wearer's data.

#### Scenario: Fixing a stale contact on the phone
- **WHEN** the wearer replaces a contact's email address and saves
- **THEN** the change is validated, persisted server-side, and the next
  escalation send uses the new address

#### Scenario: Wearer configures their own copies
- **WHEN** the wearer sets a self-notification address (their own
  Telegram/ntfy/email) in the same management flow
- **THEN** it persists to the wearer record and future escalations send
  the wearer labeled copies per the server spec

### Requirement: The safety net is visible and its absence is loud
The app SHALL present the escalation order (tiers with their contacts
and timing) as the wearer's safety net, and SHALL surface the server's
per-wearer DEGRADED state (no deliverable contacts) prominently on the
main screen — equal in visibility to a fault, since an alarm that
reaches nobody is one.

#### Scenario: Empty contact list is treated as a fault
- **WHEN** the wearer's server-side contact list has no deliverable
  contact
- **THEN** the main screen shows a prominent warning until fixed, not a
  buried settings detail

### Requirement: Verification is adjacent to configuration
A fire-drill TEST action SHALL be reachable directly from the contact
management flow, so editing contacts and proving delivery are one
gesture apart.

#### Scenario: Edit, then drill
- **WHEN** the wearer saves contact changes
- **THEN** the UI offers the fire drill, and running it exercises the
  real chain with TEST tagging per the server spec

