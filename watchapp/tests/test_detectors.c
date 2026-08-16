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

/* establish a "worn, alive" baseline: motion + pulse */
static void warmup(void) {
  for (int i = 0; i < 5; i++) { sec_moving(); }
  for (int i = 0; i < 5; i++) { sec_still_hr(70); }
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

  /* perfectly still for 40 min on motion-only hardware */
  mins_still(40);
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
    if (i % 60 == 0) sec_still_hr(62); else sec_still();
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
    if (i % 60 == 0) sec_still_hr(60); else sec_still();
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

  /* wearer puts it back on: sustained motion -> auto-resume */
  for (int i = 0; i < 16; i++) sec_moving();
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

/* Pulse is NOT a resume signal: the optical sensor phantom-reads when the
 * watch lies face-down or is pressed against a surface. */
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

  /* sustained motion is the only trusted wear evidence */
  for (int i = 0; i < 16; i++) sec_moving();
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

  for (int i = 0; i < 50; i++) sec_moving();  /* wearer walks off, watch on */
  CHECK(count_type(CM_ACT_AUTO_RESUMED) == 0);

  for (int i = 0; i < 30; i++) sec_moving();  /* grace over: 15 s run resumes */
  CHECK(count_type(CM_ACT_AUTO_RESUMED) == 1);
  CHECK(cm_suspend_remaining_s(&core, now_ms) == 0);
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
  test_manual_sos();
  test_hr_unavailable_hardware();

  printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
