"""Shared fixtures: a fresh app + temp SQLite per test, fake channels."""
from __future__ import annotations

import importlib
import os

import pytest
from fastapi.testclient import TestClient

WEARER_TOKEN = "testtoken"          # becomes wearer 'default' via bootstrap
ADMIN_TOKEN = "admintok"


class FakeChannel:
    def __init__(self, name: str):
        self.name = name
        self.sent: list[tuple] = []   # (address, text, ack_url, ack_token)
        self.fail = False
        self.on_deliver = None        # optional hook(address, ...)

    def deliver(self, address, text, ack_url, ack_token) -> bool:
        if self.on_deliver:
            self.on_deliver(address, text, ack_url, ack_token)
        if self.fail:
            return False
        self.sent.append((address, text, ack_url, ack_token))
        return True


@pytest.fixture()
def appenv(tmp_path, monkeypatch):
    """Fresh store + reloaded app modules + running TestClient."""
    monkeypatch.setenv("CM_DATA_DIR", str(tmp_path))
    monkeypatch.setenv("CM_API_TOKEN", WEARER_TOKEN)
    monkeypatch.setenv("CM_ADMIN_TOKEN", ADMIN_TOKEN)
    monkeypatch.setenv("CM_DISABLE_PUMP", "1")
    monkeypatch.setenv("CM_DISABLE_TG_POLL", "1")
    monkeypatch.setenv("CM_PUBLIC_URL", "https://cm.test")
    monkeypatch.setenv("CM_LOG_LEVEL", "WARNING")

    from app import store as store_mod
    store = importlib.reload(store_mod)
    from app import wearers as wearers_mod
    wearers = importlib.reload(wearers_mod)
    from app import main as main_mod
    main = importlib.reload(main_mod)

    fakes = {"telegram": FakeChannel("telegram"),
             "ntfy": FakeChannel("ntfy"),
             "email": FakeChannel("email")}
    main.CHANNELS = fakes

    with TestClient(main.app) as client:
        yield type("AppEnv", (), {
            "client": client, "main": main, "store": store.db,
            "wearers": wearers, "fakes": fakes,
        })()


def wearer_headers(token: str = WEARER_TOKEN) -> dict:
    return {"Authorization": f"Bearer {token}"}


def admin_headers() -> dict:
    return {"Authorization": f"Bearer {ADMIN_TOKEN}"}
