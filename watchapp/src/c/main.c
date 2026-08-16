/*
 * Foreground app: status screen, alert ladder UI, suspension menu, phone link.
 *
 * In worker mode the app is normally closed; the worker launches it when an
 * alert needs vibration/UI/AppMessage. In persistent-foreground mode ("high
 * assurance") this app stays open and doubles as a watchface-style status
 * screen (YaForecasWatch2 rendering merge: TODO, M3).
 */
#include <pebble.h>
#include "../core/detectors.h"
#include "../core/protocol.h"

static uint8_t s_debug;
/* True when the worker launched us for an alert rather than the wearer
 * opening the app. Such a launch must return the watch to the watchface
 * as soon as the alert is over. */
static bool s_launched_by_worker;
#define DLOG(...) do { \
    if (s_debug) APP_LOG(APP_LOG_LEVEL_DEBUG, __VA_ARGS__); \
  } while (0)

static Window *s_main_window;
static StatusBarLayer *s_status_bar;  /* guideline: long-running apps show time */
static TextLayer *s_status_layer;
static TextLayer *s_detail_layer;
static char s_status_buf[48];
static char s_detail_buf[64];

static Window *s_alert_window;
static TextLayer *s_alert_title;
static TextLayer *s_alert_count;   /* dominant countdown numerals */
static TextLayer *s_alert_body;
static char s_alert_count_buf[8];
static AppTimer *s_alert_timer;
static cm_action s_alert_action;
static uint16_t s_alert_seconds_left;

/* ---------- phone link ---------- */

static void send_to_phone(uint8_t msg_type, const cm_action *a) {
  DictionaryIterator *out;
  if (app_message_outbox_begin(&out) != APP_MSG_OK) return;
  dict_write_uint8(out, MESSAGE_KEY_MSG_TYPE, msg_type);
  if (a) {
    dict_write_uint8(out, MESSAGE_KEY_DETECTOR, a->detector);
    dict_write_uint8(out, MESSAGE_KEY_CANCEL_REASON, a->reason);
    dict_write_uint16(out, MESSAGE_KEY_SECONDS, a->seconds);
  }
  dict_write_uint8(out, MESSAGE_KEY_WATCH_BATTERY,
                   battery_state_service_peek().charge_percent);
  app_message_outbox_send();
  DLOG("tx pmsg=%u det=%u", msg_type, a ? a->detector : 0);
}

static void set_debug(uint8_t on) {
  s_debug = on;
  persist_write_int(PK_DEBUG, on);
  AppWorkerMessage m = {.data0 = on};
  app_worker_send_message(WMSG_SET_DEBUG, &m);
  APP_LOG(APP_LOG_LEVEL_INFO, "debug %s", on ? "ON" : "off");
}

static void inbox_received(DictionaryIterator *iter, void *context) {
  Tuple *t = dict_find(iter, MESSAGE_KEY_MSG_TYPE);
  if (!t) return;
  DLOG("rx pmsg=%u", t->value->uint8);
  switch (t->value->uint8) {
    case PMSG_SET_DEBUG: {
      Tuple *v = dict_find(iter, MESSAGE_KEY_SECONDS);
      set_debug(v && v->value->uint16 ? 1 : 0);
      break;
    }
    case PMSG_CONFIG: {
      Tuple *blob = dict_find(iter, MESSAGE_KEY_CFG_BLOB);
      if (blob && blob->length == sizeof(cm_config)) {
        persist_write_data(PK_CONFIG, blob->value->data, sizeof(cm_config));
        /* worker reloads config on restart; TODO: live reload message */
        send_to_phone(PMSG_CONFIG_ACK, NULL);
      }
      break;
    }
    case PMSG_USER_OK_REMOTE: {
      AppWorkerMessage m = {0};
      app_worker_send_message(WMSG_USER_OK, &m);
      break;
    }
    default: break;
  }
}

/* ---------- alert window ---------- */

static const char *detector_name(uint8_t det) {
  switch (det) {
    case CM_DET_PULSE:     return "No pulse signal";
    case CM_DET_IMPACT:    return "Hard impact detected";
    case CM_DET_NONMOTION: return "No movement detected";
    case CM_DET_CHECKIN:   return "Check-in due";
    case CM_DET_SOS:       return "SOS";
    default:               return "Alert";
  }
}

static void alert_vibe(void) {
  if (s_alert_action.type == CM_ACT_COUNTDOWN_START) {
    static const uint32_t seg[] = {400, 200, 400, 200, 400};
    VibePattern pat = {.durations = seg, .num_segments = ARRAY_LENGTH(seg)};
    vibes_enqueue_custom_pattern(pat);
  } else {
    vibes_double_pulse();
  }
}

static void alert_tick(void *data) {
  s_alert_timer = NULL;
  if (!s_alert_window) return;

  if (s_alert_seconds_left > 0) s_alert_seconds_left--;
  snprintf(s_alert_count_buf, sizeof(s_alert_count_buf), "%u",
           (unsigned)s_alert_seconds_left);
  text_layer_set_text(s_alert_count, s_alert_count_buf);
  text_layer_set_text(s_alert_body,
                      s_alert_action.type == CM_ACT_COUNTDOWN_START
                          ? "SELECT if you are OK"
                          : "Are you OK? Press SELECT");

  /* escalate vibration every few seconds; the core decides stage changes */
  if (s_alert_seconds_left % 5 == 0) alert_vibe();
  s_alert_timer = app_timer_register(1000, alert_tick, NULL);
}

/* Semantic color (guideline: red only for genuine error/alarm states):
 * amber while asking, red once the countdown to alarm is running. */
static void alert_apply_style(void) {
  bool critical = s_alert_action.type == CM_ACT_COUNTDOWN_START ||
                  s_alert_action.type == CM_ACT_ALARM;
#if defined(PBL_COLOR)
  GColor bg = critical ? GColorRed : GColorChromeYellow;
  GColor fg = critical ? GColorWhite : GColorBlack;
#else
  (void)critical;  /* monochrome: hierarchy carries the urgency instead */
  GColor bg = GColorWhite;
  GColor fg = GColorBlack;
#endif
  window_set_background_color(s_alert_window, bg);
  TextLayer *layers[] = {s_alert_title, s_alert_count, s_alert_body};
  for (unsigned i = 0; i < ARRAY_LENGTH(layers); i++) {
    text_layer_set_background_color(layers[i], GColorClear);
    text_layer_set_text_color(layers[i], fg);
  }
}

static void alert_select(ClickRecognizerRef ref, void *ctx) {
  AppWorkerMessage m = {0};
  app_worker_send_message(WMSG_USER_OK, &m);
  vibes_cancel();
  window_stack_remove(s_alert_window, true);
}

static void alert_click_config(void *ctx) {
  window_single_click_subscribe(BUTTON_ID_SELECT, alert_select);
  /* BACK deliberately not subscribed: it dismisses the window but the
   * ladder keeps running in the worker — dismissing UI is not "I'm OK". */
}

static void alert_window_load(Window *w) {
  Layer *root = window_get_root_layer(w);
  GRect b = layer_get_bounds(root);
  /* guideline hierarchy: 28pt title, dominant numeric countdown, 18pt hint */
  s_alert_title = text_layer_create(GRect(0, 4, b.size.w, 62));
  text_layer_set_font(s_alert_title,
                      fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
  text_layer_set_text_alignment(s_alert_title, GTextAlignmentCenter);
  text_layer_set_text(s_alert_title, detector_name(s_alert_action.detector));
  layer_add_child(root, text_layer_get_layer(s_alert_title));

  s_alert_count = text_layer_create(GRect(0, b.size.h / 2 - 24, b.size.w, 52));
  text_layer_set_font(s_alert_count,
                      fonts_get_system_font(FONT_KEY_LECO_42_NUMBERS));
  text_layer_set_text_alignment(s_alert_count, GTextAlignmentCenter);
  layer_add_child(root, text_layer_get_layer(s_alert_count));

  s_alert_body = text_layer_create(GRect(0, b.size.h - 46, b.size.w, 42));
  text_layer_set_font(s_alert_body, fonts_get_system_font(FONT_KEY_GOTHIC_18));
  text_layer_set_text_alignment(s_alert_body, GTextAlignmentCenter);
  layer_add_child(root, text_layer_get_layer(s_alert_body));

  alert_apply_style();
  s_alert_seconds_left = s_alert_action.seconds;
  alert_vibe();
  alert_tick(NULL);
}

static void alert_window_unload(Window *w) {
  if (s_alert_timer) { app_timer_cancel(s_alert_timer); s_alert_timer = NULL; }
  text_layer_destroy(s_alert_title);
  text_layer_destroy(s_alert_count);
  text_layer_destroy(s_alert_body);
  window_destroy(s_alert_window);
  s_alert_window = NULL;
}

static void show_alert(const cm_action *a) {
  s_alert_action = *a;
  if (a->type == CM_ACT_COUNTDOWN_START) send_to_phone(PMSG_PRE_ALARM, a);
  if (a->type == CM_ACT_ALARM) send_to_phone(PMSG_ALARM, a);
  if (a->type == CM_ACT_ALERT_CANCELLED) {
    send_to_phone(PMSG_CANCEL, a);
    vibes_cancel();
    if (s_alert_window) window_stack_remove(s_alert_window, true);
    /* Nothing left to show: hand the screen back to the watchface. */
    if (s_launched_by_worker) window_stack_pop_all(false);
    return;
  }
  if (!s_alert_window) {
    s_alert_window = window_create();
    window_set_click_config_provider(s_alert_window, alert_click_config);
    window_set_window_handlers(s_alert_window, (WindowHandlers){
        .load = alert_window_load, .unload = alert_window_unload});
    window_stack_push(s_alert_window, true);
  } else {
    s_alert_seconds_left = a->seconds;
    text_layer_set_text(s_alert_title, detector_name(a->detector));
    alert_apply_style();  /* stage may have escalated: amber -> red */
  }
}

static void handle_action(const cm_action *a) {
  DLOG("handle_action type=%u det=%u sec=%u", a->type, a->detector, a->seconds);
  switch (a->type) {
    case CM_ACT_CHECKIN_START:
    case CM_ACT_COUNTDOWN_START:
    case CM_ACT_ALARM:
    case CM_ACT_ALERT_CANCELLED:
      show_alert(a);
      break;
    case CM_ACT_NOTWORN_NAG:
      vibes_double_pulse(); /* TODO dedicated nag UI + phone forward */
      break;
    case CM_ACT_CHECKIN_REMINDER:
      vibes_short_pulse();  /* TODO show "check-in due in N min" */
      break;
    case CM_ACT_SUSPEND_EXPIRED:
    case CM_ACT_AUTO_RESUMED:
      vibes_short_pulse();
      send_to_phone(PMSG_SUSPENDED, a);
      break;
    default: break;
  }
}

static void pickup_pending_action(void) {
  if (!persist_exists(PK_PENDING_ACTION)) return;
  cm_action a;
  if (persist_read_data(PK_PENDING_ACTION, &a, sizeof(a)) == sizeof(a)) {
    persist_delete(PK_PENDING_ACTION);
    handle_action(&a);
  }
}

/* ---------- worker messages (while app is open) ---------- */

static void worker_message_handler(uint16_t type, AppWorkerMessage *m) {
  if (type == WMSG_ACTION) {
    cm_action a = {.type = (uint8_t)m->data0,
                   .detector = (uint8_t)m->data1,
                   .seconds = m->data2};
    persist_delete(PK_PENDING_ACTION); /* we got it live */
    handle_action(&a);
  } else if (type == WMSG_STATUS) {
    snprintf(s_detail_buf, sizeof(s_detail_buf), "stage %u  susp %u min%s",
             (unsigned)m->data0, (unsigned)m->data2, s_debug ? "  DBG" : "");
    text_layer_set_text(s_detail_layer, s_detail_buf);
    /* Stale-launch guard: the worker launched us for an alert that has
     * since ended (e.g. motion cancelled it during the launch gap). A
     * bare "Monitoring" screen the wearer never asked for must not sit
     * on top of their watchface — hand the screen back. */
    if (s_launched_by_worker && m->data0 == (uint16_t)CM_STAGE_NONE) {
      DLOG("stale worker launch: no active stage, returning to watchface");
      vibes_cancel();
      if (s_alert_window) window_stack_remove(s_alert_window, true);
      window_stack_pop_all(false);
    }
  }
}

/* ---------- suspension menu ---------- */

static void suspend_minutes(uint16_t minutes) {
  AppWorkerMessage m = {.data0 = minutes, .data1 = 1 /* auto-resume default on */};
  app_worker_send_message(WMSG_SUSPEND, &m);
  snprintf(s_status_buf, sizeof(s_status_buf), "Suspended %u min", minutes);
  text_layer_set_text(s_status_layer, s_status_buf);
}

/* TODO(M1): replace with a real MenuLayer incl. custom duration and
 * auto-resume toggle. Skeleton: UP cycles presets. */
static void up_click(ClickRecognizerRef ref, void *ctx) {
  static const uint16_t presets[] = {30, 60, 120};
  static int idx = 0;
  suspend_minutes(presets[idx]);
  idx = (idx + 1) % 3;
}

static void select_click(ClickRecognizerRef ref, void *ctx) {
  AppWorkerMessage m = {0};
  app_worker_send_message(WMSG_USER_OK, &m); /* manual check-in / resume */
  text_layer_set_text(s_status_layer, "Checked in");
}

static void down_long_click(ClickRecognizerRef ref, void *ctx) {
  AppWorkerMessage m = {0};
  app_worker_send_message(WMSG_SOS, &m);
}

static void main_click_config(void *ctx) {
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click);
  window_single_click_subscribe(BUTTON_ID_UP, up_click);
  window_long_click_subscribe(BUTTON_ID_DOWN, 700, down_long_click, NULL);
}

/* ---------- main window ---------- */

static void main_window_load(Window *w) {
  Layer *root = window_get_root_layer(w);
  GRect b = layer_get_bounds(root);

  /* guideline: long-running apps keep the time visible */
  s_status_bar = status_bar_layer_create();
  status_bar_layer_set_colors(s_status_bar,
      PBL_IF_COLOR_ELSE(GColorDarkGreen, GColorWhite),
      PBL_IF_COLOR_ELSE(GColorWhite, GColorBlack));
  layer_add_child(root, status_bar_layer_get_layer(s_status_bar));

  window_set_background_color(w,
      PBL_IF_COLOR_ELSE(GColorDarkGreen, GColorWhite));
  GColor fg = PBL_IF_COLOR_ELSE(GColorWhite, GColorBlack);

  s_status_layer = text_layer_create(
      GRect(0, STATUS_BAR_LAYER_HEIGHT + 14, b.size.w, 64));
  text_layer_set_font(s_status_layer,
                      fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
  text_layer_set_text_alignment(s_status_layer, GTextAlignmentCenter);
  text_layer_set_background_color(s_status_layer, GColorClear);
  text_layer_set_text_color(s_status_layer, fg);
  text_layer_set_text(s_status_layer, "Monitoring");
  layer_add_child(root, text_layer_get_layer(s_status_layer));

  s_detail_layer = text_layer_create(GRect(0, b.size.h - 62, b.size.w, 58));
  text_layer_set_font(s_detail_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
  text_layer_set_text_alignment(s_detail_layer, GTextAlignmentCenter);
  text_layer_set_background_color(s_detail_layer, GColorClear);
  text_layer_set_text_color(s_detail_layer, fg);
  text_layer_set_text(s_detail_layer, "SELECT check-in\nUP suspend  DOWN hold SOS");
  layer_add_child(root, text_layer_get_layer(s_detail_layer));
}

static void main_window_unload(Window *w) {
  status_bar_layer_destroy(s_status_bar);
  text_layer_destroy(s_status_layer);
  text_layer_destroy(s_detail_layer);
}

/* While the app is open it heartbeats the phone directly. (In worker mode
 * with the app closed, phone-side liveness relies on DataLogging records +
 * BT connection events — documented v0.1 limitation.) */
static void app_tick(struct tm *tick_time, TimeUnits changed) {
  static uint16_t s_hb_seq = 0;
  if (tick_time->tm_sec == 0) { /* once a minute */
    DictionaryIterator *out;
    if (app_message_outbox_begin(&out) == APP_MSG_OK) {
      dict_write_uint8(out, MESSAGE_KEY_MSG_TYPE, PMSG_HEARTBEAT);
      dict_write_uint16(out, MESSAGE_KEY_HEARTBEAT_SEQ, ++s_hb_seq);
      dict_write_uint8(out, MESSAGE_KEY_WATCH_BATTERY,
                       battery_state_service_peek().charge_percent);
      app_message_outbox_send();
    }
  }
}

static void ensure_worker_running(void) {
  if (!app_worker_is_running()) {
    AppWorkerResult r = app_worker_launch();
    APP_LOG(APP_LOG_LEVEL_INFO, "worker launch: %d", (int)r);
  }
}

static void init(void) {
  s_debug = persist_exists(PK_DEBUG) ? (uint8_t)persist_read_int(PK_DEBUG) : 0;
  s_main_window = window_create();
  window_set_click_config_provider(s_main_window, main_click_config);
  window_set_window_handlers(s_main_window, (WindowHandlers){
      .load = main_window_load, .unload = main_window_unload});
  window_stack_push(s_main_window, true);

  app_message_register_inbox_received(inbox_received);
  app_message_open(256, 256);

  app_worker_message_subscribe(worker_message_handler);
  tick_timer_service_subscribe(SECOND_UNIT, app_tick);
  ensure_worker_running();

  /* Launched by the worker? Pick up the parked action immediately. */
  s_launched_by_worker = (launch_reason() == APP_LAUNCH_WORKER);
  if (s_launched_by_worker) {
    pickup_pending_action();
  }
  AppWorkerMessage m = {0};
  app_worker_send_message(WMSG_STATUS_REQ, &m);
}

static void deinit(void) {
  app_worker_message_unsubscribe();
  window_destroy(s_main_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
