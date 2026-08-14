"""Phone-facing API surface: auth, health/status exposure, alarm lifecycle."""
from conftest import admin_headers, wearer_headers


def test_health_is_public_and_leaks_nothing(appenv):
    r = appenv.client.get("/api/v1/health")
    assert r.status_code == 200
    body = r.json()
    assert body["status"] == "ok"
    # count-only degradation signal; never names or ids
    assert set(body) == {"status", "service", "version", "wearers_degraded"}


def test_status_requires_token(appenv):
    assert appenv.client.get("/api/v1/status").status_code == 401
    r = appenv.client.get("/api/v1/status", headers=wearer_headers())
    assert r.status_code == 200
    assert r.json()["phone"] == "ok"


def test_bearer_and_legacy_query_token_both_work(appenv):
    ok = appenv.client.post("/api/v1/heartbeat",
                            json={"phone_battery_pct": 70},
                            headers=wearer_headers())
    assert ok.status_code == 200
    legacy = appenv.client.post("/api/v1/heartbeat",
                                json={"token": "testtoken",
                                      "phone_battery_pct": 70})
    assert legacy.status_code == 200


def test_bad_credentials_rejected(appenv):
    for hdr in ["Bearer wrong", "Basic testtoken", "testtoken", ""]:
        r = appenv.client.get("/api/v1/status",
                              headers={"Authorization": hdr})
        assert r.status_code == 401, hdr


def test_alarm_lifecycle(appenv):
    r = appenv.client.post("/api/v1/alarm",
                           json={"detector": "impact", "lat": 52.5, "lon": 13.4},
                           headers=wearer_headers())
    assert r.status_code == 200
    esc_id = r.json()["escalation_id"]

    s = appenv.client.get("/api/v1/status", headers=wearer_headers()).json()
    assert esc_id in s["active_escalations"]
    assert s["active_escalations"][esc_id]["detector"] == "impact"

    r = appenv.client.post(f"/api/v1/alarm/{esc_id}/resolve",
                           params={"resolution": "false_alarm"},
                           headers=wearer_headers())
    assert r.status_code == 200
    s = appenv.client.get("/api/v1/status", headers=wearer_headers()).json()
    assert esc_id not in s["active_escalations"]


def test_offline_window(appenv):
    r = appenv.client.post("/api/v1/offline-window",
                           json={"duration_s": 3600},
                           headers=wearer_headers())
    assert r.status_code == 200
    assert r.json()["state"] == "offline_declared"


def test_admin_status_lists_all_wearers(appenv):
    r = appenv.client.get("/api/v1/status", headers=admin_headers())
    assert r.status_code == 200
    ids = [w["id"] for w in r.json()["wearers"]]
    assert "default" in ids  # legacy bootstrap wearer


def test_admin_cannot_use_phone_endpoints(appenv):
    r = appenv.client.post("/api/v1/heartbeat", json={},
                           headers=admin_headers())
    assert r.status_code == 403
