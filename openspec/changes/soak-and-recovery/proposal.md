# Soak & recovery: reboot survival, soak telemetry, and the multi-day protocol

## Why

The M0 spikes proved feasibility in supervised sessions; the remaining
release risk is unattended behavior over days. Three concrete gaps
(owner decisions 2026-08-28):

1. **Phone reboot kills monitoring silently.** The companion has no
   `BOOT_COMPLETED` receiver — after any reboot (update, crash, dead
   battery) monitoring stays down until the wearer opens the app. The
   server dead-man detects `phone_silent`, but a safety product must
   self-recover, not just report its own death.
2. **False alarms are unmeasured.** The wearer received a burst of
   Telegram alarms overnight (pre-fix build); alarm fatigue is the
   killer metric for a safety product. We fixed bugs, but "it seems
   quieter now" is not evidence — we need counted alarms per day over
   a normal week.
3. **Nobody records recovery behavior.** BLE outages, service
   restarts, worker gaps: the events fly by in the log ring. A soak
   verdict needs counters, not anecdotes.

## What changes

- **Boot recovery (companion):** a `BootReceiver` starts
  `MonitorService` on `BOOT_COMPLETED` and `MY_PACKAGE_REPLACED` when
  the app is configured (server URL set). HyperOS/MIUI blocks boot
  broadcasts unless the user enables Autostart — the app documents it
  and the recovery lab verifies it (we cannot grant it
  programmatically).
- **Soak telemetry (companion):** `MonitorService` persists lifetime
  counters (service starts, boot-recovery starts, watch disconnects +
  cumulative downtime, DataLogging records, alarms/pre-alarms fired,
  self-heal launches, server send failures). The Debug screen gains a
  passive **Soak** card (counters since reset, share/export, reset)
  in the style of the S5/S6 cards.
- **Recovery lab (companion):** a guided, confirmation-gated stage
  runner in Debug (sensor-lab pattern) for the two recovery drills
  that need a human hand: phone reboot (verifies the service came
  back within the gate after boot, unattended) and a watch outage
  (power off ≥5 min; verifies disconnect detection and reconnect
  time). Results persist across the reboot and are shareable.
- **Protocol (docs):** `docs/SOAK-TEST.md` — the 7-day soak protocol:
  what to run, what counts as PASS, and the daily 2-minute checklist.

## Impact

- Affected specs: `companion-resilience` (new capability)
- Affected code: `android/` (BootReceiver, MonitorService counters,
  DebugActivity soak card + recovery lab), `docs/SOAK-TEST.md`
- Companion version bump 0.4.10 → 0.5.0 (code 29)
- Out of scope here: server survivability work (separate plan,
  `docs/SERVER-DEPLOYMENT.md`), OEM autostart auto-grant (impossible
  without MIUI-specific hacks; documented instead)
