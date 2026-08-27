/* Pebble Cryonics Monitor — detector core implementation. See detectors.h. */
#include "detectors.h"

/* Wrap-safe elapsed time (valid for spans < 2^31 ms ~ 24 days). */
static uint32_t elapsed(uint32_t now, uint32_t since) { return now - since; }

static void emit(cm_core *c, uint8_t type, uint8_t det, uint8_t reason, uint16_t seconds) {
  if (c->q_len >= (uint8_t)(sizeof(c->q) / sizeof(c->q[0]))) {
    c->q_overflow = 1; /* shell surfaces this as a FAULT */
    return;
  }
  uint8_t idx = (uint8_t)((c->q_head + c->q_len) % (sizeof(c->q) / sizeof(c->q[0])));
  c->q[idx].type = type;
  c->q[idx].detector = det;
  c->q[idx].reason = reason;
  c->q[idx].seconds = seconds;
  c->q_len++;
}

int cm_next_action(cm_core *c, cm_action *out) {
  if (c->q_len == 0) return 0;
  *out = c->q[c->q_head];
  c->q_head = (uint8_t)((c->q_head + 1) % (sizeof(c->q) / sizeof(c->q[0])));
  c->q_len--;
  return 1;
}

void cm_config_defaults(cm_config *cfg) {
  for (int i = 0; i < CM_DET_COUNT; i++) cfg->enabled[i] = 1;
  /* Scheduled check-in is opt-in: it is the only detector that demands
   * periodic attention from the wearer even when nothing is wrong. */
  cfg->enabled[CM_DET_CHECKIN] = 0;
  cfg->hr_available = 1;
  cfg->motion_jerk_mg = 60;

  cfg->pulse_lost_after_s = 150; /* must exceed 2x the normal HR sample period */
  cfg->pulse_hunt_s = 30;
  cfg->pulse_still_s = 20;
  cfg->pulse_min_bpm = 25;
  cfg->pulse_worn_grace_min = 10;
  cfg->pulse_snooze_min = 10;

  cfg->freefall_below_mg = 300;
  cfg->impact_above_mg = 2400;
  cfg->freefall_window_ms = 1500;
  cfg->crash_above_mg = 3800;
  cfg->impact_settle_s = 5;
  cfg->impact_immobile_s = 60;

  cfg->nonmotion_day_min = 40;
  cfg->nonmotion_night_min = 90;
  cfg->night_start_hour = 23;
  cfg->night_end_hour = 7;

  cfg->notworn_after_min = 3;

  cfg->checkin_interval_min = 240;
  cfg->checkin_remind_min = 5;
  cfg->checkin_grace_min = 15;

  cfg->checkin_ui_s = 30;
  cfg->countdown_s = 30;
  cfg->countdown_impact_s = 20;
  cfg->countdown_sos_s = 5;

  cfg->resume_motion_s = 15;
  cfg->resume_grace_s = 60;

  cfg->pulse_proof_min = 5;
  cfg->removal_window_s = 45;
}

void cm_init(cm_core *c, const cm_config *cfg, uint32_t now_ms) {
  /* Zero everything, then set config + time baselines. */
  uint8_t *p = (uint8_t *)c;
  for (uint32_t i = 0; i < sizeof(*c); i++) p[i] = 0;
  c->cfg = *cfg;
  c->now_ms = now_ms;
  c->last_motion_ms = now_ms;
  c->last_pulse_ms = now_ms;
  c->checkin_due_ms = now_ms + (uint32_t)cfg->checkin_interval_min * 60000u;
  c->nonmotion_armed = 1;
}

/* ---- integer sqrt (for accel magnitude) ---- */
static uint16_t isqrt32(uint32_t v) {
  uint32_t r = 0, bit = 1uL << 30;
  while (bit > v) bit >>= 2;
  while (bit) {
    if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; }
    else r >>= 1;
    bit >>= 2;
  }
  return (uint16_t)r;
}

static uint32_t mag2_of(const cm_accel_sample *s) {
  int32_t x = s->x, y = s->y, z = s->z;
  return (uint32_t)(x * x + y * y + z * z);
}

static int pulse_alert_active(const cm_core *c) {
  return c->stage != CM_STAGE_NONE && c->stage_det == CM_DET_PULSE;
}

static void end_pulse_machinery(cm_core *c) {
  if (c->pulse_phase == 1 || pulse_alert_active(c)) emit(c, CM_ACT_HR_BURST_OFF, CM_DET_PULSE, 0, 0);
  c->pulse_phase = 0;
}

/* Cancel any active alert (does not touch alarm latch unless from user/suspend). */
static void cancel_alert(cm_core *c, uint8_t reason) {
  if (c->stage == CM_STAGE_NONE) return;
  uint8_t det = c->stage_det;
  if (det == CM_DET_PULSE) end_pulse_machinery(c);
  c->stage = CM_STAGE_NONE;
  emit(c, CM_ACT_ALERT_CANCELLED, det, reason, 0);
  if (det == CM_DET_NONMOTION) c->nonmotion_armed = 0; /* re-arm on next motion */
  if (det == CM_DET_PULSE) {
    c->pulse_snooze_until_ms = c->now_ms + (uint32_t)c->cfg.pulse_snooze_min * 60000u;
    c->pulse_snoozed = 1;
  }
  if (det == CM_DET_CHECKIN) {
    /* answered/cancelled: schedule next round */
    c->checkin_due_ms = c->now_ms + (uint32_t)c->cfg.checkin_interval_min * 60000u;
    c->checkin_reminded = 0;
  }
}

static void start_checkin_stage(cm_core *c, uint8_t det) {
  c->stage = CM_STAGE_CHECKIN;
  c->stage_det = det;
  c->stage_start_ms = c->now_ms;
  emit(c, CM_ACT_CHECKIN_START, det, 0, c->cfg.checkin_ui_s);
}

static uint16_t countdown_len(const cm_core *c, uint8_t det) {
  if (det == CM_DET_IMPACT) return c->cfg.countdown_impact_s;
  if (det == CM_DET_SOS) return c->cfg.countdown_sos_s;
  return c->cfg.countdown_s;
}

static void start_countdown_stage(cm_core *c, uint8_t det) {
  c->stage = CM_STAGE_COUNTDOWN;
  c->stage_det = det;
  c->stage_start_ms = c->now_ms;
  emit(c, CM_ACT_COUNTDOWN_START, det, 0, countdown_len(c, det));
}

/* ---- motion ---- */
static void note_motion(cm_core *c) {
  c->last_motion_ms = c->now_ms;
  c->motion_this_second = 1;
  c->nonmotion_armed = 1;
  c->notworn_nagged = 0;

  /* Motion auto-dismisses the CHECKIN stage — except scheduled check-ins,
   * which require a deliberate button press, and except SOS. */
  if (c->stage == CM_STAGE_CHECKIN &&
      c->stage_det != CM_DET_CHECKIN && c->stage_det != CM_DET_SOS) {
    cancel_alert(c, CM_CANCEL_MOTION);
  }
  /* Motion during a pulse hunt: not still any more — stand down silently. */
  if (c->pulse_phase == 1) end_pulse_machinery(c);
  /* Motion in the post-impact settle window is handled in cm_tick via
   * last_motion_ms; nothing to do here. */
}

void cm_accel_feed(cm_core *c, const cm_accel_sample *s, uint32_t n, uint32_t now_ms) {
  c->now_ms = now_ms;
  for (uint32_t i = 0; i < n; i++) {
    if (s[i].did_vibrate) { c->have_prev_mag = 0; continue; }
    uint32_t m2 = mag2_of(&s[i]);
    uint16_t mag = isqrt32(m2);

    /* movement = magnitude jerk between consecutive samples */
    if (c->have_prev_mag) {
      uint16_t d = (mag > c->prev_mag) ? (uint16_t)(mag - c->prev_mag)
                                       : (uint16_t)(c->prev_mag - mag);
      if (d >= c->cfg.motion_jerk_mg) note_motion(c);
    }
    c->prev_mag = mag;
    c->have_prev_mag = 1;

    if (c->suspended || !c->cfg.enabled[CM_DET_IMPACT]) continue;
    if (c->stage != CM_STAGE_NONE) continue;

    /* impact detection */
    if (c->impact_phase < 2) {
      if (mag < c->cfg.freefall_below_mg) {
        c->impact_phase = 1;
        c->freefall_ms = now_ms;
      } else if (c->impact_phase == 1 && mag > c->cfg.impact_above_mg &&
                 elapsed(now_ms, c->freefall_ms) <= c->cfg.freefall_window_ms) {
        c->impact_phase = 2;               /* freefall -> impact */
        c->impact_ms = now_ms;
      } else if (mag > c->cfg.crash_above_mg) {
        c->impact_phase = 2;               /* single high-G shock */
        c->impact_ms = now_ms;
      } else if (c->impact_phase == 1 &&
                 elapsed(now_ms, c->freefall_ms) > c->cfg.freefall_window_ms) {
        c->impact_phase = 0;               /* freefall window expired */
      }
    }
  }
}

void cm_hr_feed(cm_core *c, uint16_t bpm, uint32_t now_ms) {
  c->now_ms = now_ms;
  if (!c->cfg.hr_available) return;
  if (bpm >= c->cfg.pulse_min_bpm) {
    c->last_pulse_ms = now_ms;
    c->ever_pulse = 1;
    c->pulse_snoozed = 0;
    c->notworn_nagged = 0;
    /* Returning pulse silently ends a hunt, and auto-dismisses a
     * pulse-loss CHECKIN stage. Countdown stage stays: explicit cancel only. */
    if (c->pulse_phase == 1) end_pulse_machinery(c);
    if (c->stage == CM_STAGE_CHECKIN && c->stage_det == CM_DET_PULSE) {
      cancel_alert(c, CM_CANCEL_PULSE);
    }
  }
}

static int is_night(const cm_core *c) {
  uint8_t s = c->cfg.night_start_hour, e = c->cfg.night_end_hour;
  if (s == e) return 0;
  if (s < e) return c->hour >= s && c->hour < e;
  return c->hour >= s || c->hour < e; /* window crosses midnight */
}

static int worn_recently(const cm_core *c) {
  if (c->cfg.hr_available) {
    return c->ever_pulse &&
           elapsed(c->now_ms, c->last_pulse_ms) <=
               (uint32_t)c->cfg.pulse_worn_grace_min * 60000u;
  }
  return 1; /* no wear sensor: assume worn (documented limitation) */
}

static void tick_suspension(cm_core *c) {
  if (!c->suspended) return;

  if ((int32_t)(c->suspend_until_ms - c->now_ms) <= 0) {
    c->suspended = 0;
    /* fresh baselines: no instant triggers on resume */
    c->last_motion_ms = c->now_ms;
    c->last_pulse_ms = c->now_ms;
    c->impact_phase = 0;
    emit(c, CM_ACT_SUSPEND_EXPIRED, 0, 0, 0);
    return;
  }

  if (c->suspend_auto_resume) {
    /* Arming delay: the wearer is usually still wearing (or handling) the
     * watch in the first moments of a suspension — those signals must not
     * resume it. Pulse is deliberately NOT a resume signal at all: the
     * optical sensor phantom-reads when pressed against a surface, so
     * sustained motion is the only trusted "back on the wrist" evidence. */
    if (elapsed(c->now_ms, c->suspend_start_ms) <
        (uint32_t)c->cfg.resume_grace_s * 1000u) {
      c->suspend_motion_run_s = 0;
      return;
    }
    if (c->motion_this_second) c->suspend_motion_run_s++;
    else c->suspend_motion_run_s = 0;

    if (c->suspend_motion_run_s >= c->cfg.resume_motion_s) {
      c->suspended = 0;
      c->last_motion_ms = c->now_ms;
      c->last_pulse_ms = c->now_ms;
      c->impact_phase = 0;
      emit(c, CM_ACT_AUTO_RESUMED, 0, 0, 0);
    }
  }
}

static void tick_ladder(cm_core *c) {
  if (c->stage == CM_STAGE_CHECKIN &&
      elapsed(c->now_ms, c->stage_start_ms) >= (uint32_t)c->cfg.checkin_ui_s * 1000u) {
    start_countdown_stage(c, c->stage_det);
  } else if (c->stage == CM_STAGE_COUNTDOWN &&
             elapsed(c->now_ms, c->stage_start_ms) >=
                 (uint32_t)countdown_len(c, c->stage_det) * 1000u) {
    c->stage = CM_STAGE_ALARM;
    c->stage_start_ms = c->now_ms;
    if (c->stage_det == CM_DET_PULSE) emit(c, CM_ACT_HR_BURST_OFF, CM_DET_PULSE, 0, 0);
    emit(c, CM_ACT_ALARM, c->stage_det, 0, 0);
  }
}

/* Removal vs. arrest: a dead wearer does not move after the pulse stops;
 * removing a watch necessarily moves it. Evaluated only at hunt-trigger
 * time (wearer still, pulse long gone), so "motion shortly AFTER the last
 * valid pulse, then stillness" is the removal signature — such an episode
 * belongs to the not-worn nag, not the alarm ladder. */
static int removal_suspected(const cm_core *c) {
  return (int32_t)(c->last_motion_ms - c->last_pulse_ms) > 0 &&
         (uint32_t)(c->last_motion_ms - c->last_pulse_ms) <=
             (uint32_t)c->cfg.removal_window_s * 1000u;
}

static void tick_pulse(cm_core *c) {
  if (!c->cfg.enabled[CM_DET_PULSE] || !c->cfg.hr_available || !c->ever_pulse) return;
  if (c->stage != CM_STAGE_NONE || c->suspended) return;
  if (c->pulse_snoozed && (int32_t)(c->pulse_snooze_until_ms - c->now_ms) > 0) return;

  uint32_t since_pulse = elapsed(c->now_ms, c->last_pulse_ms);
  uint32_t since_motion = elapsed(c->now_ms, c->last_motion_ms);

  if (c->pulse_phase == 0) {
    if (worn_recently(c) &&
        since_pulse >= (uint32_t)c->cfg.pulse_lost_after_s * 1000u &&
        since_motion >= (uint32_t)c->cfg.pulse_still_s * 1000u) {
      if (removal_suspected(c)) return; /* not-worn nag owns this episode */
      c->pulse_phase = 1;
      c->hunt_start_ms = c->now_ms;
      emit(c, CM_ACT_HR_BURST_ON, CM_DET_PULSE, 0, c->cfg.pulse_hunt_s);
    }
  } else if (c->pulse_phase == 1) {
    if (elapsed(c->now_ms, c->hunt_start_ms) >= (uint32_t)c->cfg.pulse_hunt_s * 1000u) {
      /* hunted, still nothing: escalate (burst stays on so a returning
       * pulse can still auto-dismiss the CHECKIN stage) */
      c->pulse_phase = 0;
      start_checkin_stage(c, CM_DET_PULSE);
    }
  }
}

static void tick_impact(cm_core *c) {
  if (c->impact_phase != 2 || c->suspended || c->stage != CM_STAGE_NONE) return;

  uint32_t settle_end = c->impact_ms + (uint32_t)c->cfg.impact_settle_s * 1000u;
  if ((int32_t)(c->now_ms - settle_end) < 0) return; /* still settling */

  if ((int32_t)(c->last_motion_ms - settle_end) >= 0) {
    c->impact_phase = 0; /* deliberate motion after settle: silent cancel */
    return;
  }
  if (elapsed(c->now_ms, settle_end) >= (uint32_t)c->cfg.impact_immobile_s * 1000u) {
    c->impact_phase = 0;
    start_checkin_stage(c, CM_DET_IMPACT);
  }
}

static void tick_nonmotion(cm_core *c) {
  if (!c->cfg.enabled[CM_DET_NONMOTION] || c->suspended) return;
  if (c->stage != CM_STAGE_NONE || !c->nonmotion_armed) return;
  if (!worn_recently(c)) return; /* off-wrist is the not-worn detector's job */
  /* A live pulse is proof of life: stillness alone (sleep, meditation,
   * TV) must never ping the wearer on HR hardware. Non-motion remains
   * only as the backstop for a silently failing HR sensor — the band
   * where the pulse went stale but the worn grace has not lapsed. */
  if (c->cfg.hr_available && c->ever_pulse &&
      elapsed(c->now_ms, c->last_pulse_ms) <
          (uint32_t)c->cfg.pulse_proof_min * 60000u) return;

  uint16_t mins = is_night(c) ? c->cfg.nonmotion_night_min : c->cfg.nonmotion_day_min;
  if (elapsed(c->now_ms, c->last_motion_ms) >= (uint32_t)mins * 60000u) {
    c->nonmotion_armed = 0;
    start_checkin_stage(c, CM_DET_NONMOTION);
  }
}

static void tick_checkin(cm_core *c) {
  if (!c->cfg.enabled[CM_DET_CHECKIN] || c->suspended) return;
  if (c->stage != CM_STAGE_NONE) return;

  uint32_t remind_at =
      c->checkin_due_ms - (uint32_t)c->cfg.checkin_remind_min * 60000u;
  if (!c->checkin_reminded && (int32_t)(c->now_ms - remind_at) >= 0) {
    c->checkin_reminded = 1;
    uint32_t until_due = (int32_t)(c->checkin_due_ms - c->now_ms) > 0
                             ? (c->checkin_due_ms - c->now_ms) / 1000u : 0;
    emit(c, CM_ACT_CHECKIN_REMINDER, CM_DET_CHECKIN, 0, (uint16_t)until_due);
  }
  uint32_t deadline = c->checkin_due_ms + (uint32_t)c->cfg.checkin_grace_min * 60000u;
  if ((int32_t)(c->now_ms - deadline) >= 0) {
    start_checkin_stage(c, CM_DET_CHECKIN);
  }
}

static void tick_notworn(cm_core *c) {
  if (!c->cfg.enabled[CM_DET_NOTWORN] || !c->cfg.hr_available || !c->ever_pulse) return;
  if (c->suspended || c->notworn_nagged || c->stage != CM_STAGE_NONE) return;
  if (c->pulse_phase != 0) return; /* a pulse hunt is running: let it conclude */

  uint32_t th = (uint32_t)c->cfg.notworn_after_min * 60000u;
  if (elapsed(c->now_ms, c->last_pulse_ms) >= th &&
      elapsed(c->now_ms, c->last_motion_ms) >= th) {
    c->notworn_nagged = 1;
    emit(c, CM_ACT_NOTWORN_NAG, CM_DET_NOTWORN, 0, 0);
  }
}

void cm_tick(cm_core *c, uint32_t now_ms, uint8_t local_hour) {
  c->now_ms = now_ms;
  c->hour = local_hour;

  tick_suspension(c);
  tick_ladder(c);
  if (!c->suspended && !c->charging && !c->lab_hold) {
    tick_impact(c);
    tick_pulse(c);
    tick_nonmotion(c);
    tick_checkin(c);
    tick_notworn(c);
  }
  c->motion_this_second = 0;
}

void cm_user_ok(cm_core *c, uint32_t now_ms) {
  c->now_ms = now_ms;
  /* A button press is proof of life. */
  c->last_motion_ms = now_ms;
  c->nonmotion_armed = 1;

  if (c->stage == CM_STAGE_ALARM) {
    uint8_t det = c->stage_det;
    c->stage = CM_STAGE_NONE;
    emit(c, CM_ACT_ALERT_CANCELLED, det, CM_CANCEL_USER, 0);
    if (det == CM_DET_PULSE) {
      c->pulse_snooze_until_ms = now_ms + (uint32_t)c->cfg.pulse_snooze_min * 60000u;
      c->pulse_snoozed = 1;
    }
    if (det == CM_DET_CHECKIN) {
      c->checkin_due_ms = now_ms + (uint32_t)c->cfg.checkin_interval_min * 60000u;
      c->checkin_reminded = 0;
    }
    return;
  }
  if (c->stage != CM_STAGE_NONE) {
    cancel_alert(c, CM_CANCEL_USER);
    return;
  }
  /* No alert active: treat as an early scheduled check-in. */
  if (c->cfg.enabled[CM_DET_CHECKIN]) {
    c->checkin_due_ms = now_ms + (uint32_t)c->cfg.checkin_interval_min * 60000u;
    c->checkin_reminded = 0;
  }
}

void cm_manual_sos(cm_core *c, uint32_t now_ms) {
  c->now_ms = now_ms;
  if (c->stage == CM_STAGE_ALARM) return;
  if (c->stage != CM_STAGE_NONE) cancel_alert(c, CM_CANCEL_USER);
  start_countdown_stage(c, CM_DET_SOS);
}

void cm_suspend(cm_core *c, uint32_t seconds, uint8_t auto_resume, uint32_t now_ms) {
  c->now_ms = now_ms;
  if (c->stage != CM_STAGE_NONE && c->stage != CM_STAGE_ALARM) {
    cancel_alert(c, CM_CANCEL_SUSPEND);
  }
  c->suspended = 1;
  c->suspend_start_ms = now_ms;
  c->suspend_until_ms = now_ms + seconds * 1000u;
  c->suspend_auto_resume = auto_resume;
  c->suspend_motion_run_s = 0;
  c->impact_phase = 0;
  c->pulse_phase = 0;
  emit(c, CM_ACT_SUSPEND_STARTED, 0, 0,
       (uint16_t)(seconds > 65535u ? 65535u : seconds));
}

void cm_set_charging(cm_core *c, int charging, uint32_t now_ms) {
  uint8_t on = charging ? 1 : 0;
  if (on == c->charging) return;
  c->now_ms = now_ms;
  c->charging = on;
  if (on) {
    /* Putting the watch on the charger is a deliberate act: treat it as
     * an implicit suspension. Pre-alarm stages cancel like a suspension
     * would; a latched ALARM stays latched — charging must never clear
     * an alarm someone may already be responding to. */
    if (c->stage != CM_STAGE_NONE && c->stage != CM_STAGE_ALARM) {
      cancel_alert(c, CM_CANCEL_SUSPEND);
    }
    c->impact_phase = 0;
    c->pulse_phase = 0;
    emit(c, CM_ACT_CHARGING_STARTED, 0, 0, 1);
  } else {
    /* fresh baselines: the stillness and pulse-absence accumulated on
     * the charger must not fire the instant it comes off */
    c->last_motion_ms = now_ms;
    c->last_pulse_ms = now_ms;
    c->impact_phase = 0;
    c->notworn_nagged = 0;
    c->nonmotion_armed = 1;
    emit(c, CM_ACT_CHARGING_ENDED, 0, 0, 0);
  }
}

/* Guided sensor test (M0 S4): the wearer will deliberately loosen the
 * strap and put the watch on a table — the detectors must sit this out.
 * Same rules as the charging hold, but silent: the lab UI on the phone
 * is already narrating, and no state change should reach contacts. */
void cm_set_lab_hold(cm_core *c, int hold, uint32_t now_ms) {
  uint8_t on = hold ? 1 : 0;
  if (on == c->lab_hold) return;
  c->now_ms = now_ms;
  c->lab_hold = on;
  if (on) {
    if (c->stage != CM_STAGE_NONE && c->stage != CM_STAGE_ALARM) {
      cancel_alert(c, CM_CANCEL_SUSPEND);
    }
    c->impact_phase = 0;
    c->pulse_phase = 0;
  } else {
    c->last_motion_ms = now_ms;
    c->last_pulse_ms = now_ms;
    c->impact_phase = 0;
    c->notworn_nagged = 0;
    c->nonmotion_armed = 1;
  }
}

void cm_resume(cm_core *c, uint32_t now_ms) {
  c->now_ms = now_ms;
  if (!c->suspended) return;
  c->suspended = 0;
  c->last_motion_ms = now_ms;
  c->last_pulse_ms = now_ms;
  c->impact_phase = 0;
  emit(c, CM_ACT_AUTO_RESUMED, 0, 0, 0);
}

cm_stage cm_current_stage(const cm_core *c) { return (cm_stage)c->stage; }

uint32_t cm_suspend_remaining_s(const cm_core *c, uint32_t now_ms) {
  if (!c->suspended) return 0;
  int32_t d = (int32_t)(c->suspend_until_ms - now_ms);
  return d > 0 ? (uint32_t)d / 1000u : 0;
}

uint32_t cm_checkin_due_in_s(const cm_core *c, uint32_t now_ms) {
  int32_t d = (int32_t)(c->checkin_due_ms - now_ms);
  return d > 0 ? (uint32_t)d / 1000u : 0;
}
