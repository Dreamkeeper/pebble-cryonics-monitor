# Adversarial review task: entire Pebble Cryonics Monitor codebase

You are an adversarial reviewer with full access to this repository.
Your mission is to find real defects — bugs that would fire a false
alarm, miss a real one, crash a component, lose data, or let an
outsider interfere — in a **life-safety monitoring system**. You are
not a style checker. Assume the authors are competent and the obvious
things work (there is field testing behind this code); hunt for what
field testing does NOT catch: edge cases, races, wrap-arounds,
lifecycle corners, malformed input, and adversarial input.

Write your findings to a new file: `docs/CODEBASE-REVIEW-FINDINGS.md`.

## What this system is

An open-source (GPL-3.0) unresponsiveness monitor for cryonicists:
a Pebble Time 2 watchapp + background worker (C), an Android
companion (Kotlin), and a self-hosted FastAPI server. The watch
detects candidate emergencies (pulse-signal loss, impact, prolonged
non-motion, missed check-in, manual SOS) and walks an alert ladder
(check-in prompt → countdown → alarm) designed to give the wearer
every chance to cancel; a full alarm escalates to human responders
via Telegram/ntfy/email with acknowledgment tracking. There is no
auto-EMS call. Detailed specs live in `openspec/specs/` — treat them
as the source of truth for intended behavior and flag code/spec
divergences.

## Severity model (rank findings by this, worst first)

1. **Missed alarm** — a real emergency that produces no escalation.
2. **Stuck monitoring** — a component silently stops (worse than
   crashing: crashes are watchdogged).
3. **False alarm to contacts** — erodes responder trust.
4. **Security/privacy** — unauthorized alarm injection, suppression,
   data exfiltration (HR + location on the server), token handling.
5. **Data loss/corruption** — audit trail, enrollment, contacts.
6. Everything else (wrong UI numbers, nits).

## Where the code is

- `watchapp/src/core/detectors.{h,c}` — platform-free detector state
  machine. Host tests: `watchapp/tests/test_detectors.c` (build line
  in its header; 154 checks currently green — run them).
- `watchapp/worker_src/c/worker.c` — background worker shell (10.5 KB
  RAM budget; cannot vibrate/UI/AppMessage; DataLogging + launching
  the app are its only outputs).
- `watchapp/src/c/main.c` — foreground app shell (UI, AppMessage to
  phone, persist handoff from worker).
- `watchapp/src/core/protocol.h` — persist keys, worker↔app message
  types, phone↔watch message types, DataLogging record layout.
- `android/app/src/main/java/org/cryomonitor/companion/` — the
  companion: `MonitorService.kt` (hub), `WatchLink.kt`/
  `PebbleTransport.kt`/`Pk2Bus.kt` (PebbleKit2 transport),
  `DataLogReceiver.kt` (legacy DL broadcasts), `Escalator.kt`
  (phone-direct SMS/Telegram), `ServerClient.kt`, `BootReceiver.kt`,
  `SoakStats.kt`, activities. Unit tests under `app/src/test/`.
- `server/app/` — FastAPI: `main.py` (API), `escalation.py` (engine),
  `deadman.py`, `channels.py` (Telegram/ntfy/SMTP), `store.py`
  (SQLite), `ui.py` (dashboard + session auth). Tests: `server/tests/`
  (pytest).

Out of scope: `dist/` binaries, `openspec/changes/archive/`,
`tools/`, anything under upstream fork checkouts (not in this repo).

## Attack angles to work through (per component)

**Detector core (C):** 32-bit `now_ms` wrap (epoch ms truncated to
uint32 wraps ~49.7 days — `elapsed()` claims wrap-safety for spans
< 24.8 days; find any comparison, deadline, or persisted timestamp
that breaks across a wrap or across worker restarts); suspension
deadlines vs clock changes (DST, phone-synced time jumps — wall clock
vs uptime); state-machine escapes (any path where a stage latches
forever, a snooze never clears, or `q_overflow` drops a
safety-critical action); the interplay of charging hold, lab hold,
suspension, and carry mode (can overlapping holds strand a detector
off?); resume conjunction edge cases.

**Worker (C):** the 10.5 KB budget (heap exhaustion paths — what
allocates after init?); persist-key collisions or stale persists
surviving reinstall; the parked-action handoff (timestamped, voided
at worker init — race windows left?); DataLogging session lifecycle
across reboots; behavior when `health_service_peek_current_value` is
called with metric 9 on firmware without it (guarded by `s_qmetric` —
can that flag be wrong after a firmware downgrade?).

**App shell (C):** AppMessage loss (every PMSG is fire-and-forget —
which losses are dangerous vs self-healing?); the launch-reason /
auto-close logic; UI state vs worker state divergence.

**Companion (Kotlin):** Android lifecycle (Doze, app standby,
HyperOS freezes, Second Space pausing the main space — the service
must fail LOUD, never silently); `START_STICKY` restart with null
intent; coroutine scopes surviving service destroy; the 5 s
disconnect debounce and ≥10 s outage gate (flapping links); the
2-strike server-unreachable logic; `DataLogReceiver` parsing
malformed/short/hostile records (it is an EXPORTED receiver — any
app can send `com.getpebble.action.dl.*` intents: what can a
malicious sender inject? forged worker heartbeats reset watchdogs);
`PebbleKit2ListenerService` is exported too; SharedPreferences
races; escalation dedup between server-driven and phone-direct
paths (double alerts? neither?).

**Server (Python):** auth on every route (wearer token vs admin vs
dashboard session — find any unauthenticated state-changing route);
ack-token entropy/reuse/expiry (an ack link neutralizes an
escalation — can it be guessed, replayed, or used cross-wearer?);
escalation engine restart correctness (mid-escalation crash: does the
pump resume from the DB?); exactly-once command queue (drill
commands); SQLite concurrency (threads + asyncio.to_thread — any
cross-thread connection use, missing WAL, lost writes?); timezone
handling of age gates; channel failures half-completing a fan-out;
dashboard XSS/CSRF (session cookie flags, form handling); enrollment
TTL and token migration logic; the phone_silent grace math.

**Cross-cutting:** every watchdog watches something — draw the chain
(worker → app → phone service → server → responders) and find the
link nobody watches; places where two clocks are compared (watch
epoch vs phone epoch vs server UTC); protocol version skew (old
watchapp + new companion and vice versa — the DL record's pad/heap64
byte, packed lab samples, PMSG additions).

## Method requirements

- Read the actual code before claiming anything; cite `file:line`.
- For each finding: severity class (from the model above), a concrete
  failure scenario (inputs/state → wrong outcome), and a suggested
  fix. Separate CONFIRMED (you traced the code path end-to-end) from
  PLAUSIBLE (needs a runtime check you cannot do).
- Run the test suites you can (`test_detectors.c` build line is in
  its header; `server/tests` with pytest). A finding the existing
  tests already cover is not a finding.
- No invented APIs: if you assert platform behavior (Pebble SDK,
  Android lifecycle), say whether it is documented, inferred, or
  assumed.
- Do not report: style, naming, missing comments, TODO items already
  marked, or hypotheticals requiring physical access to an unlocked
  phone.

## Output format (docs/CODEBASE-REVIEW-FINDINGS.md)

```
# Codebase review findings — <date>
Reviewed: <commit hash>

## Verdict
<2-3 sentences per subsystem: sound / needs work / blocker found>

## Findings
### <N>. [<severity class>] <one-line title> — CONFIRMED|PLAUSIBLE
Location: <file:line>
Scenario: <state + input → wrong outcome>
Fix: <suggestion>
```

Number findings; worst first. An empty findings list is a valid
outcome only if you actually did the work — show which paths you
traced in the verdict.
