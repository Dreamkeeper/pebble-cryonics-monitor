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
    parsed = telegram_poll.parse_ack_callbacks(updates)
    assert len(parsed) == 1
    assert parsed[0]["update_id"] == 5
    assert parsed[0]["cq_id"] == "cb1"
    assert parsed[0]["token"] == "tokA"


def test_parse_ack_callbacks_carries_message_identity_for_editing():
    updates = [{"update_id": 9, "callback_query": {
        "id": "cb9", "data": "ack:tokZ",
        "message": {"message_id": 77, "chat": {"id": 4242},
                    "text": "ALERT: someone"}}}]
    cb = telegram_poll.parse_ack_callbacks(updates)[0]
    assert (cb["chat_id"], cb["message_id"]) == (4242, 77)
    assert cb["text"] == "ALERT: someone"


def test_parse_messages_extracts_onboarding_chats():
    updates = [
        {"update_id": 5, "callback_query": {"id": "cb1", "data": "ack:t"}},
        {"update_id": 6, "message": {"text": "hi",
                                     "chat": {"id": 42},
                                     "from": {"first_name": "Ada",
                                              "last_name": "L"}}},
        {"update_id": 7, "message": {"text": "x", "chat": {"id": -100500},
                                     "from": {}}},
    ]
    assert telegram_poll.parse_messages(updates) == [
        (6, 42, "Ada L"), (7, -100500, "unknown")]


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
