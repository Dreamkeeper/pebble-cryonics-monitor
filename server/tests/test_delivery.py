"""Pump delivery: fan-out, retries, ordering, restart recovery, isolation,
self-notification, phone-silent advisories."""
import asyncio
import time

from app.escalation import Escalation
from conftest import admin_headers, wearer_headers


def _setup_contacts(appenv, headers=None):
    headers = headers or wearer_headers()
    for body in ({"name": "R1", "telegram_chat_id": "111111",
                  "ntfy_topic": "cm-r1"},
                 {"name": "R2", "email": "r2@x.y"}):
        r = appenv.client.post("/api/v1/contacts", json=body, headers=headers)
        assert r.status_code == 200, r.json()


def _alarm(appenv, kind="watch_alarm", headers=None):
    r = appenv.client.post("/api/v1/alarm",
                           json={"detector": "impact", "kind": kind,
                                 "lat": 52.5, "lon": 13.4},
                           headers=headers or wearer_headers())
    return r.json()["escalation_id"]


def _cycle(appenv, t):
    asyncio.run(appenv.main.pump_cycle(t))


def test_fanout_delivers_all_channels(appenv):
    _setup_contacts(appenv)
    _alarm(appenv)
    t = time.time()
    _cycle(appenv, t)

    tg = appenv.fakes["telegram"].sent
    ntfy = appenv.fakes["ntfy"].sent
    email = appenv.fakes["email"].sent
    assert [a for a, *_ in tg] == ["111111"]
    assert [a for a, *_ in ntfy] == ["cm-r1"]
    assert [a for a, *_ in email] == ["r2@x.y"]
    # message content: wearer name, location link, ref, ack affordances
    _, text, ack_url, ack_token = tg[0]
    assert "Default wearer" in text and "52.5,13.4" in text
    assert ack_token and ack_url.startswith("https://cm.test/api/v1/ack/")


def test_test_kind_is_tagged_everywhere(appenv):
    _setup_contacts(appenv)
    _alarm(appenv, kind="test")
    _cycle(appenv, time.time())
    for fake in appenv.fakes.values():
        for _, text, *_ in fake.sent:
            assert text.startswith("[TEST]")


def test_ack_token_persisted_before_send(appenv):
    """Design D3: a delivered message must never carry an unpersisted token."""
    _setup_contacts(appenv)
    seen = []

    def hook(address, text, ack_url, ack_token):
        if ack_token:
            from app.wearers import hash_token
            seen.append(appenv.store.lookup_ack_token(hash_token(ack_token)))

    appenv.fakes["telegram"].on_deliver = hook
    _alarm(appenv)
    _cycle(appenv, time.time())
    assert seen and all(row is not None for row in seen)


def test_failed_channel_retries_on_repeat_cadence(appenv):
    _setup_contacts(appenv)
    appenv.fakes["telegram"].fail = True
    _alarm(appenv)
    t0 = time.time()
    _cycle(appenv, t0)
    assert appenv.fakes["telegram"].sent == []       # failed
    assert len(appenv.fakes["ntfy"].sent) == 1       # sibling delivered

    _cycle(appenv, t0 + 10)                          # NOT every cycle
    assert appenv.fakes["ntfy"].sent and len(appenv.fakes["ntfy"].sent) == 1

    appenv.fakes["telegram"].fail = False
    _cycle(appenv, t0 + 1801)                        # repeat cadence
    assert len(appenv.fakes["telegram"].sent) == 1
    failures = [e for e in appenv.store.recent_events("default")
                if e["kind"] == "send" and not e["delivered"]]
    assert failures


def test_ack_stops_repeats_for_that_contact(appenv):
    _setup_contacts(appenv)
    _alarm(appenv)
    t0 = time.time()
    _cycle(appenv, t0)
    _, _, ack_url, _ = appenv.fakes["telegram"].sent[0]
    token = ack_url.rsplit("/", 1)[1]
    r = appenv.client.get(f"/api/v1/ack/{token}")
    assert "Acknowledged" in r.json()["message"]

    _cycle(appenv, t0 + 1801)
    assert len(appenv.fakes["telegram"].sent) == 1   # acked: no repeat
    assert len(appenv.fakes["email"].sent) == 2      # unacked repeats


def test_restart_recovery_preserves_attempts_and_acks(appenv):
    _setup_contacts(appenv)
    esc_id = _alarm(appenv)
    t0 = time.time()
    _cycle(appenv, t0)

    # simulate restart: rebuild runtime purely from the store
    rows = appenv.store.load_unresolved_escalations()
    assert [e for _, e, _ in rows] == [esc_id]
    restored = Escalation.from_state(rows[0][2])
    live = appenv.main.escalations[esc_id][1]
    assert restored.to_state() == live.to_state()

    appenv.main.escalations.clear()
    appenv.main.escalations[esc_id] = ("default", restored)
    _cycle(appenv, t0 + 10)
    assert len(appenv.fakes["telegram"].sent) == 1   # no premature resend
    _cycle(appenv, t0 + 1801)
    assert len(appenv.fakes["telegram"].sent) == 2   # cadence continued


def test_two_wearers_stay_separate(appenv):
    # second wearer with their own contact
    appenv.client.post("/api/v1/admin/wearers",
                       json={"id": "wb", "name": "Wearer B"},
                       headers=admin_headers())
    code = appenv.client.post("/api/v1/admin/wearers/wb/enroll-code",
                              headers=admin_headers()).json()["code"]
    tok_b = appenv.client.post("/api/v1/enroll",
                               json={"code": code}).json()["token"]
    appenv.client.post("/api/v1/contacts",
                       json={"name": "OnlyB", "telegram_chat_id": "999999"},
                       headers=wearer_headers(tok_b))
    _setup_contacts(appenv)  # default wearer's contacts

    _alarm(appenv)  # default's alarm only
    _cycle(appenv, time.time())
    addresses = [a for a, *_ in appenv.fakes["telegram"].sent]
    assert "111111" in addresses and "999999" not in addresses


def test_self_notify_copy_without_ack(appenv):
    _setup_contacts(appenv)
    appenv.client.put("/api/v1/self-notify",
                      json={"telegram_chat_id": "777777"},
                      headers=wearer_headers())
    _alarm(appenv)
    _cycle(appenv, time.time())
    copies = [(a, txt, url, tok) for a, txt, url, tok
              in appenv.fakes["telegram"].sent if a == "777777"]
    assert len(copies) == 1
    _, text, ack_url, ack_token = copies[0]
    assert "[copy to wearer]" in text and "Notified:" in text
    assert ack_url is None and ack_token is None


def test_phone_silent_advisory_created_once(appenv):
    _setup_contacts(appenv)
    t0 = time.time()
    appenv.main.get_monitor("default").heartbeat(t0, 80)
    _cycle(appenv, t0)
    _cycle(appenv, t0 + 2000)   # past silent_after_s
    _cycle(appenv, t0 + 2010)   # still silent: no duplicate
    advisories = [e for _, e in appenv.main.escalations.values()
                  if e.kind.value == "phone_silent"]
    assert len(advisories) == 1
    texts = [txt for _, txt, *_ in appenv.fakes["telegram"].sent]
    assert any("ADVISORY" in t for t in texts)
