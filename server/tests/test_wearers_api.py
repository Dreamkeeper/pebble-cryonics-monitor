"""Tenancy: enrollment, admin auth, contact CRUD, isolation, DEGRADED."""
from conftest import admin_headers, wearer_headers


def _create_wearer_with_token(appenv, wid: str) -> str:
    """Admin creates wearer, issues code, app enrolls -> returns token."""
    r = appenv.client.post("/api/v1/admin/wearers",
                           json={"id": wid, "name": wid.title()},
                           headers=admin_headers())
    assert r.status_code == 200
    code = appenv.client.post(f"/api/v1/admin/wearers/{wid}/enroll-code",
                              headers=admin_headers()).json()["code"]
    r = appenv.client.post("/api/v1/enroll", json={"code": code})
    assert r.status_code == 200
    return r.json()["token"]


def test_enrollment_flow_and_single_use(appenv):
    code = None
    appenv.client.post("/api/v1/admin/wearers",
                       json={"id": "alice", "name": "Alice"},
                       headers=admin_headers())
    code = appenv.client.post("/api/v1/admin/wearers/alice/enroll-code",
                              headers=admin_headers()).json()["code"]
    first = appenv.client.post("/api/v1/enroll", json={"code": code})
    assert first.status_code == 200
    again = appenv.client.post("/api/v1/enroll", json={"code": code})
    assert again.status_code == 410  # burned

    token = first.json()["token"]
    hb = appenv.client.post("/api/v1/heartbeat", json={},
                            headers=wearer_headers(token))
    assert hb.status_code == 200


def test_wearer_token_cannot_administrate(appenv):
    r = appenv.client.post("/api/v1/admin/wearers",
                           json={"id": "mallory", "name": "M"},
                           headers=wearer_headers())
    assert r.status_code == 403


def test_isolation_between_wearers(appenv):
    tok_a = _create_wearer_with_token(appenv, "wa")
    tok_b = _create_wearer_with_token(appenv, "wb")

    # B configures a contact; A must not see or edit it
    r = appenv.client.post("/api/v1/contacts",
                           json={"name": "Bea", "email": "b@x.y"},
                           headers=wearer_headers(tok_b))
    assert r.status_code == 200

    own = appenv.client.get("/api/v1/contacts", headers=wearer_headers(tok_a))
    assert own.json()["contacts"] == []

    cross = appenv.client.get("/api/v1/contacts", params={"wearer_id": "wb"},
                              headers=wearer_headers(tok_a))
    assert cross.status_code == 403

    # A cannot resolve B's escalation (indistinguishable from unknown)
    esc = appenv.client.post("/api/v1/alarm", json={"detector": "sos"},
                             headers=wearer_headers(tok_b)).json()
    r = appenv.client.post(f"/api/v1/alarm/{esc['escalation_id']}/resolve",
                           headers=wearer_headers(tok_a))
    assert r.status_code == 404

    # A's status shows nothing of B
    s = appenv.client.get("/api/v1/status", headers=wearer_headers(tok_a)).json()
    assert s["active_escalations"] == {}
    assert all("wb" != e.get("wearer_id") for e in s["recent_events"])


def test_contact_validation(appenv):
    bad = appenv.client.post("/api/v1/contacts",
                             json={"name": "X", "telegram_chat_id": "abc"},
                             headers=wearer_headers())
    assert bad.status_code == 422
    fields = bad.json()["detail"]["fields"]
    assert "telegram_chat_id" in fields

    none = appenv.client.post("/api/v1/contacts", json={"name": "X"},
                              headers=wearer_headers())
    assert none.status_code == 422
    assert "channels" in none.json()["detail"]["fields"]

    unknown_tier = appenv.client.post(
        "/api/v1/contacts",
        json={"name": "X", "email": "a@b.c", "tier_name": "nope"},
        headers=wearer_headers())
    assert unknown_tier.status_code == 422


def test_degraded_semantics(appenv):
    # bootstrap wearer has no contacts yet -> degraded, count-only in health
    h = appenv.client.get("/api/v1/health").json()
    assert h["wearers_degraded"] >= 1
    s = appenv.client.get("/api/v1/status", headers=wearer_headers()).json()
    assert s["degraded"] is True

    appenv.client.post("/api/v1/contacts",
                       json={"name": "R1", "email": "r1@x.y"},
                       headers=wearer_headers())
    s = appenv.client.get("/api/v1/status", headers=wearer_headers()).json()
    assert s["degraded"] is False


def test_admin_edits_any_wearers_contacts(appenv):
    _create_wearer_with_token(appenv, "wc")
    r = appenv.client.post("/api/v1/contacts", params={"wearer_id": "wc"},
                           json={"name": "Op-added", "email": "op@x.y"},
                           headers=admin_headers())
    assert r.status_code == 200
    listed = appenv.client.get("/api/v1/contacts", params={"wearer_id": "wc"},
                               headers=admin_headers()).json()
    assert [c["name"] for c in listed["contacts"]] == ["Op-added"]


def test_disabled_wearer_stops_authenticating(appenv):
    tok = _create_wearer_with_token(appenv, "wd")
    appenv.client.post("/api/v1/admin/wearers/wd/disable",
                       headers=admin_headers())
    r = appenv.client.post("/api/v1/heartbeat", json={},
                           headers=wearer_headers(tok))
    assert r.status_code == 401


def test_legacy_bootstrap_created_default_wearer(appenv):
    wearers = {w["id"] for w in appenv.store.list_wearers()}
    assert "default" in wearers
    events = appenv.store.recent_events("default")
    assert any(e["kind"] == "legacy_migrated" for e in events)
