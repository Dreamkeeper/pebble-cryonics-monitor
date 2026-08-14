# Tasks: server-delivery-and-persistence

## 1. Persistence foundation

- [ ] 1.1 Create `server/app/store.py`: SQLite schema (schema_version,
      escalations, contact_states, ack_tokens, events, deadman,
      tg_poll_offset), WAL mode, single `Store` class with
      `asyncio.to_thread` wrappers
- [ ] 1.2 Snapshot/rehydrate round-trip: `Escalation` ↔ rows, dead-man
      baseline ↔ row (D2); unit tests prove clocks continue across a
      simulated restart (elapsed downtime counts)
- [ ] 1.3 Wire `main.py` state transitions through the store: alarm
      intake, ack recording, resolution, event log, heartbeat baseline;
      startup restores unresolved escalations and dead-man state
- [ ] 1.4 Tests: restart during active escalation (repeat/promotion no
      later than without restart), resolved escalations stay resolved,
      ACK tokens survive, replayed/unknown tokens are inert

## 2. Contact configuration

- [ ] 2.1 Create `server/app/contacts.py`: load + validate
      `contacts.yaml` (tiers, per-channel addressing); line-precise
      error messages; add PyYAML to requirements
- [ ] 2.2 DEGRADED mode: missing/invalid config surfaces on
      `/api/v1/health` and `/api/v1/status`; phone API endpoints stay
      fully functional; remove `default_tiers()` placeholder entirely
- [ ] 2.3 Ship `server/contacts.example.yaml` with commented fields;
      tests for validation failures and degraded surfacing

## 3. Channel delivery

- [ ] 3.1 Rework `server/app/channels.py`: real async delivery via
      `asyncio.to_thread` (D5), per-send timeout, accepted/failed
      result; message rendering with alert kind, detector, location
      link, escalation id, and `[TEST]` prefix at render time only (D7)
- [ ] 3.2 Wire the pump: due send → mint+persist ACK token → deliver →
      persist attempt on transport acceptance (D3 ordering); failed
      channels retry on repeat cadence; delivery outcomes in status
- [ ] 3.3 Tests with fake transports: fan-out, partial channel failure
      retry, TEST parity (identical path), commit-ordering (crash
      between send and commit re-sends with same escalation id)

## 4. Acknowledgement receipt

- [ ] 4.1 Create `server/app/telegram_poll.py`: `getUpdates` long-poll
      task (interval `CM_TG_POLL_S`, default 30 s), `ack:<token>`
      callback parsing, `answerCallbackQuery` confirmation, persisted
      offset (D4)
- [ ] 4.2 ACK endpoint hardening: single-use semantics, "no longer
      applicable" response for resolved/unknown escalations
- [ ] 4.3 Tests: callback parsing, offset persistence across restart,
      idempotent/stale ACKs

## 5. Verification and deployment

- [ ] 5.1 Full server suite green (`server/.venv pytest`), plus
      detector suite unchanged-green (`watchapp/tests`) per project
      rule; no watchapp change expected — if any protocol file was
      touched, `pebble clean && pebble build`
- [ ] 5.2 Update `docs/DEPLOY-SYNOLOGY.md`: contacts.yaml setup, new
      `.env` keys, DEGRADED semantics, SQLite backup guidance (backup
      API or stop-copy — never live-copy the WAL pair)
- [ ] 5.3 Deploy to DKNexus via the documented no-sudo path (scp -O +
      helper container + docker:cli compose rebuild); verify health not
      degraded once contacts.yaml is in place, then run a live
      fire-drill TEST through Telegram/ntfy/email and confirm ACK
      round-trip on each channel
- [ ] 5.4 Restart the container mid-TEST-escalation and verify recovery
      on the NAS (the scenario that motivated this change)
- [ ] 5.5 Log the deployment in `HOMELAB-INFRASTRUCTURE-STATE.md` and
      add the CryoMonitor data volume to the SSD boot guard / daily
      backup per the runbook's standing note
- [ ] 5.6 Sync delta specs into `openspec/specs/escalation-and-deadman`
      and archive the change
