/*
 * Host unit tests for the detector core. Scenario-driven: simulated seconds
 * of accelerometer + HR data are fed in, emitted actions are asserted.
 *
 * Build (MSVC):  cl /W4 /std:c11 /I..\src\core test_detectors.c ..\src\core\detectors.c
 * Build (gcc):   gcc -Wall -Wextra -std=c11 -I../src/core -o test_runner \
 *                    test_detectors.c ../src/core/detectors.c
 */
#include <stdio.h>
#include <string.h>
#include "detectors.h"

static int g_failures = 0;
static int g_checks = 0;
static const char *g_test = "";

#define CHECK(cond) do { \
    g_checks++; \
    if (!(cond)) { \
      g_failures++; \
      printf("FAIL %s:%d [%s] %s\n", __FILE__, __LINE__, g_test, #cond); \
    } \
  } while (0)

/* ---- simulation harness ---- */

static cm_core core;
static uint32_t now_ms;
static uint8_t sim_hour = 12;

#define MAX_LOG 64
static cm_action log_actions[MAX_LOG];
static int log_count;

static void drain(void) {
  cm_action a;
  while (cm_next_action(&core, &a)) {
    if (log_count < MAX_LOG) log_actions[log_count++] = a;
  }
}

static void log_reset(void) { log_count = 0; }

static int count_type(cm_action_type t) {
  int n = 0;
  for (int i = 0; i < log_count; i++) if (log_actions[i].type == t) n++;
  return n;
}

static const cm_action *find_type(cm_action_type t) {
  for (int i = 0; i < log_count; i++)
    if (log_actions[i].type == t) return &log_actions[i];
  return 0;
}

static void feed_batch(int16_t z_mg) {
  cm_accel_sample s[25];
  for (int i = 0; i < 25; i++) {
    s[i].x = 0; s[i].y = 0; s[i].z = z_mg; s[i].did_vibrate = 0;
  }
  cm_accel_feed(&core, s, 25, now_ms);
}

/* one simulated second, lying still */
static void sec_still(void) {
  now_ms += 1000;
  feed_batch(-1000);
  cm_tick(&core, now_ms, sim_hour);
  drain();
}

/* one simulated second with deliberate movement */
static void sec_moving(void) {
  now_ms += 1000;
  feed_batch(-1000);
  feed_batch(-1300); /* jerk 300 mg > threshold */
  cm_tick(&core, now_ms, sim_hour);
  drain();
}

static void sec_still_hr(uint16_t bpm) {
  now_ms += 1000;
  feed_batch(-1000);
  cm_hr_feed(&core, bpm, now_ms);
  cm_tick(&core, now_ms, sim_hour);
  drain();
}

/* one simulated second with movement AND a pulse reading */
static void sec_moving_hr(uint16_t bpm) {
  now_ms += 1000;
  feed_batch(-1000);
  feed_batch(-1300);
  cm_hr_feed(&core, bpm, now_ms);
  cm_tick(&core, now_ms, sim_hour);
  drain();
}

static void mins_still(int minutes) { for (int i = 0; i < minutes * 60; i++) sec_still(); }
static void secs_still(int seconds) { for (int i = 0; i < seconds; i++) sec_still(); }

/* freefall then hard impact within one batch */
static void event_fall(void) {
  cm_accel_sample s[3];
  memset(s, 0, sizeof(s));
  s[0].z = -1000;
  s[1].z = -100;   /* mag 100 < freefall_below (300) */
  s[2].z = -3000;  /* mag 3000 > impact_above (2400) */
  now_ms += 1000;
  cm_accel_feed(&core, s, 3, now_ms);
  cm_tick(&core, now_ms, sim_hour);
  drain();
}

static cm_config test_cfg(void) {
  cm_config cfg;
  cm_config_defaults(&cfg);
  cfg.pulse_lost_after_s = 30; /* tight timings so tests read in seconds */
  cfg.pulse_hunt_s = 30;
  cfg.pulse_still_s = 20;
  cfg.pulse_snooze_min = 10;
  cfg.checkin_ui_s = 30;
  cfg.countdown_s = 30;
  cfg.countdown_impact_s = 20;
  cfg.notworn_after_min = 15;
  return cfg;
}

static void setup(const cm_config *cfg) {
  now_ms = 1000000;
  sim_hour = 12;
  log_reset();
  cm_init(&core, cfg, now_ms);
}

/* establish a "worn, alive" baseline: motion, then a still-but-alive
 * minute of JITTERING pulse — real HR is never flat (S4), and the last
 * value change must clear the removal window before scenarios begin so
 * a subsequent signal loss reads as arrest, not removal */
static void warmup(void) {
  for (int i = 0; i < 5; i++) { sec_moving(); }
  for (int i = 0; i < 60; i++) { sec_still_hr((uint16_t)(70 + (i & 1))); }
  log_reset();
}

/* ---- tests ---- */

static void test_defaults(void) {
  g_test = "defaults";
  cm_config cfg;
  cm_config_defaults(&cfg);
  CHECK(cfg.hr_available == 1);
  CHECK(cfg.pulse_lost_after_s == 150);
  CHECK(cfg.nonmotion_day_min == 40);
  CHECK(cfg.nonmotion_night_min == 90);
  CHECK(cfg.countdown_impact_s < cfg.countdown_s); /* impacts get a faster fuse */
  CHECK(cfg.notworn_after_min == 3);   /* removal nags fast, never contacts */
  CHECK(cfg.pulse_proof_min == 5);     /* live pulse = proof of life */
  CHECK(cfg.pulse_flat_after_s == 300); /* frozen value = stale (S4) */
  CHECK(cfg.pulse_hunt_s == 45);       /* burst spin-up ~23 s measured (S4) */
  CHECK(cfg.removal_window_s == 45);   /* motion-after-pulse = removal */
  CHECK(cfg.resume_grace_s == 60);     /* auto-resume arming delay */
  /* Scheduled check-in is opt-in; every passive detector is on by default. */
  CHECK(cfg.enabled[CM_DET_CHECKIN] == 0);
  for (int i = 0; i < CM_DET_COUNT; i++)
    if (i != CM_DET_CHECKIN) CHECK(cfg.enabled[i] == 1);
}

static void test_impact_full_ladder(void) {
  g_test = "impact_full_ladder";
  cm_config cfg = test_cfg();
  cfg.enabled[CM_DET_PULSE] = 0;   /* isolate: no HR feed in this scenario */
  cfg.enabled[CM_DET_NOTWORN] = 0;
  setup(&cfg);
  warmup();

  event_fall();
  CHECK(count_type(CM_ACT_CHECKIN_START) == 0); /* nothing yet: immobility gate */

  /* settle (5 s) + immobility window (60 s) while lying still */
  secs_still(66);
  const cm_action *ci = find_type(CM_ACT_CHECKIN_START);
  CHECK(ci != 0);
  CHECK(ci && ci->detector == CM_DET_IMPACT);
  log_reset();

  /* CHECKIN stage times out (30 s) -> countdown */
  secs_still(31);
  const cm_action *cd = find_type(CM_ACT_COUNTDOWN_START);
  CHECK(cd != 0);
  CHECK(cd && cd->detector == CM_DET_IMPACT);
  CHECK(cd && cd->seconds == 20); /* impact fuse */
  log_reset();

  /* motion during COUNTDOWN must NOT cancel (explicit tap only) */
  sec_moving();
  CHECK(count_type(CM_ACT_ALERT_CANCELLED) == 0);

  /* countdown expires -> ALARM */
  secs_still(20);
  const cm_action *al = find_type(CM_ACT_ALARM);
  CHECK(al != 0);
  CHECK(al && al->detector == CM_DET_IMPACT);
  CHECK(cm_current_stage(&core) == CM_STAGE_ALARM);
  log_reset();

  /* user clears the latched alarm */
  cm_user_ok(&core, now_ms);
  drain();
  const cm_action *cc = find_type(CM_ACT_ALERT_CANCELLED);
  CHECK(cc != 0);
  CHECK(cc && cc->reason == CM_CANCEL_USER);
  CHECK(cm_current_stage(&core) == CM_STAGE_NONE);
}

static void test_impact_cancelled_by_motion(void) {
  g_test = "impact_cancelled_by_motion";
  cm_config cfg = test_cfg();
  cfg.enabled[CM_DET_PULSE] = 0;
  cfg.enabled[CM_DET_NOTWORN] = 0;
  setup(&cfg);
  warmup();

  event_fall();
  secs_still(10);   /* past settle */
  sec_moving();     /* wearer moves deliberately */
  mins_still(3);
  CHECK(count_type(CM_ACT_CHECKIN_START) == 0);
  CHECK(count_type(CM_ACT_ALARM) == 0);
}

static void test_impact_checkin_motion_dismiss(void) {
  g_test = "impact_checkin_motion_dismiss";
  cm_config cfg = test_cfg();
  cfg.enabled[CM_DET_PULSE] = 0;
  cfg.enabled[CM_DET_NOTWORN] = 0;
  setup(&cfg);
  warmup();

  event_fall();
  secs_still(66);
  CHECK(count_type(CM_ACT_CHECKIN_START) == 1);
  log_reset();

  sec_moving(); /* motion during CHECKIN stage auto-dismisses */
  const cm_action *cc = find_type(CM_ACT_ALERT_CANCELLED);
  CHECK(cc != 0);
  CHECK(cc && cc->reason == CM_CANCEL_MOTION);
  CHECK(cm_current_stage(&core) == CM_STAGE_NONE);
}

static void test_pulse_loss_full_ladder(void) {
  g_test = "pulse_loss_full_ladder";
  cm_config cfg = test_cfg();
  setup(&cfg);
  warmup();

  /* pulse disappears while lying still */
  secs_still(31); /* pulse_lost_after 30 s (still >= 20 s satisfied) */
  const cm_action *hb = find_type(CM_ACT_HR_BURST_ON);
  CHECK(hb != 0);
  CHECK(count_type(CM_ACT_CHECKIN_START) == 0); /* hunt is silent */
  log_reset();

  secs_still(31); /* hunt (30 s) finds nothing */
  const cm_action *ci = find_type(CM_ACT_CHECKIN_START);
  CHECK(ci != 0);
  CHECK(ci && ci->detector == CM_DET_PULSE);
  log_reset();

  secs_still(31); /* CHECKIN times out */
  CHECK(count_type(CM_ACT_COUNTDOWN_START) == 1);
  log_reset();

  secs_still(31); /* countdown expires */
  CHECK(count_type(CM_ACT_ALARM) == 1);
  CHECK(count_type(CM_ACT_HR_BURST_OFF) == 1); /* burst released at alarm */
}

static void test_pulse_returns_during_hunt(void) {
  g_test = "pulse_returns_during_hunt";
  cm_config cfg = test_cfg();
  setup(&cfg);
  warmup();

  secs_still(31);
  CHECK(count_type(CM_ACT_HR_BURST_ON) == 1);
  log_reset();

  sec_still_hr(72); /* pulse found: silent stand-down */
  CHECK(count_type(CM_ACT_HR_BURST_OFF) == 1);
  for (int i = 0; i < 60; i++) sec_still_hr(70); /* pulse stays present */
  CHECK(count_type(CM_ACT_CHECKIN_START) == 0);
}

static void test_pulse_checkin_dismissed_by_pulse(void) {
  g_test = "pulse_checkin_dismissed_by_pulse";
  cm_config cfg = test_cfg();
  setup(&cfg);
  warmup();

  secs_still(62); /* through hunt into CHECKIN */
  CHECK(count_type(CM_ACT_CHECKIN_START) == 1);
  log_reset();

  sec_still_hr(68); /* pulse returns during CHECKIN */
  const cm_action *cc = find_type(CM_ACT_ALERT_CANCELLED);
  CHECK(cc != 0);
  CHECK(cc && cc->reason == CM_CANCEL_PULSE);
  CHECK(count_type(CM_ACT_HR_BURST_OFF) == 1);
  CHECK(cm_current_stage(&core) == CM_STAGE_NONE);
}

static void test_pulse_user_cancel_snoozes(void) {
  g_test = "pulse_user_cancel_snoozes";
  cm_config cfg = test_cfg();
  setup(&cfg);
  warmup();

  secs_still(62);
  CHECK(count_type(CM_ACT_CHECKIN_START) == 1);
  cm_user_ok(&core, now_ms);
  drain();
  log_reset();

  /* still no pulse, still still — but snoozed for 10 min */
  mins_still(5);
  CHECK(count_type(CM_ACT_HR_BURST_ON) == 0);
}

static void test_nonmotion_daytime(void) {
  g_test = "nonmotion_daytime";
  cm_config cfg = test_cfg();
  cfg.enabled[CM_DET_PULSE] = 0;   /* isolate the non-motion detector */
  cfg.enabled[CM_DET_NOTWORN] = 0;
  cfg.hr_available = 0;            /* flint/gabbro: motion is the only signal */
  setup(&cfg);
  warmup();

  /* perfectly still on motion-only hardware (warmup already banked
   * 60 s of stillness — fire lands at the loop end, stage fresh) */
  mins_still(39);
  secs_still(5);
  const cm_action *ci = find_type(CM_ACT_CHECKIN_START);
  CHECK(ci != 0);
  CHECK(ci && ci->detector == CM_DET_NONMOTION);
  log_reset();

  sec_moving(); /* motion dismisses */
  const cm_action *cc = find_type(CM_ACT_ALERT_CANCELLED);
  CHECK(cc != 0);
  CHECK(cc && cc->reason == CM_CANCEL_MOTION);
}

static void test_nonmotion_night_threshold(void) {
  g_test = "nonmotion_night_threshold";
  cm_config cfg = test_cfg();
  cfg.enabled[CM_DET_PULSE] = 0;
  cfg.enabled[CM_DET_NOTWORN] = 0;
  cfg.hr_available = 0;
  setup(&cfg);
  warmup();
  sim_hour = 2; /* night */

  mins_still(60);  /* under the 90 min night limit */
  CHECK(count_type(CM_ACT_CHECKIN_START) == 0);

  mins_still(31);  /* now past 90 min */
  CHECK(count_type(CM_ACT_CHECKIN_START) == 1);
}

/* The wearer's scenario: sleeping / meditating / watching TV. Perfectly
 * still with a live pulse must NEVER ping on HR hardware — the pulse IS
 * the proof of life. */
static void test_still_with_pulse_stays_silent(void) {
  g_test = "still_with_pulse_stays_silent";
  cm_config cfg = test_cfg();
  cfg.pulse_lost_after_s = 150;  /* realistic default: 60 s samples are fresh */
  setup(&cfg);
  warmup();

  for (int i = 0; i < 50 * 60; i++) {   /* 50 min, well past day threshold */
    if (i % 60 == 0) sec_still_hr((uint16_t)(62 + ((i / 60) & 1)));
    else sec_still();
  }
  CHECK(count_type(CM_ACT_CHECKIN_START) == 0);
  CHECK(count_type(CM_ACT_HR_BURST_ON) == 0);
  CHECK(count_type(CM_ACT_NOTWORN_NAG) == 0);
  CHECK(count_type(CM_ACT_ALARM) == 0);
}

/* Backstop band: the HR sensor silently stops reading mid-sleep. Once the
 * pulse is staler than pulse_proof_min (but inside the worn grace), the
 * accumulated stillness may ping. */
static void test_nonmotion_backstop_stale_pulse(void) {
  g_test = "nonmotion_backstop_stale_pulse";
  cm_config cfg = test_cfg();
  cfg.enabled[CM_DET_PULSE] = 0;   /* isolate from the pulse ladder */
  cfg.enabled[CM_DET_NOTWORN] = 0;
  setup(&cfg);
  warmup();

  for (int i = 0; i < 36 * 60; i++) {   /* still, pulse alive: silent */
    if (i % 60 == 0) sec_still_hr((uint16_t)(60 + ((i / 60) & 1)));
    else sec_still();
  }
  CHECK(count_type(CM_ACT_CHECKIN_START) == 0);

  mins_still(4);                        /* pulse now stale, proof holds */
  CHECK(count_type(CM_ACT_CHECKIN_START) == 0);

  mins_still(2);                        /* proof lapsed, worn grace not yet */
  const cm_action *ci = find_type(CM_ACT_CHECKIN_START);
  CHECK(ci != 0);
  CHECK(ci && ci->detector == CM_DET_NONMOTION);
}

static void test_notworn_nag_not_alarm(void) {
  g_test = "notworn_nag_not_alarm";
  cm_config cfg = test_cfg();
  cfg.enabled[CM_DET_NONMOTION] = 0;
  cfg.enabled[CM_DET_CHECKIN] = 0;
  setup(&cfg);
  warmup();

  /* watch comes off: pulse gone + still. Pulse ladder fires first; user cancels. */
  secs_still(62);
  CHECK(count_type(CM_ACT_CHECKIN_START) == 1);
  cm_user_ok(&core, now_ms);
  drain();
  log_reset();

  /* 15+ min later: not-worn nag (to wearer only), and no new pulse ladder
   * (worn-grace has lapsed, so pulse-loss no longer applies) */
  mins_still(16);
  CHECK(count_type(CM_ACT_NOTWORN_NAG) == 1);
  CHECK(count_type(CM_ACT_ALARM) == 0);
  CHECK(count_type(CM_ACT_CHECKIN_START) == 0);

  /* nag fires once, not repeatedly */
  mins_still(10);
  CHECK(count_type(CM_ACT_NOTWORN_NAG) == 1);
}

static void test_scheduled_checkin(void) {
  g_test = "scheduled_checkin";
  cm_config cfg = test_cfg();
  cfg.enabled[CM_DET_PULSE] = 0;
  cfg.enabled[CM_DET_NONMOTION] = 0;
  cfg.enabled[CM_DET_NOTWORN] = 0;
  cfg.enabled[CM_DET_CHECKIN] = 1;  /* opt-in feature, enabled for this test */
  cfg.checkin_interval_min = 60;
  cfg.checkin_remind_min = 5;
  cfg.checkin_grace_min = 10;
  setup(&cfg);

  /* stay active so nothing else triggers */
  for (int i = 0; i < 54 * 60; i++) { if (i % 30 == 0) sec_moving(); else sec_still(); }
  CHECK(count_type(CM_ACT_CHECKIN_REMINDER) == 0);
  for (int i = 0; i < 2 * 60; i++) sec_still();
  CHECK(count_type(CM_ACT_CHECKIN_REMINDER) == 1); /* T-5 min reminder */
  log_reset();

  /* miss the deadline: due (60) + grace (10) */
  for (int i = 0; i < 15 * 60; i++) { if (i % 30 == 0) sec_moving(); else sec_still(); }
  const cm_action *ci = find_type(CM_ACT_CHECKIN_START);
  CHECK(ci != 0);
  CHECK(ci && ci->detector == CM_DET_CHECKIN);
  log_reset();

  /* motion must NOT dismiss a scheduled check-in — button only */
  sec_moving();
  CHECK(count_type(CM_ACT_ALERT_CANCELLED) == 0);
  cm_user_ok(&core, now_ms);
  drain();
  CHECK(count_type(CM_ACT_ALERT_CANCELLED) == 1);

  /* answering rescheduled the next round */
  CHECK(cm_checkin_due_in_s(&core, now_ms) > 59u * 60u);
}

static void test_suspension_blocks_and_autoresumes(void) {
  g_test = "suspension_blocks_and_autoresumes";
  cm_config cfg = test_cfg();
  setup(&cfg);
  warmup();

  cm_suspend(&core, 1800, 1, now_ms); /* 30 min, auto-resume on */
  drain();
  CHECK(count_type(CM_ACT_SUSPEND_STARTED) == 1);
  log_reset();

  /* watch on the shelf: no pulse, no motion, 20 min — total silence expected */
  mins_still(20);
  CHECK(log_count == 0);

  /* bag ride: motion alone must NOT resume — being carried off-wrist is
   * a valid suspend state (owner decision 2026-08-29) */
  for (int i = 0; i < 120; i++) sec_moving();
  CHECK(count_type(CM_ACT_AUTO_RESUMED) == 0);
  CHECK(cm_suspend_remaining_s(&core, now_ms) > 0);

  /* back on the wrist: sustained motion + changing bpm -> auto-resume */
  for (int i = 0; i < 20; i++) sec_moving_hr((uint16_t)(70 + (i & 1)));
  CHECK(count_type(CM_ACT_AUTO_RESUMED) == 1);
  CHECK(cm_suspend_remaining_s(&core, now_ms) == 0);
}

static void test_suspension_expiry(void) {
  g_test = "suspension_expiry";
  cm_config cfg = test_cfg();
  setup(&cfg);
  warmup();

  cm_suspend(&core, 60, 0, now_ms);
  drain();
  log_reset();

  secs_still(62);
  CHECK(count_type(CM_ACT_SUSPEND_EXPIRED) == 1);
  /* baselines were reset: no instant pulse/non-motion trigger */
  CHECK(count_type(CM_ACT_HR_BURST_ON) == 0);
  CHECK(count_type(CM_ACT_CHECKIN_START) == 0);
}

/* Resume needs BOTH halves: readings without motion never resume (the
 * phantom-press case), and motion whose pulse evidence has gone stale
 * never resumes either (the bag-ride case). */
static void test_suspension_pulse_does_not_resume(void) {
  g_test = "suspension_pulse_does_not_resume";
  cm_config cfg = test_cfg();
  setup(&cfg);
  warmup();

  cm_suspend(&core, 3600, 1, now_ms);
  drain();
  log_reset();

  mins_still(10);
  for (int i = 0; i < 30; i++) sec_still_hr(75); /* phantom "pulse" on a shelf */
  CHECK(count_type(CM_ACT_AUTO_RESUMED) == 0);
  CHECK(cm_suspend_remaining_s(&core, now_ms) > 0);

  /* the phantom change ages past the fresh window; then motion alone */
  mins_still(4);
  for (int i = 0; i < 30; i++) sec_moving();
  CHECK(count_type(CM_ACT_AUTO_RESUMED) == 0);

  /* both together = wrist evidence */
  for (int i = 0; i < 20; i++) sec_moving_hr((uint16_t)(70 + (i & 1)));
  CHECK(count_type(CM_ACT_AUTO_RESUMED) == 1);
}

/* The T4 field bug: pressing "suspend" while still wearing the watch used
 * to auto-resume within a second (fresh pulse). The arming grace must
 * swallow all signals — including continuous motion — for resume_grace_s. */
static void test_suspension_grace_blocks_instant_resume(void) {
  g_test = "suspension_grace_blocks_instant_resume";
  cm_config cfg = test_cfg();
  setup(&cfg);
  warmup();

  cm_suspend(&core, 1800, 1, now_ms);
  drain();
  log_reset();

  /* wearer walks off with the watch on (motion + live pulse) */
  for (int i = 0; i < 50; i++) sec_moving_hr((uint16_t)(70 + (i & 1)));
  CHECK(count_type(CM_ACT_AUTO_RESUMED) == 0);

  /* grace over: 15 s worn run resumes */
  for (int i = 0; i < 30; i++) sec_moving_hr((uint16_t)(70 + (i & 1)));
  CHECK(count_type(CM_ACT_AUTO_RESUMED) == 1);
  CHECK(cm_suspend_remaining_s(&core, now_ms) == 0);
}

/* Sensor died while worn (field 2026-08-29: the HRM failed to start on
 * 2 of 3 consecutive boots): a moving wearer with a flat bpm gets the
 * sensor-fault nag — not "Not worn?", never the ladder. */
static void test_sensor_fault_fires_when_moving_without_pulse(void) {
  g_test = "sensor_fault_fires_when_moving_without_pulse";
  cm_config cfg = test_cfg();
  cfg.sensor_fault_after_min = 4;
  setup(&cfg);
  warmup();

  for (int i = 0; i < 3 * 60; i++) sec_moving();
  CHECK(count_type(CM_ACT_SENSOR_FAULT) == 0); /* under threshold */

  for (int i = 0; i < 2 * 60; i++) sec_moving();
  CHECK(count_type(CM_ACT_SENSOR_FAULT) == 1);
  CHECK(count_type(CM_ACT_NOTWORN_NAG) == 0);
  CHECK(count_type(CM_ACT_CHECKIN_START) == 0);
  CHECK(count_type(CM_ACT_ALARM) == 0);

  /* once per episode: motion never re-arms it */
  for (int i = 0; i < 5 * 60; i++) sec_moving();
  CHECK(count_type(CM_ACT_SENSOR_FAULT) == 1);

  /* a live pulse re-arms; a fresh flat episode fires again */
  for (int i = 0; i < 30; i++) sec_moving_hr((uint16_t)(70 + (i & 1)));
  for (int i = 0; i < 5 * 60; i++) sec_moving();
  CHECK(count_type(CM_ACT_SENSOR_FAULT) == 2);
}

/* Delivery hardening (2026-08-29): ladder episodes carry a durable id
 * minted from a shell-persisted sequence; promotions keep it, cancel
 * echoes it, informational actions carry 0. */
static void test_episode_identity_and_carryover(void) {
  g_test = "episode_identity_and_carryover";
  cm_config cfg = test_cfg();
  setup(&cfg);
  warmup();
  core.episode_seq = 41; /* shell seed from persist */

  cm_manual_sos(&core, now_ms); drain();
  const cm_action *cd = find_type(CM_ACT_COUNTDOWN_START);
  CHECK(cd && cd->episode == 42);
  secs_still(7); /* SOS fuse (5 s) -> ALARM, same episode */
  const cm_action *al = find_type(CM_ACT_ALARM);
  CHECK(al && al->episode == 42);
  cm_user_ok(&core, now_ms); drain();
  const cm_action *cx = find_type(CM_ACT_ALERT_CANCELLED);
  CHECK(cx && cx->episode == 42);

  log_reset();
  cm_manual_sos(&core, now_ms); drain();
  cd = find_type(CM_ACT_COUNTDOWN_START);
  CHECK(cd && cd->episode == 43); /* new episode, new id */
  cm_user_ok(&core, now_ms); drain();

  log_reset();
  cm_set_charging(&core, 1, now_ms); drain();
  const cm_action *ch = find_type(CM_ACT_CHARGING_STARTED);
  CHECK(ch && ch->episode == 0); /* informational: no episode */
  cm_set_charging(&core, 0, now_ms); drain();
}

static void test_stage_remaining_seconds(void) {
  g_test = "stage_remaining_seconds";
  cm_config cfg = test_cfg();
  setup(&cfg);
  warmup();
  CHECK(cm_stage_remaining_s(&core, now_ms) == 0); /* idle */
  cm_manual_sos(&core, now_ms); drain();          /* SOS fuse 5 s */
  CHECK(cm_stage_remaining_s(&core, now_ms) == 5);
  secs_still(2);
  CHECK(cm_stage_remaining_s(&core, now_ms) == 3);
  cm_user_ok(&core, now_ms); drain();
  CHECK(cm_stage_remaining_s(&core, now_ms) == 0);
}

static void test_sensor_fault_holds_during_suspension(void) {
  g_test = "sensor_fault_holds_during_suspension";
  cm_config cfg = test_cfg();
  cfg.sensor_fault_after_min = 4;
  setup(&cfg);
  warmup();

  cm_suspend(&core, 3600, 0, now_ms);
  drain();
  log_reset();
  for (int i = 0; i < 6 * 60; i++) sec_moving();
  CHECK(count_type(CM_ACT_SENSOR_FAULT) == 0);
}

/* Removal signature: motion right after the last pulse, then stillness —
 * this must go to the not-worn nag, never the alarm ladder. */
static void test_removal_goes_to_nag_not_ladder(void) {
  g_test = "removal_goes_to_nag_not_ladder";
  cm_config cfg = test_cfg();
  cfg.notworn_after_min = 3;
  setup(&cfg);
  warmup();

  for (int i = 0; i < 10; i++) sec_moving(); /* unbuckle, set on the table */
  mins_still(2);
  CHECK(count_type(CM_ACT_HR_BURST_ON) == 0);   /* no silent hunt */
  CHECK(count_type(CM_ACT_CHECKIN_START) == 0); /* no ladder */
  CHECK(count_type(CM_ACT_NOTWORN_NAG) == 0);   /* not yet: under 3 min */

  mins_still(2);
  CHECK(count_type(CM_ACT_NOTWORN_NAG) == 1);   /* nag at ~3 min */
  CHECK(count_type(CM_ACT_CHECKIN_START) == 0);
  CHECK(count_type(CM_ACT_ALARM) == 0);

  mins_still(10);                               /* once per episode */
  CHECK(count_type(CM_ACT_NOTWORN_NAG) == 1);
  CHECK(count_type(CM_ACT_ALARM) == 0);
}

/* On the charger = deliberate off-wrist: total silence while plugged,
 * fresh baselines on unplug — the T4-family behavior, but automatic. */
static void test_charging_hold(void) {
  g_test = "charging_hold";
  cm_config cfg = test_cfg();
  setup(&cfg);
  warmup();

  cm_set_charging(&core, 1, now_ms);
  drain();
  CHECK(count_type(CM_ACT_CHARGING_STARTED) == 1);
  log_reset();

  mins_still(60); /* no pulse, no motion, on charger: nothing may fire */
  CHECK(log_count == 0);

  cm_set_charging(&core, 0, now_ms);
  drain();
  CHECK(count_type(CM_ACT_CHARGING_ENDED) == 1);
  log_reset();

  /* baselines reset: wearer puts the watch back on — no instant triggers
   * (an unplugged-and-abandoned watch still earns the ladder later, by
   * the same arrest-vs-removal rules as any other signal loss) */
  for (int i = 0; i < 120; i++) {
    if (i % 20 == 0) sec_still_hr(66); else sec_still();
  }
  CHECK(count_type(CM_ACT_CHECKIN_START) == 0);
  CHECK(count_type(CM_ACT_NOTWORN_NAG) == 0);
  CHECK(count_type(CM_ACT_HR_BURST_ON) == 0);
}

/* Docking mid-alert behaves like a suspension: pre-alarm stages cancel,
 * a latched ALARM survives. */
static void test_charging_cancels_checkin_not_alarm(void) {
  g_test = "charging_cancels_checkin_not_alarm";
  cm_config cfg = test_cfg();
  setup(&cfg);
  warmup();

  secs_still(62); /* pulse ladder reaches CHECKIN */
  CHECK(count_type(CM_ACT_CHECKIN_START) == 1);
  cm_set_charging(&core, 1, now_ms);
  drain();
  const cm_action *cc = find_type(CM_ACT_ALERT_CANCELLED);
  CHECK(cc != 0);
  CHECK(cc && cc->reason == CM_CANCEL_SUSPEND);
  CHECK(cm_current_stage(&core) == CM_STAGE_NONE);

  /* latched alarm: charging must NOT clear it */
  cm_config cfg2 = test_cfg();
  setup(&cfg2);
  warmup();
  cm_manual_sos(&core, now_ms);
  secs_still(7); /* SOS fuse (5 s) expires -> ALARM latches */
  CHECK(cm_current_stage(&core) == CM_STAGE_ALARM);
  log_reset();
  cm_set_charging(&core, 1, now_ms);
  drain();
  CHECK(cm_current_stage(&core) == CM_STAGE_ALARM);
  CHECK(count_type(CM_ACT_ALERT_CANCELLED) == 0);
}

/* Simulate the S4 field finding: after removal (or arrest) the firmware
 * keeps serving the last bpm with fresh events, bit-identical forever. */
static void sec_still_hr_frozen(uint16_t bpm) { sec_still_hr(bpm); }

/* S4 scenario: watch removed (handling motion near the freeze moment),
 * then frozen-82 readings continue — must go to the NAG, not the ladder,
 * and the frozen feed must not postpone the nag. */
static void test_frozen_pulse_removal_nags(void) {
  g_test = "frozen_pulse_removal_nags";
  cm_config cfg = test_cfg();
  cfg.notworn_after_min = 3;
  cfg.pulse_flat_after_s = 300;
  setup(&cfg);
  warmup();

  for (int i = 0; i < 10; i++) sec_moving();     /* unbuckle, set down */
  for (int i = 0; i < 4 * 60; i++) {             /* frozen feed continues */
    if (i % 2 == 0) sec_still_hr_frozen(82); else sec_still();
  }
  CHECK(count_type(CM_ACT_HR_BURST_ON) == 0);    /* no ladder */
  CHECK(count_type(CM_ACT_CHECKIN_START) == 0);
  CHECK(count_type(CM_ACT_ALARM) == 0);
  CHECK(count_type(CM_ACT_NOTWORN_NAG) == 1);    /* nag despite readings */
}

/* S4 scenario: arrest signature — wearer long still, the value freezes
 * while events keep coming, no motion near the freeze. The flat trigger
 * hunts; the hunt stays flat; the ladder must run. */
static void test_frozen_pulse_still_wearer_alarms(void) {
  g_test = "frozen_pulse_still_wearer_alarms";
  cm_config cfg = test_cfg();
  cfg.enabled[CM_DET_NONMOTION] = 0;  /* isolate the pulse path */
  cfg.enabled[CM_DET_NOTWORN] = 0;
  cfg.pulse_flat_after_s = 120;       /* compressed for the test */
  setup(&cfg);
  warmup();

  /* value freezes at 76 but readings keep arriving every 2 s */
  for (int i = 0; i < 121; i++) {
    if (i % 2 == 0) sec_still_hr_frozen(76); else sec_still();
  }
  CHECK(count_type(CM_ACT_HR_BURST_ON) == 1);    /* flat -> silent hunt */
  CHECK(count_type(CM_ACT_CHECKIN_START) == 0);
  log_reset();

  for (int i = 0; i < 31; i++) {                 /* hunt: still frozen */
    if (i % 2 == 0) sec_still_hr_frozen(76); else sec_still();
  }
  CHECK(count_type(CM_ACT_CHECKIN_START) == 1);  /* hunt failed: ladder */
  log_reset();

  sec_still_hr(78); /* a CHANGING value dismisses the check-in */
  const cm_action *cc = find_type(CM_ACT_ALERT_CANCELLED);
  CHECK(cc != 0);
  CHECK(cc && cc->reason == CM_CANCEL_PULSE);
}

/* Normal resting jitter (the worn_still lab stage: 74..81, changing
 * every few samples) must never trigger anything. */
static void test_jittering_rest_stays_silent(void) {
  g_test = "jittering_rest_stays_silent";
  cm_config cfg = test_cfg();
  cfg.pulse_flat_after_s = 120;
  cfg.pulse_lost_after_s = 150;  /* realistic: 60 s samples stay fresh */
  setup(&cfg);
  warmup();

  for (int i = 0; i < 20 * 60; i++) {  /* 20 min still, pulse jitters */
    if (i % 60 == 0) sec_still_hr((uint16_t)(76 + ((i / 60) % 3)));
    else sec_still();
  }
  CHECK(count_type(CM_ACT_HR_BURST_ON) == 0);
  CHECK(count_type(CM_ACT_CHECKIN_START) == 0);
  CHECK(count_type(CM_ACT_NOTWORN_NAG) == 0);
}

/* Field event 2026-08-27: a table tremor ran the impact ladder on a
 * watch lying off-wrist. With the pulse frozen beyond the flat window,
 * the impact ladder must stand down — tremors are not falls. */
static void test_impact_suppressed_offwrist(void) {
  g_test = "impact_suppressed_offwrist";
  cm_config cfg = test_cfg();
  cfg.enabled[CM_DET_PULSE] = 0;    /* isolate the impact detector */
  cfg.enabled[CM_DET_NOTWORN] = 0;
  cfg.enabled[CM_DET_NONMOTION] = 0;
  cfg.pulse_flat_after_s = 120;
  setup(&cfg);
  warmup();

  for (int i = 0; i < 3 * 60; i++) {  /* off-wrist: frozen feed */
    if (i % 2 == 0) sec_still_hr(82); else sec_still();
  }
  event_fall();                        /* table tremor */
  secs_still(66);                      /* settle + immobility pass */
  CHECK(count_type(CM_ACT_CHECKIN_START) == 0);
  CHECK(count_type(CM_ACT_ALARM) == 0);
}

/* Worker restarts while the watch lies off-wrist (build update on the
 * nightstand): no reading ever arrives, and the wearer must still be
 * told monitoring is blind — ever_pulse must not gate the nag. */
static void test_never_worn_since_restart_still_nags(void) {
  g_test = "never_worn_since_restart_still_nags";
  cm_config cfg = test_cfg();
  cfg.notworn_after_min = 3;
  setup(&cfg);           /* NO warmup: fresh restart, zero readings */

  mins_still(4);
  CHECK(count_type(CM_ACT_NOTWORN_NAG) == 1);
  CHECK(count_type(CM_ACT_CHECKIN_START) == 0);  /* never-worn: no ladder */
  CHECK(count_type(CM_ACT_ALARM) == 0);

  mins_still(10);        /* once per episode */
  CHECK(count_type(CM_ACT_NOTWORN_NAG) == 1);
}

/* S4 sensor lab: silent detector hold — the guided test deliberately
 * removes the watch, and nothing may fire during or right after. */
static void test_lab_hold_is_silent(void) {
  g_test = "lab_hold_is_silent";
  cm_config cfg = test_cfg();
  setup(&cfg);
  warmup();

  cm_set_lab_hold(&core, 1, now_ms);
  drain();
  log_reset();
  mins_still(15); /* strap loose, table, face-down... total silence */
  CHECK(log_count == 0);

  cm_set_lab_hold(&core, 0, now_ms);
  drain();
  log_reset();
  for (int i = 0; i < 120; i++) {  /* wearer puts it back on */
    if (i % 20 == 0) sec_still_hr(68); else sec_still();
  }
  CHECK(count_type(CM_ACT_CHECKIN_START) == 0);
  CHECK(count_type(CM_ACT_NOTWORN_NAG) == 0);
  CHECK(count_type(CM_ACT_HR_BURST_ON) == 0);
}

static void test_manual_sos(void) {
  g_test = "manual_sos";
  cm_config cfg = test_cfg();
  setup(&cfg);
  warmup();

  cm_manual_sos(&core, now_ms);
  drain();
  const cm_action *cd = find_type(CM_ACT_COUNTDOWN_START);
  CHECK(cd != 0);
  CHECK(cd && cd->detector == CM_DET_SOS);
  CHECK(cd && cd->seconds == 5);
  log_reset();

  secs_still(6);
  CHECK(count_type(CM_ACT_ALARM) == 1);
}

static void test_hr_unavailable_hardware(void) {
  g_test = "hr_unavailable_hardware";
  cm_config cfg = test_cfg();
  cfg.hr_available = 0; /* flint / gabbro */
  setup(&cfg);
  for (int i = 0; i < 5; i++) sec_moving();
  log_reset();

  /* no pulse machinery may ever fire */
  mins_still(10);
  CHECK(count_type(CM_ACT_HR_BURST_ON) == 0);
  CHECK(count_type(CM_ACT_NOTWORN_NAG) == 0);

  /* but non-motion still works (assumed worn) */
  mins_still(31); /* total > 40 min */
  CHECK(count_type(CM_ACT_CHECKIN_START) == 1);
}

int main(void) {
  test_defaults();
  test_impact_full_ladder();
  test_impact_cancelled_by_motion();
  test_impact_checkin_motion_dismiss();
  test_pulse_loss_full_ladder();
  test_pulse_returns_during_hunt();
  test_pulse_checkin_dismissed_by_pulse();
  test_pulse_user_cancel_snoozes();
  test_nonmotion_daytime();
  test_nonmotion_night_threshold();
  test_still_with_pulse_stays_silent();
  test_nonmotion_backstop_stale_pulse();
  test_notworn_nag_not_alarm();
  test_removal_goes_to_nag_not_ladder();
  test_scheduled_checkin();
  test_suspension_blocks_and_autoresumes();
  test_suspension_expiry();
  test_suspension_pulse_does_not_resume();
  test_suspension_grace_blocks_instant_resume();
  test_sensor_fault_fires_when_moving_without_pulse();
  test_sensor_fault_holds_during_suspension();
  test_episode_identity_and_carryover();
  test_stage_remaining_seconds();
  test_charging_hold();
  test_charging_cancels_checkin_not_alarm();
  test_frozen_pulse_removal_nags();
  test_frozen_pulse_still_wearer_alarms();
  test_jittering_rest_stays_silent();
  test_impact_suppressed_offwrist();
  test_never_worn_since_restart_still_nags();
  test_lab_hold_is_silent();
  test_manual_sos();
  test_hr_unavailable_hardware();

  printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
