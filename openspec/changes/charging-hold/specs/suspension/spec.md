# Delta: suspension — charging is an implicit hold

## ADDED Requirements

### Requirement: Charging is an implicit suspension
When the watch reports charger power (`is_plugged`), monitoring SHALL
hold automatically: no detector may trigger and no hunts may run,
exactly as during a suspension. Entering the hold SHALL cancel an
active CHECKIN or COUNTDOWN with reason SUSPEND; a latched ALARM SHALL
NOT be cleared by docking the watch. On unplug, all detector baselines
SHALL reset so time on the charger cannot trigger anything instantly,
and the not-worn nag re-arms. The hold state SHALL be visible on the
watch ("Charging") and in the phone notification ("ON CHARGER"). The
hold ends with the charger — an unplugged, unworn watch is subject to
the normal removal/arrest rules thereafter.

#### Scenario: Watch charges overnight in silence
- **WHEN** the watch sits on its charger with no pulse and no motion
  for hours
- **THEN** no nag, hunt, check-in, or alarm is emitted, and the phone
  shows the charging hold

#### Scenario: Docking mid-check-in cancels it, docking mid-alarm does not
- **WHEN** the wearer docks the watch during a CHECKIN stage
- **THEN** the alert cancels with reason SUSPEND
- **AND** if a latched ALARM was active instead, it remains latched

#### Scenario: Unplugging does not instantly alert
- **WHEN** the watch comes off the charger and is worn again
- **THEN** no detector fires from the stillness accumulated while
  charging
