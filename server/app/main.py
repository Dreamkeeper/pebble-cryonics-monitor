"""FastAPI wiring: heartbeats in, escalations out.

V0 scaffold: single wearer, in-memory state, config from environment.
TODO(M2): SQLite persistence, multi-wearer, web dashboard, Telegram
callback webhook, OwnTracks/Dawarich liveness probe, fire-drill scheduler.
"""
from __future__ import annotations

import asyncio
import os
import secrets
import time

from fastapi import FastAPI, HTTPException
from pydantic import BaseModel

from .deadman import DeadmanConfig, DeadmanMonitor, PhoneState
from .escalation import AlertKind, Contact, Escalation, Tier

app = FastAPI(title="Pebble Cryonics Monitor Server", version="0.1.0")

API_TOKEN = os.environ.get("CM_API_TOKEN", "")  # empty = auth disabled (dev only)

deadman = DeadmanMonitor(DeadmanConfig())
active_escalations: dict[str, Escalation] = {}
ack_tokens: dict[str, tuple[str, str]] = {}  # token -> (escalation_id, contact_id)
event_log: list[dict] = []


def default_tiers() -> list[Tier]:
    """TODO(M2): load from persisted config; contacts must be opt-in confirmed."""
    return [
        Tier(name="relatives", contacts=[
            Contact(id="relative1", name="Relative 1",
                    channels=("telegram", "ntfy", "email")),
        ]),
        Tier(name="cso", contacts=[
            Contact(id="cso_standby", name="CSO standby team",
                    channels=("telegram", "email")),
        ]),
    ]


def log_event(kind: str, **data) -> None:
    event_log.append({"t": time.time(), "kind": kind, **data})


class HeartbeatIn(BaseModel):
    token: str = ""
    phone_battery_pct: int | None = None
    watch_battery_pct: int | None = None
    watch_data_age_s: int | None = None
    suspended_until: float | None = None
    low_battery_warning: bool = False


class AlarmIn(BaseModel):
    token: str = ""
    detector: str
    kind: str = "watch_alarm"   # watch_alarm | fault | test
    lat: float | None = None
    lon: float | None = None


class OfflineWindowIn(BaseModel):
    token: str = ""
    duration_s: int


def _auth(token: str) -> None:
    if API_TOKEN and not secrets.compare_digest(token, API_TOKEN):
        raise HTTPException(401, "bad token")


@app.post("/api/v1/heartbeat")
def heartbeat(hb: HeartbeatIn):
    _auth(hb.token)
    t = time.time()
    deadman.heartbeat(t, hb.phone_battery_pct)
    if hb.low_battery_warning:
        deadman.low_battery_notice(t)
    log_event("heartbeat", battery=hb.phone_battery_pct,
              watch_age=hb.watch_data_age_s)
    return {"state": deadman.state.value, "server_time": t}


@app.post("/api/v1/offline-window")
def offline_window(w: OfflineWindowIn):
    _auth(w.token)
    deadman.declare_offline(time.time(), w.duration_s)
    log_event("offline_window", duration_s=w.duration_s)
    return {"state": PhoneState.OFFLINE_DECLARED.value}


@app.post("/api/v1/alarm")
def alarm(a: AlarmIn):
    _auth(a.token)
    t = time.time()
    esc_id = f"esc-{int(t)}-{a.detector}"
    kind = AlertKind(a.kind)
    loc = f"{a.lat},{a.lon}" if a.lat is not None else ""
    active_escalations[esc_id] = Escalation(
        kind=kind, tiers=default_tiers(), started_t=t,
        detector=a.detector, location=loc)
    log_event("alarm", id=esc_id, detector=a.detector, alert_kind=a.kind,
              location=loc)
    return {"escalation_id": esc_id}


@app.post("/api/v1/alarm/{esc_id}/resolve")
def resolve(esc_id: str, resolution: str = "false_alarm", token: str = ""):
    _auth(token)
    esc = active_escalations.get(esc_id)
    if not esc:
        raise HTTPException(404, "unknown escalation")
    esc.resolve(resolution)
    log_event("resolved", id=esc_id, resolution=resolution)
    return {"ok": True}


@app.get("/api/v1/ack/{ack_token}")
def ack(ack_token: str):
    """Signed one-tap ACK link included in every outbound message."""
    pair = ack_tokens.get(ack_token)
    if not pair:
        raise HTTPException(404, "unknown or expired ack token")
    esc_id, contact_id = pair
    esc = active_escalations.get(esc_id)
    if esc and esc.record_ack(contact_id, time.time()):
        log_event("ack", id=esc_id, contact=contact_id)
    return {"ok": True, "message": "Acknowledged. Thank you — updates will follow."}


@app.get("/api/v1/status")
def status():
    deadman.evaluate(time.time())
    return {
        "phone": deadman.state.value,
        "last_heartbeat_t": deadman.last_heartbeat_t,
        "active_escalations": {
            k: {"kind": e.kind.value, "detector": e.detector,
                "resolved": e.resolved, "any_ack": e.any_ack}
            for k, e in active_escalations.items()
        },
        "recent_events": event_log[-50:],
    }


async def escalation_pump():
    """Periodic driver: dead-man evaluation + due escalation sends.

    TODO(M2): wire Send objects to the configured channel plugins with
    per-send ack tokens; currently logs only (scaffold).
    """
    while True:
        t = time.time()
        prev = deadman.state
        state = deadman.evaluate(t)
        if state == PhoneState.SILENT and prev != PhoneState.SILENT:
            esc_id = f"esc-{int(t)}-phone-silent"
            active_escalations[esc_id] = Escalation(
                kind=AlertKind.PHONE_SILENT, tiers=default_tiers(),
                started_t=t, detector="phone_silent")
            log_event("deadman_silent", id=esc_id)

        for esc_id, esc in list(active_escalations.items()):
            for send in esc.step(t):
                token = secrets.token_urlsafe(16)
                ack_tokens[token] = (esc_id, send.contact.id)
                esc.record_sent(send.contact.id, t)
                log_event("send", id=esc_id, contact=send.contact.id,
                          channel=send.channel, attempt=send.attempt)
        await asyncio.sleep(5)


@app.on_event("startup")
async def _start_pump():
    if os.environ.get("CM_DISABLE_PUMP") != "1":  # tests disable the loop
        asyncio.create_task(escalation_pump())
