# Android companion

Kotlin foreground-service app pairing with the watchapp (UUID
`7f8e2c40-...`).

Watch transport: **PebbleKit2** (`io.rebble.pebblekit2:client`, needs the
Core mobile app >= 1.0.7.7) is primary — the Core app binds
`PebbleKit2ListenerService` when the watchapp opens, and `WatchLink`
sends through `DefaultPebbleSender`. The PebbleKit Classic
broadcast-intent transport (`PebbleTransport`) remains as an automatic
fallback for older phone apps. The watchapp's `companionApp` declaration
in `watchapp/package.json` is what makes the Core app pick PK2 — keep the
package name in sync with `applicationId`.

Status: structural skeleton — transport (PebbleKitAndroid2), alarm UI, and
escalation are TODO(M1). Architecture is a Kotlin port of
OpenSeizureDetector's `Android_Pebble_SD` (GPL-3.0): `SdServer` →
[MonitorService](app/src/main/java/org/cryomonitor/companion/MonitorService.kt),
alarm state + SMS escalation → Escalator, plus a ServerClient for the
self-hosted backend.

Build flavors:
- `play` — Play-Store-safe (no SEND_SMS/CALL_PHONE).
- `sideload` — full fallback escalation + (later) the gateway-phone role;
  distributed as APK/F-Droid/Obtainium.

Requires Android Studio / SDK 35; not buildable in this repo's dev
environment yet.
