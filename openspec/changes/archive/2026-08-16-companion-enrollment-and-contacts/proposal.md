# Proposal: companion-enrollment-and-contacts

## Why

The owner's decision: contacts are managed from the Android app, not
files or curl. `server-delivery-and-persistence` ships the wearer/
contacts API; this change gives it its intended client — otherwise
response-group members would onboard by typing curl commands over SSH,
which fails the product requirement that a stranger can adopt the
system from documentation alone.

## What Changes

- **Enrollment flow**: first-run (or settings) screen accepts server
  URL + enrollment code (`XXXX-XXXX`), exchanges it for the per-wearer
  bearer token, stores the token, and replaces manual token entry as
  the primary path. Manual token entry remains as an advanced fallback.
- **Contact management screens**: list/add/edit/delete the wearer's own
  contacts (name + Telegram chat id / ntfy topic / email, ≥1 channel)
  and assign them to tiers; edits go through the server API with
  validation errors surfaced inline.
- **Tier view**: show the escalation order (who gets alerted, when
  promoted) so a wearer can verify their safety net at a glance;
  DEGRADED (no deliverable contacts) is shown prominently on the main
  screen, not buried.
- **Fire-drill affordance moves next to contacts**: after editing,
  the natural verification is one tap away.

Alarm-path impact (required disclosure): no detection or escalation
logic changes. Risk is misconfiguration via easier editing — mitigated
by server-side validation, the visible tier view, DEGRADED prominence,
and the adjacent fire drill. False-negative risk *decreases*: today
stale contacts can only be fixed over SSH, so they stay stale.

## Non-goals

- Operator/multi-wearer administration from the phone (web dashboard's
  job). The app manages only its own wearer.
- Contact opt-in confirmation flow (dashboard-era).
- Redesign of the settings screen beyond what these flows need.
- iOS.

## Capabilities

### New Capabilities

- `companion-enrollment-and-contacts`: the Android app's enrollment and
  self-service contact/tier management behavior.

### Modified Capabilities

(none — server behavior is specified in `server-delivery-and-persistence`)

## Impact

- `android/`: new enrollment screen, contacts list/editor screens, tier
  view; `ServerClient` gains enroll + contact/tier CRUD calls;
  `SettingsStore` stores the enrollment-derived token.
- Depends on `server-delivery-and-persistence` being implemented first
  (API must exist).
- No watchapp or server change.
