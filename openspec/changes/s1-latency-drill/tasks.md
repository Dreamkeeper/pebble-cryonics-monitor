# Tasks: s1-latency-drill

## 1. Implementation

- [x] 1.1 Protocol: PMSG_DRILL/PMSG_DRILL_RESULT, WMSG_DRILL,
      PK_DRILL_FIRE_MS, CM_DRILL_DELAY_S (+ Protocol.kt mirror)
- [x] 1.2 Worker: arm on WMSG_DRILL, fire CM_ACT_LATENCY_DRILL through
      notify_app(launch=true) with a persisted fire timestamp
- [x] 1.3 App: PMSG_DRILL arms worker and exits; drill action computes
      launch delta, vibrates, reports PMSG_DRILL_RESULT, holds the
      stale-launch guard while the outbox flushes
- [x] 1.4 Android: heartbeat command dispatch, runLatencyDrill(),
      Diagnostics button, drill-result upload with Build.MODEL
- [x] 1.5 Server: kv command queue (exactly-once via heartbeat ack),
      POST /api/v1/drill-result -> latency_drill event, dashboard
      "⏱ Latency drill" button (admin)
- [x] 1.6 Version honesty: watchapp package.json 0.2.0 -> 0.3.6 (the
      installed-app version the Core app displays)

## 2. Verification

- [x] 2.1 Server suite green (68) incl. exactly-once command delivery
      and drill-result event test
- [x] 2.2 Clean pbw + APK builds
- [x] 2.3 Owner field run (2026-08-18, app button): launch=71 ms —
      go-gate met by two orders of magnitude; recorded in M0-SPIKES S1.
      First run exposed the phone-path arithmetic bug (guessed 10 s vs
      tick-aligned countdown) -> watch now reports arm->result total,
      phone derives pure BT transport (launch/watch/rtt/transport)
- [ ] 2.4 Dashboard-queued drill path exercised once (command channel
      end-to-end on hardware)

## 3. Wrap-up

- [ ] 3.1 Archive after the field run (updates watch-phone-protocol)
