"""Persistence: snapshot round-trips, restart clock honesty, enrollment."""
import time

from app.escalation import AlertKind, Contact, Escalation, Tier


def _tiers():
    return [Tier(name="primary",
                 contacts=[Contact(id="c1", name="R1",
                                   channels=("telegram", "email"),
                                   addresses=(("telegram", "111"),
                                              ("email", "r1@x.y")))],
                 repeat_after_s=1800, promote_after_s=600)]


def test_wearer_creation_seeds_default_tier(appenv):
    appenv.store.create_wearer("w2", "Wearer Two")
    tiers = appenv.store.list_tiers("w2")
    assert [t["name"] for t in tiers] == ["primary"]


def test_escalation_snapshot_roundtrip_preserves_clocks(appenv):
    t0 = 1000.0
    esc = Escalation(AlertKind.WATCH_ALARM, _tiers(), started_t=t0,
                     detector="impact", location="1,2")
    sends = esc.step(t0)
    assert sends
    esc.record_sent("c1", t0)
    esc.record_ack("c1", t0 + 5)

    appenv.store.save_escalation("default", "e1", esc.to_state())
    rows = appenv.store.load_unresolved_escalations()
    assert [(w, e) for w, e, _ in rows] == [("default", "e1")]
    restored = Escalation.from_state(rows[0][2])

    assert restored.any_ack
    assert restored.to_state() == esc.to_state()
    # clocks continue: repeat not due before cadence, due after
    assert restored.step(t0 + 60) == []
    assert restored.step(t0 + 1801) == []  # acked contact never repeats


def test_resolved_stays_resolved(appenv):
    esc = Escalation(AlertKind.TEST, _tiers(), started_t=0.0)
    esc.resolve("false_alarm")
    appenv.store.save_escalation("default", "e2", esc.to_state())
    assert appenv.store.load_unresolved_escalations() == []


def test_addresses_survive_snapshot(appenv):
    esc = Escalation(AlertKind.WATCH_ALARM, _tiers(), started_t=0.0)
    restored = Escalation.from_state(esc.to_state())
    contact = restored.tiers[0].contacts[0]
    assert dict(contact.addresses) == {"telegram": "111", "email": "r1@x.y"}


def test_deadman_roundtrip(appenv):
    appenv.store.save_deadman("default", 123.0, None, 456.0)
    row = appenv.store.load_deadman_all()["default"]
    assert row["last_heartbeat_t"] == 123.0
    assert row["low_battery_notice_t"] is None
    assert row["offline_until_t"] == 456.0


def test_enroll_code_single_use_and_ttl(appenv):
    appenv.store.create_wearer("w3", "Three")
    now = time.time()
    appenv.store.create_enroll_code("w3", "hash1", now + 60)
    assert appenv.store.exchange_enroll_code("hash1", now, "tok1") == "w3"
    assert appenv.store.exchange_enroll_code("hash1", now, "tok2") is None
    appenv.store.create_enroll_code("w3", "hash2", now - 1)  # expired
    assert appenv.store.exchange_enroll_code("hash2", now, "tok3") is None
    assert appenv.store.wearer_for_token("tok1") == "w3"


def test_token_revocation(appenv):
    appenv.store.create_wearer("w4", "Four")
    appenv.store.add_token("w4", "t4hash")
    assert appenv.store.wearer_for_token("t4hash") == "w4"
    assert appenv.store.revoke_tokens("w4") == 1
    assert appenv.store.wearer_for_token("t4hash") is None
