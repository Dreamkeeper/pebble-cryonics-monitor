# Deploying the server on a Synology NAS

Tested against DSM 7.2 with **Container Manager** (the successor of the
Docker package). Any x86-64 or ARM64 Synology that supports Container
Manager works; the server is a single small Python container.

## 1. Copy the server folder

Copy `server/` to the NAS, e.g. to `/volume1/docker/cryomonitor/`
(File Station upload, or `git clone` via SSH).

```bash
ssh admin@nas
mkdir -p /volume1/docker/cryomonitor
# then upload the server/ directory contents there
```

## 2. Configure secrets

```bash
cd /volume1/docker/cryomonitor
cp .env.example .env
vi .env        # set CM_API_TOKEN (openssl rand -hex 32), Telegram token, SMTP
```

## 3. Create the project in Container Manager

Container Manager → **Project** → **Create**:
- Project name: `cryomonitor`
- Path: `/volume1/docker/cryomonitor`
- Source: *Use existing docker-compose.yml*

Build + start. Two containers come up:
- `monitor` on port **8080** (the API + dashboard)
- `ntfy` on port **8090** (self-hosted push; optional — remove from
  docker-compose.yml if you use ntfy.sh or Telegram only)

CLI alternative: `docker compose up -d --build` over SSH.

## 4. HTTPS via DSM reverse proxy (recommended)

The companion app talks to the server over the internet — put DSM's
reverse proxy with a Let's Encrypt certificate in front:

Control Panel → Login Portal → Advanced → **Reverse Proxy** → Create:
- Source: HTTPS, `cryomonitor.your-ddns.synology.me`, port 443
- Destination: HTTP, `localhost`, port 8080

Control Panel → Security → Certificate: issue/assign a Let's Encrypt
certificate for that hostname. Use DSM's DDNS
(Control Panel → External Access → DDNS) if you have no static IP, and
forward port 443 on your router to the NAS.

## 5. Point the Android companion at it

In the companion app settings:
- Server URL: `https://cryomonitor.your-ddns.synology.me`
- API token: the `CM_API_TOKEN` value

Then run a **fire-drill TEST alarm** from the app and confirm the
escalation messages arrive and the dashboard
(`https://…/api/v1/status`) shows the event.

## 6. Keep it alive

- Container Manager restarts the containers on boot
  (`restart: unless-stopped` is set in the compose file).
- Add an uptime check (e.g. UptimeRobot on `/api/v1/status`, or a second
  ntfy topic) so YOU are alerted when the NAS or tunnel is down — the
  phone-side mutual watchdog will also warn the wearer.
- DSM auto-update: keep minor updates on; Container Manager projects
  survive DSM restarts.

## Notes

- Data is currently in-memory (v0.1 scaffold); SQLite persistence lands in
  M2 and will live in the mounted `./data` volume, surviving container
  rebuilds.
- The `ntfy` container needs no account: contacts install the ntfy app and
  subscribe to their topic URL, e.g. `https://nas:8090/cm-relative1`.
