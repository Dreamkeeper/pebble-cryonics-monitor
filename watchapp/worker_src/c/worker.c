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
static uint16_t s_last_bpm;            /* last raw HR reading (0 = none) */
static uint16_t s_drill_countdown;     /* S1 latency drill: seconds to fire */
static DataLoggingSessionRef s_log_session;

/* ---- debug mode (toggled from the phone; view with `pebble logs`) ---- */
static uint8_t s_debug;
static uint16_t s_dbg_motion_events;   /* per-minute counters */
static uint16_t s_dbg_last_mag;
static uint16_t s_dbg_last_bpm;
static uint16_t s_dbg_hr_updates;

#define DLOG(...) do { \
    if (s_debug) APP_LOG(APP_LOG_LEVEL_DEBUG, __VA_ARGS__); \
  } while (0)

static const char *action_name(uint8_t t) {
  switch (t) {
    case CM_ACT_HR_BURST_ON:      return "HR_BURST_ON";
    case CM_ACT_HR_BURST_OFF:     return "HR_BURST_OFF";
    case CM_ACT_CHECKIN_START:    return "CHECKIN_START";
    case CM_ACT_COUNTDOWN_START:  return "COUNTDOWN_START";
    case CM_ACT_ALERT_CANCELLED:  return "ALERT_CANCELLED";
    case CM_ACT_ALARM:            return "ALARM";
    case CM_ACT_NOTWORN_NAG:      return "NOTWORN_NAG";
    case CM_ACT_CHECKIN_REMINDER: return "CHECKIN_REMINDER";
    case CM_ACT_SUSPEND_STARTED:  return "SUSPEND_STARTED";
    case CM_ACT_SUSPEND_EXPIRED:  return "SUSPEND_EXPIRED";
    case CM_ACT_AUTO_RESUMED:     return "AUTO_RESUMED";
    default:                      return "?";
  }
}

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
  /* Minutes round UP: a fresh 30-min suspension reads "30", not "29".
   * data1 packs the M0 spike telemetry (S4 raw HR, S8 heap headroom):
   * low byte = last raw bpm (capped 255), high byte = free heap / 64 B. */
  uint32_t heap = heap_bytes_free();
  if (heap > 255u * 64u) heap = 255u * 64u;
  AppWorkerMessage m = {
    .data0 = (uint16_t)cm_current_stage(&s_core),
    .data1 = (uint16_t)((s_last_bpm > 255 ? 255 : s_last_bpm) |
                        ((heap / 64u) << 8)),
    .data2 = (uint16_t)((cm_suspend_remaining_s(&s_core, now_ms()) + 59u) / 60u),
  };
  app_worker_send_message(WMSG_STATUS, &m);
}

/* Tell the foreground app about an action.
 *
 * `launch` must be true ONLY when the wearer has to see or do something
 * right now: launching takes over the screen and hides the watchface, so
 * informational events (cancellations, nags, suspension changes) are
 * delivered just to an app that already happens to be open. */
static void notify_app(const cm_action *a, bool launch) {
  AppWorkerMessage m = {
    .data0 = a->type, .data1 = a->detector, .data2 = a->seconds,
  };
  app_worker_send_message(WMSG_ACTION, &m); /* no-op if app not running */
  if (!launch) return;
  persist_write_data(PK_PENDING_ACTION, a, sizeof(*a));
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
    DLOG("act %s det=%u sec=%u reason=%u",
         action_name(a.type), a.detector, a.seconds, a.reason);
    switch (a.type) {
      case CM_ACT_HR_BURST_ON:  set_hr_burst(true);  break;
      case CM_ACT_HR_BURST_OFF: set_hr_burst(false); break;

      /* Needs the wearer's attention now: take the screen. */
      case CM_ACT_CHECKIN_START:
      case CM_ACT_COUNTDOWN_START:
      case CM_ACT_ALARM:
        notify_app(&a, true);
        break;
      /* Only reached when the wearer opted into scheduled check-ins, i.e.
       * asked to be prompted. */
      case CM_ACT_CHECKIN_REMINDER:
        notify_app(&a, true);
        break;

      /* The watch looks off-wrist: nobody is watching the watchface, so
       * taking the screen for the nag is the point — otherwise the nag
       * is invisible in worker mode (the worker cannot vibrate). */
      case CM_ACT_NOTWORN_NAG:
        notify_app(&a, true);
        break;

      /* Informational: never hijack the watchface for these. A
       * cancellation also invalidates any parked launch action — the
       * racing app launch must find nothing rather than a stale alert. */
      case CM_ACT_ALERT_CANCELLED:
        persist_delete(PK_PENDING_ACTION);
        notify_app(&a, false);
        break;
      case CM_ACT_SUSPEND_STARTED:
        notify_app(&a, false);
        break;
      /* Resume clears the persisted suspension — without this a worker
       * restart would silently re-suspend until the original expiry. */
      case CM_ACT_SUSPEND_EXPIRED:
      case CM_ACT_AUTO_RESUMED:
        persist_delete(PK_SUSPEND_UNTIL);
        persist_delete(PK_SUSPEND_AUTORESUME);
        notify_app(&a, false);
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
  uint32_t motion_before = s_core.last_motion_ms;
  cm_accel_feed(&s_core, s, n, now_ms());
  if (s_debug) {
    if (s_core.last_motion_ms != motion_before) s_dbg_motion_events++;
    s_dbg_last_mag = s_core.prev_mag;
  }
  drain_actions();
}

#if defined(PBL_HEALTH)
static void health_handler(HealthEventType event, void *context) {
  if (event == HealthEventHeartRateUpdate || event == HealthEventSignificantUpdate) {
    HealthValue bpm = health_service_peek_current_value(HealthMetricHeartRateRawBPM);
    s_last_bpm = bpm > 0 ? (uint16_t)bpm : 0;
    if (s_debug) {
      s_dbg_hr_updates++;
      s_dbg_last_bpm = s_last_bpm;
      DLOG("hr raw=%d burst=%u", (int)bpm, s_hr_burst_active);
    }
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
    .last_bpm = (uint8_t)(s_last_bpm > 255 ? 255 : s_last_bpm),
    .suspended = s_core.suspended,
  };
  data_logging_log(s_log_session, &rec, 1);
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  cm_tick(&s_core, now_ms(), (uint8_t)tick_time->tm_hour);
  drain_actions();

  /* S1 latency drill: fire a synthetic alert through the REAL alarm path
   * (persist handoff + worker_launch_app) and stamp the fire time so the
   * app can compute the cold-launch latency on a shared clock. */
  if (s_drill_countdown && --s_drill_countdown == 0) {
    cm_action a = {.type = CM_ACT_LATENCY_DRILL, .detector = 0, .seconds = 0};
    persist_write_int(PK_DRILL_FIRE_MS, (int32_t)now_ms());
    notify_app(&a, true);
    APP_LOG(APP_LOG_LEVEL_INFO, "latency drill fired");
  }

  if (--s_heartbeat_countdown == 0) {
    s_heartbeat_countdown = CM_HEARTBEAT_INTERVAL_S;
    log_heartbeat();
    if (s_debug) {
      DLOG("min: stage=%u susp=%u motion_evts=%u mag=%u bpm=%u hr_upd=%u heap_free=%u",
           (unsigned)cm_current_stage(&s_core), s_core.suspended,
           s_dbg_motion_events, s_dbg_last_mag, s_dbg_last_bpm,
           s_dbg_hr_updates, (unsigned)heap_bytes_free());
      s_dbg_motion_events = 0;
      s_dbg_hr_updates = 0;
    }
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
    case WMSG_RESUME:
      cm_resume(&s_core, now_ms());
      persist_delete(PK_SUSPEND_UNTIL);
      persist_delete(PK_SUSPEND_AUTORESUME);
      break;
    case WMSG_SOS: cm_manual_sos(&s_core, now_ms()); break;
    case WMSG_STATUS_REQ: push_status_to_app(); break;
    case WMSG_DRILL:
      s_drill_countdown = CM_DRILL_DELAY_S;
      APP_LOG(APP_LOG_LEVEL_INFO, "latency drill armed (%us)", CM_DRILL_DELAY_S);
      break;
    case WMSG_SET_DEBUG:
      s_debug = (uint8_t)m->data0;
      persist_write_int(PK_DEBUG, s_debug);
      APP_LOG(APP_LOG_LEVEL_INFO, "worker debug %s", s_debug ? "ON" : "off");
      break;
    default: break;
  }
  DLOG("wmsg type=%u d0=%u", type, m->data0);
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
  s_debug = persist_exists(PK_DEBUG) ? (uint8_t)persist_read_int(PK_DEBUG) : 0;
  if (s_debug) {
    APP_LOG(APP_LOG_LEVEL_INFO,
            "worker up (debug ON) hr=%u heap_free=%u",
            cfg.hr_available, (unsigned)heap_bytes_free());
  }
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
