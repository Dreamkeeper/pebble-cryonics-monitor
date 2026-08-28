/*
 * Shared worker <-> app <-> phone protocol constants.
 * Included by src/c (app), worker_src/c (worker), and mirrored in the
 * Android companion (Protocol.kt — keep in sync).
 */
#ifndef CM_PROTOCOL_H
#define CM_PROTOCOL_H

/* Persist keys (worker and app share the app's persist storage). */
enum {
  PK_CONFIG = 1,          /* cm_config blob */
  PK_PENDING_ACTION = 2,  /* cm_action awaiting foreground app pickup */
  PK_MODE = 3,            /* 0 = worker mode, 1 = persistent foreground mode */
  PK_SUSPEND_UNTIL = 4,   /* epoch seconds; survives worker restart */
  PK_SUSPEND_AUTORESUME = 5,
  PK_DEBUG = 6,           /* 1 = extensive APP_LOG output (app + worker) */
  PK_DRILL_FIRE_MS = 7,   /* wall-clock ms when the worker fired the latency
                             drill (worker and app share the clock) */
  PK_DRILL_ARM_MS = 8,    /* wall-clock ms when the worker was armed; lets the
                             phone subtract ALL watch-side time (arm->result)
                             instead of guessing the countdown duration */
  PK_BUILD_ID = 9,        /* hash of the app build: a running worker survives
                             a sideload executing the OLD binary — the app
                             kills and relaunches it when the build changes */
  PK_PENDING_ACTION_T = 10 /* epoch seconds the pending action was parked:
                              the app discards stale parked actions instead
                              of replaying yesterday's nag after a reboot */
};

/* AppWorkerMessage types (uint8). data0/data1/data2 per type. */
enum {
  WMSG_ACTION = 1,        /* worker->app: data0=cm_action_type, data1=detector, data2=seconds */
  WMSG_USER_OK = 2,       /* app->worker */
  WMSG_SUSPEND = 3,       /* app->worker: data0=minutes, data1=auto_resume */
  WMSG_RESUME = 4,        /* app->worker */
  WMSG_SOS = 5,           /* app->worker */
  WMSG_STATUS_REQ = 6,    /* app->worker: request status push */
  WMSG_STATUS = 7,        /* worker->app: data0=stage, data1=last_bpm, data2=suspend_remaining_min */
  WMSG_SET_DEBUG = 8,     /* app->worker: data0 = 0/1 */
  WMSG_DRILL = 9,         /* app->worker: run the S1 latency drill — wait
                             CM_DRILL_DELAY_S, then fire a synthetic
                             worker_launch_app() alert */
  WMSG_HR_LAB = 10,       /* app->worker: data0 = 1/0 — S4 sensor lab:
                             burst HR sampling + silent detector hold */
  WMSG_HR_SAMPLE = 11,    /* worker->app (lab only, every 2 s):
                             data0 = raw peek bpm, data1 = free heap / 64 B,
                             data2 = seconds since last HR event */
  WMSG_DIAG = 12          /* worker->app (debug only, with each status):
                             data0 = s since last bpm CHANGE (cap 9999),
                             data1 = s since last motion (cap 9999),
                             data2 = CM_DIAG_* flag bits */
};

/* WMSG_DIAG / heartbeat-record flag bits */
#define CM_DIAG_CHARGING   0x01
#define CM_DIAG_LAB_HOLD   0x02
#define CM_DIAG_HUNTING    0x04
#define CM_DIAG_NAGGED     0x08
#define CM_DIAG_EVER_PULSE 0x10
#define CM_DIAG_SUSPENDED  0x20

/* DataLogging heartbeat: tag 0xC202 = the 14-byte v2 record (v1 was 8
 * bytes under 0xC201; the phone parses either by size). */
#define CM_DL_TAG 0xC202

/* MSG_TYPE values for AppMessage to/from the phone. */
enum {
  PMSG_HEARTBEAT = 1,     /* watch->phone: periodic liveness + battery + bpm */
  PMSG_PRE_ALARM = 2,     /* watch->phone: countdown started (phone starts its own siren) */
  PMSG_ALARM = 3,         /* watch->phone: ladder exhausted — escalate */
  PMSG_CANCEL = 4,        /* watch->phone: alert cancelled (reason attached) */
  PMSG_SUSPENDED = 5,     /* watch->phone: suspension state changed */
  PMSG_CONFIG = 6,        /* phone->watch: cm_config blob push */
  PMSG_CONFIG_ACK = 7,    /* watch->phone */
  PMSG_USER_OK_REMOTE = 8,/* phone->watch: user cancelled on the phone */
  PMSG_SET_DEBUG = 9,     /* phone->watch: SECONDS key carries 0/1 */
  PMSG_NOTWORN = 10,      /* watch->phone: off-wrist nag (wearer-only, never contacts) */
  PMSG_DRILL = 11,        /* phone->watch: start the S1 latency drill */
  PMSG_DRILL_RESULT = 12, /* watch->phone: SECONDS = worker-fire -> app-alive ms */
  PMSG_CHARGING = 13,     /* watch->phone: SECONDS 1 = on charger (implicit
                             hold), 0 = unplugged (monitoring resumed) */
  PMSG_HR_LAB = 14,       /* phone->watch: SECONDS 1/0 — start/stop the S4
                             sensor lab (forwarded to the worker) */
  PMSG_HR_SAMPLE = 15     /* watch->phone: SECONDS = raw bpm, HEARTBEAT_SEQ =
                             seconds since last HR event, DETECTOR = heap/64 */
};

/* S1 latency drill: the worker waits this long after the app closes before
 * firing, so the measured launch is a genuine cold start. */
#define CM_DRILL_DELAY_S 10

/* Heartbeat cadence (watch -> phone) while connected. */
#define CM_HEARTBEAT_INTERVAL_S 60

/* Normal vs burst HR sampling period (seconds). */
#define CM_HR_PERIOD_NORMAL_S 60
#define CM_HR_PERIOD_BURST_S 1

#endif /* CM_PROTOCOL_H */
