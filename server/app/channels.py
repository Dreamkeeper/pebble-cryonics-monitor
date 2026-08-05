"""Outbound channel plugins.

Each channel delivers an alert message to one contact and returns whether
the transport accepted it (transport ACK != human ACK; human ACKs arrive
via the /ack endpoint or the Telegram callback).

V1 channels: telegram, email (SMTP), ntfy. Later (plugin interface stays):
twilio-class telephony, SIP, gateway-phone (an old Android handset running
the companion app in gateway role, taking send-SMS/place-call commands).
"""
from __future__ import annotations

import abc
import json
import smtplib
import urllib.request
from email.message import EmailMessage

from .escalation import Send


class Channel(abc.ABC):
    name: str

    @abc.abstractmethod
    async def deliver(self, send: Send, text: str, ack_url: str) -> bool:
        """Deliver; return True if the transport accepted the message."""


class TelegramChannel(Channel):
    """Telegram bot with an inline 'I acknowledge' button."""
    name = "telegram"

    def __init__(self, bot_token: str, chat_ids: dict[str, str]):
        self.bot_token = bot_token
        self.chat_ids = chat_ids  # contact_id -> chat_id

    async def deliver(self, send: Send, text: str, ack_url: str) -> bool:
        chat_id = self.chat_ids.get(send.contact.id)
        if not chat_id:
            return False
        payload = {
            "chat_id": chat_id,
            "text": text,
            "reply_markup": {
                "inline_keyboard": [[{
                    "text": "✅ I acknowledge — I'm on it",
                    "callback_data": f"ack:{send.contact.id}",
                }]]
            },
        }
        return await _post_json(
            f"https://api.telegram.org/bot{self.bot_token}/sendMessage", payload)


class NtfyChannel(Channel):
    """Self-hostable push via ntfy.sh or a private ntfy server."""
    name = "ntfy"

    def __init__(self, base_url: str, topics: dict[str, str]):
        self.base_url = base_url.rstrip("/")
        self.topics = topics  # contact_id -> topic

    async def deliver(self, send: Send, text: str, ack_url: str) -> bool:
        topic = self.topics.get(send.contact.id)
        if not topic:
            return False
        req = urllib.request.Request(
            f"{self.base_url}/{topic}",
            data=text.encode(),
            headers={
                "Priority": "urgent" if send.kind.value == "watch_alarm" else "high",
                "Tags": "rotating_light",
                "Actions": f"view, Acknowledge, {ack_url}",
            })
        return _urlopen_ok(req)


class EmailChannel(Channel):
    name = "email"

    def __init__(self, smtp_host: str, smtp_port: int, sender: str,
                 addresses: dict[str, str], username: str = "", password: str = ""):
        self.smtp_host, self.smtp_port = smtp_host, smtp_port
        self.sender = sender
        self.addresses = addresses  # contact_id -> email
        self.username, self.password = username, password

    async def deliver(self, send: Send, text: str, ack_url: str) -> bool:
        addr = self.addresses.get(send.contact.id)
        if not addr:
            return False
        msg = EmailMessage()
        msg["From"] = self.sender
        msg["To"] = addr
        prefix = "[TEST] " if send.kind.value == "test" else "[ALERT] "
        msg["Subject"] = prefix + "Cryonics Monitor"
        msg.set_content(f"{text}\n\nAcknowledge: {ack_url}")
        try:
            with smtplib.SMTP(self.smtp_host, self.smtp_port, timeout=15) as s:
                if self.username:
                    s.starttls()
                    s.login(self.username, self.password)
                s.send_message(msg)
            return True
        except OSError:
            return False


async def _post_json(url: str, payload: dict) -> bool:
    req = urllib.request.Request(
        url, data=json.dumps(payload).encode(),
        headers={"Content-Type": "application/json"})
    return _urlopen_ok(req)


def _urlopen_ok(req: urllib.request.Request) -> bool:
    try:
        with urllib.request.urlopen(req, timeout=15) as resp:
            return 200 <= resp.status < 300
    except OSError:
        return False
