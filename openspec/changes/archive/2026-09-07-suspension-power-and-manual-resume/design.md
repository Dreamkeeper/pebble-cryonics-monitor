# Design

## D1. Where the cadence decision lives

The core stays hardware-agnostic: it never knows sample periods. The
worker derives the idle cadence from core state in one place
(`set_hr_burst(false)` → `suspended || charging ? 300 s : 60 s`) and
re-applies it on every hold transition action (SUSPEND_STARTED,
SUSPEND_EXPIRED, AUTO_RESUMED, CHARGING_STARTED, CHARGING_ENDED).
Restore-at-init goes through `cm_suspend` → SUSPEND_STARTED, so a
worker restart mid-suspension lands on the hold cadence too. A lab
burst (`WMSG_HR_LAB`) still forces 1 s regardless.

## D2. Auto-resume under the slower cadence

Rule unchanged: sustained motion AND a bpm value change no older than
`resume_pulse_fresh_s` (150 s). With 300 s sampling, a re-worn watch
produces its first changed reading within at most 300 s; a wearer who
is moving then resumes at that instant. A wearer who is still at that
instant and starts moving more than 150 s later waits for the next
sample. Worst case is about 5 min plus warm-up; the SELECT path covers
the "I want it now" case, which is why the two changes ship together.
The freshness window is deliberately NOT widened: the 150 s bound is
what keeps a stale palm-touch reading from pairing with a later bag
ride.

## D3. SELECT semantics while suspended

`cm_user_ok` ordering: (1) latched ALARM → cancel (unchanged);
(2) active CHECKIN/COUNTDOWN → cancel (cannot occur while suspended,
kept for completeness); (3) suspended → `cm_resume` + reschedule
check-in; (4) otherwise early check-in (unchanged). Reusing
`cm_resume` means the worker's existing AUTO_RESUMED handling
(persist cleanup, brief app launch so the phone learns
PMSG_SUSPENDED=0) applies without new protocol. Carry mode
(`auto_resume=0`) is included on purpose: the spec already promises
"until the timer expires or the wearer resumes explicitly".

## D4. Charger hold

Docked watches get the 300 s cadence as well — charging is an
implicit suspension. SELECT does not end a charger hold (the hold
ends with the plug), unchanged.
