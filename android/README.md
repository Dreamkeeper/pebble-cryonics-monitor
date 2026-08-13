# Android companion

Kotlin foreground-service app pairing with the watchapp by UUID
(`7f8e2c40-...`) over the PebbleKit Classic broadcast-intent protocol.
The watchapp deliberately does NOT declare `companionApp` in its
package.json — that declaration switches the Core mobile app to the
PebbleKit2 bound-service protocol, which this app doesn't implement yet
(planned for M2 via PebbleKitAndroid2).

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
