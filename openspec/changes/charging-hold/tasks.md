# Tasks: charging-hold

- [x] 1.1 Core cm_set_charging: silence while plugged, cancel pre-alarm
      stages (not ALARM), baseline reset + nag re-arm on unplug
- [x] 1.2 Worker BatteryStateService subscription + startup seed;
      charging flag in the status push
- [x] 1.3 Watch UI "Charging" + unplug confirmation pulse; nag hold
      released by docking
- [x] 1.4 PMSG_CHARGING; phone notification "ON CHARGER · " prefix
- [x] 1.5 Host tests: overnight silence, cancel-checkin-not-alarm,
      no instant triggers after unplug (105 -> 118 checks green)
- [ ] 2.1 Owner field check: dock the watch -> watch shows Charging +
      phone shows ON CHARGER within ~10 s; no nag overnight on the
      charger; unplug + wear -> single pulse, monitoring resumes
- [ ] 2.2 Archive after field check (updates suspension spec)
