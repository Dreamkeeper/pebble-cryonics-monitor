"""Telegram long-poll: callback parsing and offset persistence."""
import asyncio

from app import telegram_poll


def test_parse_ack_callbacks_filters_and_extracts():
    updates = [
        {"update_id": 5, "callback_query": {"id": "cb1", "data": "ack:tokA"}},
        {"update_id": 6, "message": {"text": "hi"}},                # ignored
        {"update_id": 7, "callback_query": {"id": "cb2", "data": "other"}},
        {"update_id": 8, "callback_query": {"data": "ack:noid"}},   # no id
    ]
    assert telegram_poll.parse_ack_callbacks(updates) == [(5, "cb1", "tokA")]


def test_poll_records_ack_and_advances_offset(appenv, monkeypatch):
    calls = []

    def fake_api(bot_token, method, params, timeout):
        calls.append((method, params))
        if method == "getUpdates":
            if len([c for c in calls if c[0] == "getUpdates"]) == 1:
                return {"result": [
                    {"update_id": 41,
                     "callback_query": {"id": "cb9", "data": "ack:rawtok"}}]}
            raise asyncio.CancelledError  # stop the loop after one cycle
        return {"ok": True}

    monkeypatch.setattr(telegram_poll, "_api", fake_api)
    acked = []

    async def on_ack(token):
        acked.append(token)
        return "Acknowledged"

    async def run():
        try:
            await telegram_poll.poll_loop("BOT", appenv.store, on_ack,
                                          idle_sleep_s=0)
        except asyncio.CancelledError:
            pass

    asyncio.run(run())
    assert acked == ["rawtok"]
    assert appenv.store.kv_get(telegram_poll.OFFSET_KEY) == "41"
    answered = [p for m, p in calls if m == "answerCallbackQuery"]
    assert answered and answered[0]["callback_query_id"] == "cb9"
