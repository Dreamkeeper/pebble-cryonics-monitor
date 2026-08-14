# Tasks: server-web-dashboard

## 1. Accounts and sessions

- [ ] 1.1 Store: operators, sessions tables; scrypt hashing; session
      create/lookup/revoke; login rate-limit counters
- [ ] 1.2 `operators.py`: login/logout routes, session dependency, role
      check dependency, first-boot admin bootstrap (env consumed once)
- [ ] 1.3 CSRF token per session; verification on all state-changing UI
      routes
- [ ] 1.4 Tests: auth, roles, session expiry/revocation, rate limiting,
      CSRF, cross-auth refusal (session on API, wearer token on UI)

## 2. Fleet and wearer views

- [ ] 2.1 Vendor htmx static file; base template + layout
- [ ] 2.2 Fleet view: all wearers worst-first with live states, htmx
      polling refresh (`CM_UI_REFRESH_S` default 15 s)
- [ ] 2.3 Wearer detail: dead-man state, heartbeat/battery trail,
      active + historical escalations, event log, contacts/tiers
- [ ] 2.4 Tests: state ordering, no wearer data on pre-auth surfaces

## 3. Actions

- [ ] 3.1 Escalation acknowledge + resolve (typed reason required) via
      engine methods with operator actor tags
- [ ] 3.2 Admin: wearer create/disable, enrollment code issuance
      (display-once), token revoke/reissue, contact/tier editing, fire
      drill per wearer
- [ ] 3.3 Operator account management (admin): create, role change,
      password reset, disable
- [ ] 3.4 Audit view (admin, read-only); every action lands in the
      event log with operator id
- [ ] 3.5 `CM_ADMIN_TOKEN` retirement per design D5, with the pointed
      error message

## 4. Verification and deployment

- [ ] 4.1 Full server suite green; detector suite unchanged-green per
      project rule (no watchapp change expected; if protocol files
      touched, `pebble clean && pebble build`)
- [ ] 4.2 Docs: DEPLOY-SYNOLOGY gains UI bootstrap env keys, first
      login, account management, and login-hardening notes
- [ ] 4.3 Deploy to DKNexus (documented no-sudo path); bootstrap admin;
      create a responder account; verify fleet view over
      https://cm.dkvasnikov.ru/ui/ from outside the LAN
- [ ] 4.4 Live drill: fire drill from the UI, acknowledge from the UI,
      resolve with reason; verify audit entries and that CM_ADMIN_TOKEN
      is refused post-bootstrap
- [ ] 4.5 Log deployment in `HOMELAB-INFRASTRUCTURE-STATE.md`
- [ ] 4.6 Sync deltas (incl. superseding wearer-management's env-admin
      requirement) and archive the change
