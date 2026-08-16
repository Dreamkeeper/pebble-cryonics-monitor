"""Escalation engine: tiered, acknowledged, retried alert delivery.

Pure state machine with an injected clock. ``step(t)`` returns the ``Send``
actions due now; the runner performs them through channel plugins and calls
``record_sent``/``record_ack``. Nothing here does IO.

Design (docs/PLAN.md): delivery is acknowledged-and-retried, never
fire-and-forget — the Apple Watch "MMS arrived hours later" failure mode is
the reason this module exists. Unacknowledged tiers repeat and the next
tier is promoted after ``promote_after_s``. Any single ACK stops promotion
(a human is now aware) but repeats continue for other contacts until
``resolve()`` is called.
"""
from __future__ import annotations

import enum
from dataclasses import dataclass, field


class AlertKind(enum.Enum):
    WATCH_ALARM = "watch_alarm"        # confirmed ladder-exhausted alarm
    PHONE_SILENT = "phone_silent"      # dead-man advisory (lower severity)
    FAULT = "fault"                    # system health (watch battery, worker evicted)
    TEST = "test"                      # fire-drill mode: tagged TEST everywhere


@dataclass(frozen=True)
class Contact:
    id: str
    name: str
    channels: tuple[str, ...]  # e.g. ("telegram", "email", "ntfy")
    # Channel addressing, snapshotted at escalation creation:
    # (("telegram", "<chat_id>"), ("email", "<addr>"), ...). The engine
    # never reads it; the delivery layer does. Kept here so one snapshot
    # captures everything an escalation needs to run to completion even
    # if the wearer's live contacts are edited mid-alarm.
    addresses: tuple[tuple[str, str], ...] = ()


@dataclass
class Tier:
    name: str                          # "relatives", "cso"
    contacts: list[Contact]
    repeat_after_s: int = 1800         # re-send to unacked contacts (CI pattern: 30 min)
    promote_after_s: int = 600         # no ACK at all -> wake next tier


@dataclass(frozen=True)
class Send:
    contact: Contact
    channel: str
    tier: str
    kind: AlertKind
    attempt: int


@dataclass
class _ContactState:
    contact: Contact
    tier: str
    first_due_t: float
    acked: bool = False
    attempts: int = 0
    last_sent_t: float | None = None


class Escalation:
    def __init__(self, kind: AlertKind, tiers: list[Tier], started_t: float,
                 detector: str = "", location: str = ""):
        self.kind = kind
        self.tiers = tiers
        self.started_t = started_t
        self.detector = detector
        self.location = location
        self.resolved = False
        self.resolution: str | None = None
        self._contacts: list[_ContactState] = []
        self._activated_tiers: set[str] = set()
        self._operator_acks: list[str] = []  # dashboard operators who acked
        self._activate_tier(0, started_t)

    # -- internal --

    def _activate_tier(self, idx: int, t: float) -> None:
        if idx >= len(self.tiers):
            return
        tier = self.tiers[idx]
        if tier.name in self._activated_tiers:
            return
        self._activated_tiers.add(tier.name)
        for c in tier.contacts:
            self._contacts.append(_ContactState(contact=c, tier=tier.name, first_due_t=t))

    def _tier_index(self, name: str) -> int:
        for i, tier in enumerate(self.tiers):
            if tier.name == name:
                return i
        raise KeyError(name)

    def _tier(self, name: str) -> Tier:
        return self.tiers[self._tier_index(name)]

    # -- events from the runner --

    def record_sent(self, contact_id: str, t: float) -> None:
        for cs in self._contacts:
            if cs.contact.id == contact_id:
                cs.attempts += 1
                cs.last_sent_t = t

    def record_ack(self, contact_id: str, t: float) -> bool:
        """Returns True if this was a new acknowledgement."""
        for cs in self._contacts:
            if cs.contact.id == contact_id and not cs.acked:
                cs.acked = True
                return True
        return False

    def record_operator_ack(self, operator_id: str) -> bool:
        """A dashboard operator takes ownership: same ACK-gating effect as a
        contact acknowledgement (stops tier promotion), idempotent."""
        if operator_id in self._operator_acks:
            return False
        self._operator_acks.append(operator_id)
        return True

    def resolve(self, resolution: str) -> None:
        """'false_alarm' (wearer cancelled) or 'handled' (responder confirms)."""
        self.resolved = True
        self.resolution = resolution

    @property
    def any_ack(self) -> bool:
        return any(cs.acked for cs in self._contacts) or bool(self._operator_acks)

    # -- the clockwork --

    def step(self, t: float) -> list[Send]:
        """Return sends due at time ``t`` and handle tier promotion."""
        if self.resolved or not self.tiers:
            return []

        # Promote the next tier if the newest active tier is unacked too long.
        if not self.any_ack:
            newest_idx = max(self._tier_index(n) for n in self._activated_tiers)
            newest = self.tiers[newest_idx]
            tier_started = min(cs.first_due_t for cs in self._contacts
                               if cs.tier == newest.name)
            if t - tier_started >= newest.promote_after_s:
                self._activate_tier(newest_idx + 1, t)

        due: list[Send] = []
        for cs in self._contacts:
            if cs.acked:
                continue
            repeat = self._tier(cs.tier).repeat_after_s
            if cs.last_sent_t is None:
                if t >= cs.first_due_t:
                    due.extend(self._sends_for(cs))
            elif t - cs.last_sent_t >= repeat:
                due.extend(self._sends_for(cs))
        return due

    def _sends_for(self, cs: _ContactState) -> list[Send]:
        return [Send(contact=cs.contact, channel=ch, tier=cs.tier,
                     kind=self.kind, attempt=cs.attempts + 1)
                for ch in cs.contact.channels]

    # -- pure snapshot/restore (no IO; the store persists the dict) --

    def to_state(self) -> dict:
        return {
            "kind": self.kind.value,
            "detector": self.detector,
            "location": self.location,
            "started_t": self.started_t,
            "resolved": self.resolved,
            "resolution": self.resolution,
            "tiers": [
                {"name": tr.name,
                 "repeat_after_s": tr.repeat_after_s,
                 "promote_after_s": tr.promote_after_s,
                 "contacts": [
                     {"id": c.id, "name": c.name,
                      "channels": list(c.channels),
                      "addresses": [list(a) for a in c.addresses]}
                     for c in tr.contacts]}
                for tr in self.tiers],
            "activated_tiers": sorted(self._activated_tiers),
            "operator_acks": list(self._operator_acks),
            "contact_states": [
                {"contact_id": cs.contact.id, "tier": cs.tier,
                 "first_due_t": cs.first_due_t, "acked": cs.acked,
                 "attempts": cs.attempts, "last_sent_t": cs.last_sent_t}
                for cs in self._contacts],
        }

    @classmethod
    def from_state(cls, state: dict) -> "Escalation":
        tiers = [
            Tier(name=tr["name"],
                 contacts=[Contact(id=c["id"], name=c["name"],
                                   channels=tuple(c["channels"]),
                                   addresses=tuple(tuple(a) for a in
                                                   c.get("addresses", [])))
                           for c in tr["contacts"]],
                 repeat_after_s=tr["repeat_after_s"],
                 promote_after_s=tr["promote_after_s"])
            for tr in state["tiers"]]
        esc = cls.__new__(cls)
        esc.kind = AlertKind(state["kind"])
        esc.tiers = tiers
        esc.started_t = state["started_t"]
        esc.detector = state["detector"]
        esc.location = state["location"]
        esc.resolved = state["resolved"]
        esc.resolution = state["resolution"]
        esc._activated_tiers = set(state["activated_tiers"])
        esc._operator_acks = list(state.get("operator_acks", []))  # pre-v2 states
        by_id = {c.id: (c, tr.name) for tr in tiers for c in tr.contacts}
        esc._contacts = []
        for cs in state["contact_states"]:
            contact, _tier = by_id[cs["contact_id"]]
            esc._contacts.append(_ContactState(
                contact=contact, tier=cs["tier"],
                first_due_t=cs["first_due_t"], acked=cs["acked"],
                attempts=cs["attempts"], last_sent_t=cs["last_sent_t"]))
        return esc
