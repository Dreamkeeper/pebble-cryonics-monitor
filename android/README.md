# Android companion

Kotlin foreground-service app pairing with the watchapp (UUID
`7f8e2c40-...`, `companionApp` entry in `watchapp/package.json`).

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
