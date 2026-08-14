# Tasks: server-delivery-and-persistence

## 1. Persistence foundation

- [ ] 1.1 Create `server/app/store.py`: SQLite schema (schema_version,
      wearers, tokens, enroll_codes, contacts, tiers, escalations,
      contact_states, ack_tokens, events, deadman, tg_poll_offset), WAL
      mode, single `Store` class with `asyncio.to_thread` wrappers;
      every data method takes `wearer_id` first (D8)
- [ ] 1.2 Snapshot/rehydrate round-trip: `Escalation` ↔ rows with tier
      config snapshotted at creation (D2), per-wearer dead-man baseline
      ↔ row; unit tests prove clocks continue across a simulated restart
- [ ] 1.3 Wire `main.py` state transitions through the store: alarm
      intake, ack recording, resolution, event log, heartbeat baselines;
      startup restores unresolved escalations and dead-man state for
      every wearer
- [ ] 1.4 Tests: restart during active escalation (repeat/promotion no
      later than without restart), resolved stays resolved, ACK tokens
      survive, replayed/unknown tokens inert

## 2. Wearers, auth, enrollment, contacts API

- [ ] 2.1 Create `server/app/wearers.py`: auth resolution dependency
      (`Authorization` → role + wearer_id, hashed token lookup,
      constant-time compare), admin credential via `CM_ADMIN_TOKEN`
      (D8)
- [ ] 2.2 Admin endpoints: wearer create/disable/list, enrollment-code
      issuance (single-use, TTL `CM_ENROLL_TTL_S` default 24 h), token
      revocation/reissue; `POST /api/v1/enroll` exchanges code for
      token atomically (D9)
- [ ] 2.3 Legacy bootstrap (D10): empty wearer table + `CM_API_TOKEN`
      set → default wearer bound to that token hash; test proves the
      old phone credential still heartbeats after upgrade
- [ ] 2.4 Contact/tier CRUD endpoints (wearer token = own; admin = any
      wearer), channel-addressing validation, effective without restart
      (next escalation snapshot); remove `default_tiers()` placeholder
      entirely
- [ ] 2.5 Per-wearer DEGRADED (no deliverable contact): named in
      authenticated status, count-only on public health
- [ ] 2.6 Isolation test suite: every endpoint probed with another
      wearer's token and with wearer-token-vs-admin confusion; zero
      cross-wearer bytes

## 3. Channel delivery

- [ ] 3.1 Rework `server/app/channels.py`: real async delivery via
      `asyncio.to_thread` (D5), per-send timeout, accepted/failed
      result; rendering includes wearer name, alert kind, detector,
      location link, escalation id, `[TEST]` prefix at render time only
      (D7)
- [ ] 3.2 Wire the pump per wearer: due send → mint+persist ACK token →
      deliver → persist attempt on transport acceptance (D3); failed
      channels retry on repeat cadence; delivery outcomes in status
- [ ] 3.3 Wearer self-notification copies (D11): wearer record gains
      optional self-notify addresses (settable via contact API +
      admin); labeled copies on every send incl. phone-silent, no ACK
      affordance, never counted by ACK gating
- [ ] 3.4 Tests with fake transports: fan-out, partial channel failure
      retry, TEST parity, commit-ordering (crash between send and
      commit re-sends with same escalation id), two-wearer concurrent
      escalations stay separate, self-notify copies present/absent per
      configuration and inert for gating

## 4. Acknowledgement receipt

- [ ] 4.1 Create `server/app/telegram_poll.py`: `getUpdates` long-poll
      task (`CM_TG_POLL_S` default 30 s), `ack:<token>` callback
      parsing, `answerCallbackQuery` confirmation, persisted offset
      (D4)
- [ ] 4.2 ACK endpoint hardening: single-use semantics, wearer-scoped
      tokens, "no longer applicable" for resolved/unknown escalations
- [ ] 4.3 Tests: callback parsing, offset persistence across restart,
      idempotent/stale ACKs

## 5. Verification and deployment

- [ ] 5.1 Full server suite green (`server/.venv pytest`), plus
      detector suite unchanged-green (`watchapp/tests`) per project
      rule; no watchapp change expected — if any protocol file was
      touched, `pebble clean && pebble build`
- [ ] 5.2 Update `docs/DEPLOY-SYNOLOGY.md`: new env keys
      (`CM_ADMIN_TOKEN`, `CM_TG_POLL_S`, `CM_ENROLL_TTL_S`, SMTP/ntfy),
      admin API examples (create wearer, issue enrollment code, add
      contacts via curl until the app UI lands), per-wearer DEGRADED
      semantics, SQLite backup guidance (backup API or stop-copy —
      never live-copy the WAL pair)
- [ ] 5.3 Deploy to DKNexus via the documented no-sudo path; verify
      legacy bootstrap kept the existing phone authenticating; create
      the owner's real contacts via API; confirm wearer not DEGRADED
- [ ] 5.4 Live fire-drill TEST through Telegram/ntfy/email with ACK
      round-trip on each channel; then restart the container
      mid-TEST-escalation and verify recovery on the NAS (the scenario
      that motivated this change)
- [ ] 5.5 Log the deployment in `HOMELAB-INFRASTRUCTURE-STATE.md` and
      add the CryoMonitor data volume to the SSD boot guard / daily
      backup per the runbook's standing note
- [ ] 5.6 Sync delta specs into `openspec/specs/` (escalation-and-
      deadman modified, wearer-management new) and archive the change
