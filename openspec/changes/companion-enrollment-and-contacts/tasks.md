# Tasks: companion-enrollment-and-contacts

## 1. API client

- [x] 1.1 Extend `ServerClient`: `enroll(url, code)`, contacts/tiers
      CRUD, per-wearer status incl. DEGRADED; map field-level
      validation errors
- [x] 1.2 Unit tests for request shapes and error mapping (MockWebServer)

## 2. Enrollment flow

- [x] 2.1 Enrollment screen (URL + code), distinct failure messaging
      (unreachable / invalid / expired / used), success state; store
      token via `SettingsStore`
- [x] 2.2 Demote manual token entry to an "advanced" affordance;
      Save-time immediate heartbeat retained

## 3. Contact management

- [x] 3.1 Contacts list + editor screens (name, telegram chat id, ntfy
      topic, email; ≥1 channel enforced client-side, server errors
      inline); tier assignment
- [x] 3.2 Tier view rendering the escalation order with timings
- [x] 3.3 DEGRADED banner on main screen + FAULT-channel notification
      wiring from status payload
- [x] 3.4 Fire-drill affordance adjacent to the contacts flow
- [x] 3.5 Self-notification address editing on the wearer's own record
      (same flow as contacts; explains "copies of alerts to yourself")

## 4. Verification

- [ ] 4.1 Build both flavors; sideload on the Xiaomi 17 Ultra; enroll
      against the NAS with a real code; edit real contacts; run fire
      drill and confirm ACK round-trip
- [x] 4.2 Server suite + detector suite unchanged-green per project
      rule (no watchapp change expected; if protocol files were
      touched, `pebble clean && pebble build`)
- [x] 4.3 Update README/deploy docs: enrollment is the primary
      onboarding; curl examples marked operator-only
- [ ] 4.4 Sync delta spec into `openspec/specs/` and archive the change
