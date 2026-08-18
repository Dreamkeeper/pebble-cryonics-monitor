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
- [ ] 2.3 Owner field run: drill from the app button, then from the
      dashboard; go-gate launch_ms + vibe < 3 s; record numbers in
      docs/M0-SPIKES.md S1

## 3. Wrap-up

- [ ] 3.1 Archive after the field run (updates watch-phone-protocol)
