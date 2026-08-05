# M0 Feasibility Spikes — go/no-go gates

Each spike answers a question the platform docs leave open. Run on real
hardware (Pebble Time 2 primary) before committing to the worker-mode
architecture. Record results in this file.

## S1 — Worker alarm path latency
**Question:** How long from `worker_launch_app()` in the background worker to
(a) first vibration and (b) AppMessage delivered to the Android companion?
**Method:** Instrumented worker fires on a button-simulated event; foreground
app timestamps launch, vibrates, sends AppMessage; companion logs receive time.
**Go:** < 3 s end-to-end. **No-go fallback:** persistent foreground mode becomes default.
**Result:** _pending_

## S2 — Worker survival across reboot / battery death
**Question:** Does the background worker auto-restart after a watch reboot or
battery die→recharge cycle? (Undocumented.)
**Method:** Confirm worker running (Settings → Background App), reboot watch,
check again; repeat with full battery drain.
**Go:** auto-restarts, or companion can detect death and re-launch via app start.
**Result:** _pending_

## S3 — Worker vibration capability
**Question:** Can the worker call `vibes_*` directly? (Docs omit it from the
worker API list but no explicit statement; a direct vibe would shorten the alarm path.)
**Method:** Call `vibes_short_pulse()` in worker; check build error / runtime behavior.
**Result:** _pending_

## S4 — Raw HR behavior at 1 s period
**Question:** With `health_service_set_heart_rate_sample_period(1)`, what does
`HealthMetricHeartRateRawBPM` report (values + update cadence) when: worn
normally; worn but perfectly still; strap loose; watch on a table?
Does the system honor ~1 s or throttle?
**Go:** off-wrist/still states distinguishable from normal wear within 60 s
using HR-signal-presence + accel variance together.
**Result:** _pending_

## S5 — DataLogging heartbeat latency
**Question:** When BT-connected, how quickly does a DataLogging session
flush to `PebbleDataLogReceiver` on Android? Is PebbleKitAndroid2's
DataLogging support functional (unconfirmed in its README)?
**Go:** median flush < 60 s → usable as worker heartbeat channel.
**No-go fallback:** periodic `worker_launch_app()` status sync (hourly) +
companion treats AppMessage silence as the heartbeat signal.
**Result:** _pending_

## S6 — Battery drain per sampling profile
**Question:** %/hour at: (a) accel 25 Hz batched 25, HR off; (b) + HR 60 s
period; (c) + HR 1 s period (burst mode). Both worker and foreground modes.
**Go:** profile (b) ≥ 7 days projected battery on Time 2.
**Result:** _pending_

## S7 — PebbleKitAndroid2 end-to-end
**Question:** AppMessage round-trip watch↔phone with Core app 1.0.7.7+ /
microPebble; `companionApp` pairing in package.json; connection events.
**Result:** _pending_

## S8 — Worker memory headroom
**Question:** Actual free heap in the worker on emery after subscribing to
accel (25-sample batch) + HealthService events (~2 kB) + detector state.
**Go:** > 2 kB headroom remaining.
**Result:** _pending_
