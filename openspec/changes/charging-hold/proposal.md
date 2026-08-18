# Proposal: charging-hold

## Why

Putting the watch on its charger is the one off-wrist act the watch can
detect *directly and unambiguously* — no pulse/motion inference needed.
Today it behaves like any other removal: the not-worn nag fires, and an
unlucky signal pattern could start a ladder. The owner's call: charger =
deliberate, treat it like a suspension, zero alerts.

## What changes

- Core: `cm_set_charging()` — while plugged, all detectors are silenced
  exactly like a suspension; entering the hold cancels CHECKIN/COUNTDOWN
  (reason SUSPEND) but NEVER a latched ALARM; unplugging resets all
  baselines (no instant triggers) and re-arms the nag.
- Worker: subscribes to BatteryStateService (`is_plugged`), seeds the
  state at startup (a reboot on the charger starts held).
- Watch UI: status shows "Charging"; the not-worn nag hold is released
  (the charger IS the answer to "not worn?"); unplug gives a short
  confirmation pulse. Status push carries the hold flag.
- Phone: new PMSG_CHARGING; notification shows `ON CHARGER · ` prefix.
- After unplugging, an abandoned watch earns alerts by the same
  arrest-vs-removal rules as any other signal loss — the hold ends when
  the charger does, deliberately.

## Non-goals

Server/dashboard visibility of the charging hold (phone notification
only for now); charging-based battery analytics (S6 owns that).

## Impact

- Specs: suspension (ADDED requirement)
- Code: detectors.{h,c}, worker.c, main.c, protocol.h, Protocol.kt,
  MonitorService.kt; tests 105 -> 118 checks
