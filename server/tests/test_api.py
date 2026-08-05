import os

os.environ["CM_DISABLE_PUMP"] = "1"
os.environ["CM_API_TOKEN"] = "testtoken"

from fastapi.testclient import TestClient

from app import main
from app.main import app

client = TestClient(app)


def test_heartbeat_roundtrip():
    r = client.post("/api/v1/heartbeat",
                    json={"token": "testtoken", "phone_battery_pct": 77})
    assert r.status_code == 200
    assert r.json()["state"] == "ok"


def test_bad_token_rejected():
    r = client.post("/api/v1/heartbeat", json={"token": "wrong"})
    assert r.status_code == 401


def test_alarm_creates_escalation_and_resolve():
    r = client.post("/api/v1/alarm",
                    json={"token": "testtoken", "detector": "impact",
                          "lat": 52.52, "lon": 13.405})
    assert r.status_code == 200
    esc_id = r.json()["escalation_id"]
    assert esc_id in main.active_escalations

    s = client.get("/api/v1/status").json()
    assert s["active_escalations"][esc_id]["detector"] == "impact"

    r = client.post(f"/api/v1/alarm/{esc_id}/resolve",
                    params={"resolution": "false_alarm", "token": "testtoken"})
    assert r.status_code == 200
    assert main.active_escalations[esc_id].resolved


def test_offline_window():
    r = client.post("/api/v1/offline-window",
                    json={"token": "testtoken", "duration_s": 3600})
    assert r.status_code == 200
    assert r.json()["state"] == "offline_declared"
