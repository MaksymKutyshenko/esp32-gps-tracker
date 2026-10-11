# GPS Tracker web app

Live map, track history and device health for the ESP32-S3 tracker in this
repository. One Node process receives the tracker's reports and serves the
dashboard; Postgres keeps the history.

| | |
| --- | --- |
| Server | Node 22, TypeScript, Fastify; plain SQL on Postgres 16 (`pg`), no ORM |
| Client | React 18, Vite, Leaflet (OpenStreetMap tiles), Recharts |
| Live updates | Server-sent events: every report reaches every open tab the moment it lands |
| Sign-in | A shared access key (one input, easy to hand to friends), optionally Google sign-in with an e-mail allow list |
| Device protocol | The firmware's OsmAnd position GET plus a `health=1` GET with every diagnostics counter |

## Run it locally

```bash
cd web
cp .env.example .env      # set AUTH_DISABLED=true for a first look, see below for Google
pnpm install
pnpm db:up                # Postgres 16 in Docker (docker compose up -d db)
pnpm dev                  # API on :5055, UI on :5173 (proxied to the API)
```

Open <http://localhost:5173>. With no device yet, feed the dashboard from the
simulator, which sends exactly what the firmware sends:

```bash
node tools/simulate-device.mjs --history 3h    # three hours of history, then live
```

Point a real tracker at the machine running the server (the API port, not
Vite's), on its serial console:

```
set url http://192.168.1.10:5055
set id my-tracker
save
```

The first health report arrives as soon as the GPRS bearer is up, positions
once there is a fix. The device needs plain HTTP on a routable address; a
SIM800C cannot do TLS, so on the internet put the server behind a host with
a public IP (or a reverse proxy that still accepts HTTP on the device port).

## Sign-in

### Access keys (default)

One login field, two keys:

- **Admin key** (`ADMIN_KEY`, a server secret): the owner. Signing in with
  it adds an *Access key* button to the header, which generates (or sets)
  a new shared viewer key and shows it once together with a share link.
- **Viewer key** (in the database): everyone else. Give it to whoever may
  watch, or hand them `https://your.host/#key=<the key>`, which signs
  them in and drops the key from the address bar (a URL fragment is never
  sent to the server). `ACCESS_KEY` in the environment only seeds the
  database on the first start; after that the dashboard owns it and the
  secret can be removed.

Keys are stored as scrypt hashes, compared in constant time, and an
address is locked out after 10 wrong guesses in 15 minutes. Sessions are
HttpOnly cookies valid for 30 days that carry a fingerprint of the key
generation they were issued for: rotating the viewer key signs every
viewer out within seconds, and a viewer can never rotate it, so the owner
cannot be locked out.

Treat the viewer key like a password to the map: anyone holding it sees
where the tracker is. Rotate it when you stop trusting a holder.

### Google sign-in (optional, for named accounts)

1. In [Google Cloud console → Credentials](https://console.cloud.google.com/apis/credentials)
   create an OAuth client ID of type **Web application**.
2. Under *Authorised JavaScript origins* add `http://localhost:5173` for
   development and `https://your.host` for production. No redirect URI and
   no client secret are needed: the browser gets an ID token and posts it to
   the server, which verifies it against Google's keys.
3. Put the client ID in `.env` as `GOOGLE_CLIENT_ID` and list who may sign
   in in `ALLOWED_EMAILS`.

Both methods can be on at once. Either way set a long random
`SESSION_SECRET` and keep `AUTH_DISABLED=false`; sessions are signed JWT
cookies, the server keeps no session table.

## Production

```bash
cp .env.example .env            # fill in the Google and secret values
docker compose --profile app up -d --build
```

This builds the client and the server into one image and runs it with
Postgres on port 5055, which is the port the tracker reports to and the
port the dashboard is served on. Put a TLS-terminating proxy in front for
the browser if you like, but keep port 5055 reachable over plain HTTP for
the device, or give the device its own port that proxies to the app.

`ALLOWED_DEVICE_IDS` limits which `id` values are accepted; leave it empty
to accept any device, which is how Traccar behaves too.

### On Fly.io (no server, no domain)

`fly.toml` is ready; it turns the HTTPS redirect off so the modem's plain
GET gets through, and keeps one small machine running. Postgres can be
[Neon](https://neon.tech)'s free tier or `fly postgres create`.

```bash
brew install flyctl && fly auth login
fly launch --copy-config --no-deploy        # choose an app name, e.g. my-tracker
fly secrets set DATABASE_URL='postgres://...?sslmode=require' \
  GOOGLE_CLIENT_ID='...' ALLOWED_EMAILS='you@example.com' \
  SESSION_SECRET="$(openssl rand -hex 32)"
fly deploy
curl -i "http://my-tracker.fly.dev/?id=test&lat=50.45&lon=30.52"   # expect 200 OK, no redirect
```

Then on the tracker: `set url http://my-tracker.fly.dev` (no port: Fly
serves the device on 80 and the dashboard on 443). For Google sign-in add
`https://my-tracker.fly.dev` as an authorised origin in the Google console.

### Behind a Cloudflare Tunnel (no public IP)

A *named* tunnel works from a machine at home: `cloudflared/config.yml.example`
has the ingress. Three things matter for the SIM800C:

- The device URL must be `http://tracker.example.com` with **no port**:
  Cloudflare proxies HTTP only on 80, 8080, 8880, 2052, 2082, 2086 and 2095,
  never on 5055. The browser uses `https://tracker.example.com`.
- Leave the zone's *Always Use HTTPS* off, or add a Configuration Rule that
  exempts this hostname, so the modem's plain GET is not redirected.
- Keep Bot Fight Mode off for the hostname; the modem cannot answer a challenge.

The account-less "quick tunnel" (`cloudflared tunnel --url`) is handy for a
one-off test but its API endpoint (api.trycloudflare.com) is blocked on
some networks, and it has no uptime guarantee.

## What the dashboard shows

- **Header**: device, online / offline with the age of the last report,
  time range (1 h … 30 d), light / dark, who is signed in. A device is
  *online* while its last report is younger than `ONLINE_TIMEOUT_S` (150 s
  by default, the firmware reports health every 60 s). When it goes
  offline the marker turns grey and dashed, the trail fades and the value
  tiles dim: what you see is the last known state, not the current one.
- **Map**: the trail for the selected range, the latest position (pulsing
  when online), the range's first point. *Follow* keeps the newest point in
  view. Clicking a row in the Positions table pans to it.
- **Tiles**: latitude, longitude, speed, course, altitude, HDOP, satellites
  used / tracked / in view, strongest C/N0, tracker state, battery, fix
  age, GSM signal.
- **Modules**: one card per firmware module with a verdict and the reason:
  Device (reset reason, uptime, reboots), GNSS receiver (state, sky view,
  rejected sentences, silences), MicroSD log (mounted, write errors,
  remounts, drops), GPRS uplink (CSQ, failures, power cycles, backlog),
  Display and button (display errors, bus timeouts), Battery (voltage),
  Memory and tasks (heap low-water mark, stack headroom per task). The
  verdict uses the *change* between the last two health reports, so an
  old error that has stopped recurring shows as a lifetime count in the
  details, not as a live warning.
- **Charts**: speed, battery, satellites, C/N0, CSQ, upload failures and
  backlog, rejected sentences, card/display/bus errors, free heap,
  uptime, lowest stack headroom. All over the selected range, one axis
  each, hover for the values.
- **Positions / Health reports**: every row in range, newest first. A
  health row expands to every counter with its label and the short key the
  firmware sent.

## Device protocol

Position (OsmAnd, unchanged; Traccar accepts the same request):

```
GET /?id=<device>&lat=..&lon=..&timestamp=<unix>&speed=<kn>&bearing=..&altitude=<m>&hdop=..&batt=<%>
```

Health, once per `health_ms` (default 60 s), built by
`include/transport/HealthReport.hpp`:

```
GET /?id=<device>&health=1&fw=0.2.0&up=<s>&rst=<poweron|sw|panic|intwdt|taskwdt|wdt|deepsleep|brownout|other>
    &st=<boot|nosignal|acquiring|tracking|stationary|sleeping>
    &fix=0/1&sat=&sky=&sig=&cn0=&q=              GNSS: used, in view, tracked, strongest C/N0, GGA quality
    &nrx=&nrej=&ncks=&nfld=&sil=                 NMEA received, rejected (checksum, fields), receiver silences
    &sd=0/1&lok=&ldrop=&lerr=&lrm=               card mounted, records logged, dropped, write errors, remounts
    &uok=&udrop=&ufail=&ucyc=&uq=&csq=           uploaded, dropped, failures, modem power cycles, backlog, CSQ
    &derr=&bto=&berr=&bdrop=                     display errors, bus timeouts, battery read errors, button drops
    &bmv=&heap=&hmin=&stk=gps/display/logger/uplink/ui   battery mV, free heap, min free heap, stack free per task
```

The server answers `200 OK` for anything it stored (or had already stored:
a position whose acknowledgement was lost is deduplicated on `fix_time`),
`400` for a malformed report and `403` for a device not on the allow list.
The firmware pops a record from its queue only on 200.

## Layout

```
web/
  shared/types.ts        types and key tables shared by server and client
  server/src
    index.ts, app.ts     bootstrap, Fastify wiring, static client
    config.ts            environment
    db.ts, migrations/   Postgres pool and SQL migrations
    ingest.ts            query string -> typed report (pure, tested)
    status.ts            two health reports -> module verdicts (pure, tested)
    repo.ts              all SQL
    auth.ts              Google ID token, session cookie
    events.ts            SSE hub
    routes/              /ingest and /, /api/*
  client/src
    App.tsx              session gate
    components/          Dashboard, Header, MapView, FixStrip, ModulesPanel, Charts, tables, Login
    hooks.ts, api.ts, format.ts, styles.css
  tools/simulate-device.mjs
```

```bash
pnpm test        # server unit tests (ingest parser, module verdicts)
pnpm typecheck   # both packages
pnpm build       # client to client/dist, server to server/dist
```
