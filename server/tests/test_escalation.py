from app.escalation import AlertKind, Contact, Escalation, Send, Tier


def tiers():
    return [
        Tier(name="relatives",
             contacts=[
                 Contact(id="r1", name="R1", channels=("telegram", "ntfy")),
                 Contact(id="r2", name="R2", channels=("email",)),
             ],
             repeat_after_s=1800, promote_after_s=600),
        Tier(name="cso",
             contacts=[Contact(id="cso", name="CSO", channels=("telegram",))],
             repeat_after_s=1800, promote_after_s=600),
    ]


def run_step(esc, t):
    sends = esc.step(t)
    for s in sends:
        pass
    # record one send per contact (channels fan out within a single attempt)
    for cid in {s.contact.id for s in sends}:
        esc.record_sent(cid, t)
    return sends


def test_tier1_sends_immediately_all_channels():
    esc = Escalation(AlertKind.WATCH_ALARM, tiers(), started_t=0.0)
    sends = run_step(esc, 0.0)
    assert {(s.contact.id, s.channel) for s in sends} == {
        ("r1", "telegram"), ("r1", "ntfy"), ("r2", "email")}
    assert all(s.tier == "relatives" for s in sends)


def test_no_ack_promotes_next_tier():
    esc = Escalation(AlertKind.WATCH_ALARM, tiers(), started_t=0.0)
    run_step(esc, 0.0)
    assert run_step(esc, 300) == []                   # not yet
    sends = run_step(esc, 600)                        # promote_after_s
    assert {s.contact.id for s in sends} == {"cso"}


def test_ack_stops_promotion_but_repeats_unacked():
    esc = Escalation(AlertKind.WATCH_ALARM, tiers(), started_t=0.0)
    run_step(esc, 0.0)
    assert esc.record_ack("r1", 120)
    assert run_step(esc, 700) == []                   # no promotion: someone is on it
    sends = run_step(esc, 1800)                       # repeat cycle
    ids = {s.contact.id for s in sends}
    assert "r2" in ids and "r1" not in ids            # only the unacked contact


def test_resolution_stops_everything():
    esc = Escalation(AlertKind.WATCH_ALARM, tiers(), started_t=0.0)
    run_step(esc, 0.0)
    esc.resolve("false_alarm")
    assert run_step(esc, 600) == []
    assert run_step(esc, 5000) == []


def test_duplicate_ack_is_idempotent():
    esc = Escalation(AlertKind.WATCH_ALARM, tiers(), started_t=0.0)
    run_step(esc, 0.0)
    assert esc.record_ack("r1", 10) is True
    assert esc.record_ack("r1", 20) is False


def test_repeat_cycle_for_fully_unacked():
    esc = Escalation(AlertKind.PHONE_SILENT, tiers(), started_t=0.0)
    first = run_step(esc, 0.0)
    assert len(first) == 3
    assert run_step(esc, 900) != []                   # tier2 promoted at 600
    sends = run_step(esc, 1800)                       # tier1 repeat due
    assert {s.contact.id for s in sends} >= {"r1", "r2"}
    r1 = [s for s in sends if s.contact.id == "r1"]
    assert all(s.attempt == 2 for s in r1)


def test_attempt_counter_increments():
    esc = Escalation(AlertKind.TEST, tiers(), started_t=0.0)
    s1 = run_step(esc, 0.0)
    s2 = run_step(esc, 1800)
    s3 = run_step(esc, 3600)
    a_r1 = lambda ss: [s.attempt for s in ss if s.contact.id == "r1"][0]
    assert (a_r1(s1), a_r1(s2), a_r1(s3)) == (1, 2, 3)
