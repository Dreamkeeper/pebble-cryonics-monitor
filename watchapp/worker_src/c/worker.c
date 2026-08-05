/*
 * Background worker: 24/7 sensing + detection.
 *
 * Runs the detector core against the accelerometer and (where present) the
 * heart rate sensor. The worker cannot vibrate, show UI, or send AppMessage
 * (M0 spike S3 pending) — any action needing those launches the foreground
 * app via worker_launch_app() with the action parked in persist storage.
 *
 * Memory budget: 10.5 kB. Keep static state minimal; no heap use beyond
 * what the event subscriptions allocate (~2 kB for HealthService).
 */
#include <pebble_worker.h>
#include "../../src/core/detectors.h"
#include "../../src/core/protocol.h"

static cm_core s_core;
static uint8_t s_hr_burst_active;
static uint16_t s_heartbeat_countdown = CM_HEARTBEAT_INTERVAL_S;
static DataLoggingSessionRef s_log_session;

/* Heartbeat record for DataLogging (phone-side watchdog + audit trail). */
typedef struct __attribute__((packed)) {
  uint32_t epoch_s;
  uint8_t stage;
  uint8_t battery_pct;
  uint8_t last_bpm;
  uint8_t suspended;
} cm_heartbeat_rec;

static uint32_t now_ms(void) {
  time_t s;
  uint16_t ms;
  time_ms(&s, &ms);
  return (uint32_t)s * 1000u + ms;
}

static void push_status_to_app(void) {
  AppWorkerMessage m = {
    .data0 = (uint16_t)cm_current_stage(&s_core),
    .data1 = 0, /* TODO: cache last bpm */
    .data2 = (uint16_t)(cm_suspend_remaining_s(&s_core, now_ms()) / 60u),
  };
  app_worker_send_message(WMSG_STATUS, &m);
}

/* Park the action for the foreground app and wake it. */
static void hand_to_app(const cm_action *a) {
  persist_write_data(PK_PENDING_ACTION, a, sizeof(*a));
  AppWorkerMessage m = {
    .data0 = a->type, .data1 = a->detector, .data2 = a->seconds,
  };
  app_worker_send_message(WMSG_ACTION, &m); /* no-op if app not running */
  worker_launch_app();
}

static void set_hr_burst(bool on) {
#if defined(PBL_HEALTH)
  health_service_set_heart_rate_sample_period(
      on ? CM_HR_PERIOD_BURST_S : CM_HR_PERIOD_NORMAL_S);
#endif
  s_hr_burst_active = on ? 1 : 0;
}

static void drain_actions(void) {
  cm_action a;
  while (cm_next_action(&s_core, &a)) {
    switch (a.type) {
      case CM_ACT_HR_BURST_ON:  set_hr_burst(true);  break;
      case CM_ACT_HR_BURST_OFF: set_hr_burst(false); break;
      /* Everything else needs vibration/UI/phone -> foreground app. */
      case CM_ACT_CHECKIN_START:
      case CM_ACT_COUNTDOWN_START:
      case CM_ACT_ALARM:
      case CM_ACT_ALERT_CANCELLED:
      case CM_ACT_NOTWORN_NAG:
      case CM_ACT_CHECKIN_REMINDER:
      case CM_ACT_SUSPEND_EXPIRED:
      case CM_ACT_AUTO_RESUMED:
        hand_to_app(&a);
        break;
      default: break;
    }
  }
  if (s_core.q_overflow) {
    s_core.q_overflow = 0;
    /* Surfaced to phone as FAULT via the next heartbeat. TODO: flag field. */
  }
}

static void accel_handler(AccelData *data, uint32_t num_samples) {
  /* AccelData layout matches cm_accel_sample closely; repack (int16 x/y/z). */
  cm_accel_sample s[25];
  uint32_t n = num_samples > 25 ? 25 : num_samples;
  for (uint32_t i = 0; i < n; i++) {
    s[i].x = data[i].x;
    s[i].y = data[i].y;
    s[i].z = data[i].z;
    s[i].did_vibrate = data[i].did_vibrate ? 1 : 0;
  }
  cm_accel_feed(&s_core, s, n, now_ms());
  drain_actions();
}

#if defined(PBL_HEALTH)
static void health_handler(HealthEventType event, void *context) {
  if (event == HealthEventHeartRateUpdate || event == HealthEventSignificantUpdate) {
    HealthValue bpm = health_service_peek_current_value(HealthMetricHeartRateRawBPM);
    cm_hr_feed(&s_core, bpm > 0 ? (uint16_t)bpm : 0, now_ms());
    drain_actions();
  }
}
#endif

static void log_heartbeat(void) {
  cm_heartbeat_rec rec = {
    .epoch_s = (uint32_t)time(NULL),
    .stage = (uint8_t)cm_current_stage(&s_core),
    .battery_pct = battery_state_service_peek().charge_percent,
    .last_bpm = 0, /* TODO: cache last reading */
    .suspended = s_core.suspended,
  };
  data_logging_log(s_log_session, &rec, 1);
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  cm_tick(&s_core, now_ms(), (uint8_t)tick_time->tm_hour);
  drain_actions();

  if (--s_heartbeat_countdown == 0) {
    s_heartbeat_countdown = CM_HEARTBEAT_INTERVAL_S;
    log_heartbeat();
  }
}

static void worker_message_handler(uint16_t type, AppWorkerMessage *m) {
  switch (type) {
    case WMSG_USER_OK: cm_user_ok(&s_core, now_ms()); break;
    case WMSG_SUSPEND:
      cm_suspend(&s_core, (uint32_t)m->data0 * 60u, (uint8_t)m->data1, now_ms());
      persist_write_int(PK_SUSPEND_UNTIL, (int)(time(NULL) + m->data0 * 60));
      persist_write_int(PK_SUSPEND_AUTORESUME, m->data1);
      break;
    case WMSG_RESUME: cm_resume(&s_core, now_ms()); break;
    case WMSG_SOS: cm_manual_sos(&s_core, now_ms()); break;
    case WMSG_STATUS_REQ: push_status_to_app(); break;
    default: break;
  }
  drain_actions();
}

static void load_config(cm_config *cfg) {
  cm_config_defaults(cfg);
  if (persist_exists(PK_CONFIG) &&
      persist_get_size(PK_CONFIG) == (int)sizeof(*cfg)) {
    persist_read_data(PK_CONFIG, cfg, sizeof(*cfg));
  }
#if !defined(PBL_HEALTH)
  cfg->hr_available = 0;
#else
  if (!health_service_metric_accessible(HealthMetricHeartRateBPM,
                                        time(NULL), time(NULL))) {
    cfg->hr_available = 0; /* flint / gabbro */
  }
#endif
}

static void restore_suspension(void) {
  if (!persist_exists(PK_SUSPEND_UNTIL)) return;
  time_t until = (time_t)persist_read_int(PK_SUSPEND_UNTIL);
  time_t now = time(NULL);
  if (until > now) {
    cm_suspend(&s_core, (uint32_t)(until - now),
               (uint8_t)persist_read_int(PK_SUSPEND_AUTORESUME), now_ms());
    drain_actions();
  }
}

static void init(void) {
  cm_config cfg;
  load_config(&cfg);
  cm_init(&s_core, &cfg, now_ms());
  restore_suspension();

  accel_service_set_sampling_rate(ACCEL_SAMPLING_25HZ);
  accel_data_service_subscribe(25, accel_handler); /* one callback per second */

#if defined(PBL_HEALTH)
  if (cfg.hr_available) {
    health_service_events_subscribe(health_handler, NULL); /* costs ~2 kB heap */
    set_hr_burst(false);
  }
#endif

  tick_timer_service_subscribe(SECOND_UNIT, tick_handler);
  app_worker_message_subscribe(worker_message_handler);

  s_log_session = data_logging_create(
      /* tag */ 0xC201, DATA_LOGGING_BYTE_ARRAY, sizeof(cm_heartbeat_rec),
      /* resume */ true);
}

static void deinit(void) {
  data_logging_finish(s_log_session);
  app_worker_message_unsubscribe();
  tick_timer_service_unsubscribe();
#if defined(PBL_HEALTH)
  set_hr_burst(false); /* never leave the HRM in burst mode */
  health_service_events_unsubscribe();
#endif
  accel_data_service_unsubscribe();
}

int main(void) {
  init();
  worker_event_loop();
  deinit();
}
