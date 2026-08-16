# End-to-end test procedure

Run top to bottom after any change to the watchapp, companion, or server.
Total time ~15 minutes plus one optional 35-minute soak. Prerequisites:
current .pbw and APK installed, phone enrolled, at least one contact with
Telegram, dashboard reachable.

Reading the phone notification: `watch ✓ 87% · synced 4m ago · server ✓`
- **watch ✓ / LINK DOWN** — live Bluetooth truth from the Pebble app.
- **synced N ago** — when the *watchapp* last spoke. It only can while
  open (the background worker cannot send messages — platform limit,
  documented in the protocol spec). A large value with `watch ✓` is
  normal in worker mode, NOT a fault. This resets when you open the
  watchapp (T1) and after any alert reaches the phone.
- **server ✓** — last heartbeat accepted by the server.

## T1 — Watch ↔ phone link (2 min)

1. Open Cryonics Monitor on the watch.
2. Within ~1 min the phone notification shows `synced …s ago` fresh, and
   watch battery appears.
3. Close the watchapp (BACK). The watchface returns. Expected: `synced`
   age now grows until the next alert or app-open — that is correct.

**Pass:** fresh sync while open; no FAULT notification while `watch ✓`.

## T2 — Full alarm chain: watch SOS → Telegram ACK (5 min)

1. On the watch, open the app and long-press DOWN (SOS). A 5 s countdown
   starts on the watch.
2. Let it expire. Expected within ~15 s:
   - phone: full-screen red alarm with siren,
   - Telegram: alert message with the ✅ acknowledge button,
   - Telegram: your `[copy to wearer]` self-notification,
   - dashboard fleet view: wearer top-ranked, `ESCALATING`.
3. Press the Telegram ✅ button. Expected: popup confirmation AND the
   message edits itself — button gone, `✅ ACKNOWLEDGED` appended.
4. On the phone alarm screen press **I'M OK — CANCEL**, pick a cause.
   Expected: watch alert clears, escalation resolves (dashboard shows no
   active escalation; Audit shows ack + resolve).

**Pass:** every step; nothing requires SSH or curl.

## T3 — Ghost-launch regression (3 min)

1. Sit still until a non-motion or pulse check-in fires ("Are you OK?"
   with vibration), OR provoke pulse-loss: keep the watch on but slide a
   finger under the sensor for ~3 minutes while still.
2. When the check-in vibrates, move your wrist deliberately.
3. Expected: alert dismisses itself AND the watch returns to your
   watchface. The app must NOT remain foreground showing "Monitoring".

**Pass:** watchface visible after implicit cancellation, no ghost screen.

## T4 — Suspension (3 min)

1. Watchapp → UP button: suspend 30 min. Phone notification reflects it;
   dashboard shows `suspended`.
2. Take the watch off for ~1 min, put it back on and walk a few steps.
3. Expected: auto-resume vibration; suspension cleared everywhere.

## T5 — Phone-silent advisory (optional 35-min soak)

1. Enable airplane mode on the phone WITHOUT declaring an offline window.
2. After ~30 min the server marks the wearer SILENT and escalates an
   ADVISORY (Telegram to contacts, clearly worded as advisory).
3. Disable airplane mode; acknowledge/resolve from Telegram or the
   dashboard.

**Pass:** advisory wording (not a confirmed alarm), single advisory (no
duplicates), recovery after reconnect.

## T6 — Fire drill from each surface (2 min)

Run a TEST drill from: the app (Contacts → Fire drill), the dashboard
(wearer page → Fire drill). Both must deliver `[TEST]`-tagged messages
and be resolvable from the dashboard with a typed reason.

## Known-limitation checklist (expected "failures")

- `synced` age grows while the watchapp is closed — by design until the
  DataLogging path (M0 spike S5) lands.
- No watch-side alert if the worker was evicted by another background
  app — check `Settings → Background App` on the watch after installing
  other apps.
- Email/ntfy channels silently skip unless configured in `.env`.
