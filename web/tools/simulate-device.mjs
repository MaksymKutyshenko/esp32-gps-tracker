#!/usr/bin/env node
// Pretends to be the tracker: sends the same GETs the firmware sends
// (OsmAnd positions and health=1 reports) so the web app can be developed
// without the board.
//
//   node tools/simulate-device.mjs                       # live: a lap around Kyiv, 1 position / 5 s, health / 60 s
//   node tools/simulate-device.mjs --history 3h          # backfill 3 h of history first, then go live
//   node tools/simulate-device.mjs --url http://host:5055 --id my-tracker --once
//
// The simulated device has a watchdog reset 40 min into the history, a
// weak-signal stretch and a few upload failures, so every card on the
// dashboard has something to say.

const args = Object.fromEntries(
  process.argv.slice(2).map((a, i, all) => (a.startsWith('--') ? [a.slice(2), all[i + 1]?.startsWith('--') || all[i + 1] === undefined ? true : all[i + 1]] : [])).filter((x) => x.length),
);
const url = (args.url ?? 'http://localhost:5055').replace(/\/$/, '');
const id = args.id ?? 'gps-tracker-01';
const positionEveryMs = Number(args.position ?? 5) * 1000;
const healthEveryMs = Number(args.health ?? 60) * 1000;
const historyMs = parseDuration(args.history);

function parseDuration(text) {
  if (!text || text === true) return 0;
  const m = /^(\d+)(s|m|h|d)$/.exec(text);
  if (!m) throw new Error(`bad duration ${text}`);
  return Number(m[1]) * { s: 1000, m: 60_000, h: 3_600_000, d: 86_400_000 }[m[2]];
}

// A loop around the centre of Kyiv (Khreshchatyk - Dnipro embankment).
const LOOP = [
  [50.4501, 30.5234], [50.4472, 30.5262], [50.4440, 30.5290], [50.4415, 30.5335], [50.4420, 30.5420],
  [50.4460, 30.5470], [50.4510, 30.5450], [50.4550, 30.5380], [50.4560, 30.5300], [50.4535, 30.5250],
];
const LAP_MS = 20 * 60_000;

function pointAt(t) {
  const phase = ((t % LAP_MS) + LAP_MS) % LAP_MS / LAP_MS;
  const i = Math.floor(phase * LOOP.length);
  const f = phase * LOOP.length - i;
  const a = LOOP[i];
  const b = LOOP[(i + 1) % LOOP.length];
  const lat = a[0] + (b[0] - a[0]) * f;
  const lon = a[1] + (b[1] - a[1]) * f;
  const bearing = (Math.atan2(b[1] - a[1], b[0] - a[0]) * 180) / Math.PI;
  return { lat, lon, bearing: (bearing + 360) % 360 };
}

// Device life: boot at t0, task-watchdog reset 40 min later.
const now = Date.now();
const t0 = now - Math.max(historyMs, 10 * 60_000);
const resetAt = t0 + 40 * 60_000;
const parkedFrom = now - 6 * 60_000; // parked for the last 6 minutes

function stateAt(t) {
  if (t < t0 + 90_000) return 'acquiring';
  return t >= parkedFrom ? 'stationary' : 'tracking';
}

function moving(t) {
  return stateAt(t) === 'tracking';
}

function uptimeAt(t) {
  return Math.floor((t - (t >= resetAt ? resetAt : t0)) / 1000);
}

function counters(t) {
  const up = uptimeAt(t);
  const weak = t > now - 50 * 60_000 && t < now - 35 * 60_000; // weak GSM for 15 min
  const sincePark = Math.max(0, Math.floor((t - parkedFrom) / 1000));
  const movingS = Math.max(0, up - (t >= parkedFrom ? sincePark : 0));
  return {
    up,
    rst: t >= resetAt ? 'taskwdt' : 'poweron',
    st: stateAt(t),
    fix: stateAt(t) === 'acquiring' ? 0 : 1,
    sat: stateAt(t) === 'acquiring' ? 3 : 8 + Math.round(2 * Math.sin(t / 300_000)),
    sky: 12 + Math.round(2 * Math.cos(t / 400_000)),
    sig: stateAt(t) === 'acquiring' ? 4 : 9 + Math.round(Math.sin(t / 200_000)),
    cn0: stateAt(t) === 'acquiring' ? 31 : 40 + Math.round(3 * Math.sin(t / 250_000)),
    q: stateAt(t) === 'acquiring' ? 0 : 1,
    nrx: up * 3,
    nrej: Math.floor(up / 600),
    ncks: Math.floor(up / 900),
    nfld: Math.floor(up / 1800),
    sil: 0,
    sd: 1,
    lok: movingS + Math.floor(sincePark / 60),
    ldrop: 0,
    lerr: t > now - 20 * 60_000 && t < now - 15 * 60_000 ? 2 : t >= now - 15 * 60_000 ? 2 : 0,
    lrm: 0,
    uok: Math.floor(up / 5) - (weak ? 20 : 0),
    udrop: 0,
    ufail: (weak ? Math.floor((Math.min(t, now - 35 * 60_000) - (now - 50 * 60_000)) / 60_000) : 0) + (t >= now - 35 * 60_000 ? 15 : 0),
    ucyc: t >= now - 35 * 60_000 ? 1 : 0,
    uq: weak ? 30 : moving(t) ? 2 : 0,
    csq: weak ? 6 : 17 + Math.round(3 * Math.sin(t / 150_000)),
    derr: 0,
    bto: Math.floor(up / 3600),
    berr: 0,
    bdrop: 0,
    bmv: Math.round(4100 - ((t - t0) / 3_600_000) * 60),
    heap: 205_000 + Math.round(3000 * Math.sin(t / 100_000)),
    hmin: 178_000,
    stk: [1180, 940, 2210, 2480, 1520].map((s) => s - Math.round(40 * Math.abs(Math.sin(t / 700_000)))).join('/'),
  };
}

async function send(query, when) {
  const q = new URLSearchParams(query).toString();
  const res = await fetch(`${url}/?id=${encodeURIComponent(id)}&${q}`);
  const body = await res.text();
  if (res.status !== 200) console.error(`${new Date(when).toISOString()} -> HTTP ${res.status} ${body}`);
  return res.status;
}

function positionQuery(t) {
  const p = pointAt(t);
  const mv = moving(t);
  const speedKmh = mv ? 32 + 12 * Math.sin(t / 60_000) : 0;
  return {
    lat: p.lat.toFixed(7),
    lon: p.lon.toFixed(7),
    timestamp: Math.floor(t / 1000),
    speed: (speedKmh / 1.852).toFixed(2),
    bearing: mv ? p.bearing.toFixed(1) : '0.0',
    altitude: (120 + 30 * Math.sin(t / 400_000)).toFixed(2),
    hdop: (0.8 + 0.3 * Math.abs(Math.sin(t / 90_000))).toFixed(2),
    batt: Math.round(((counters(t).bmv - 3300) * 100) / 900),
  };
}

function healthQuery(t) {
  // ts= lets a backfilled report keep its time; the firmware itself never sends it.
  return { health: 1, fw: '0.2.0', ts: Math.floor(t / 1000), ...counters(t) };
}

async function backfill() {
  let sent = 0;
  for (let t = t0; t < now; t += healthEveryMs) {
    if (stateAt(t) !== 'acquiring' || t > t0 + 60_000) {
      // Health first (it precedes the first fix on a real boot).
    }
    await send(healthQuery(t), t);
    sent++;
    const step = moving(t) ? positionEveryMs : 60_000;
    for (let p = t; p < Math.min(t + healthEveryMs, now); p += step) {
      if (stateAt(p) === 'acquiring') continue;
      await send(positionQuery(p), p);
      sent++;
    }
  }
  console.log(`backfilled ${sent} reports covering ${Math.round(historyMs / 60_000)} min`);
}

async function live() {
  console.log(`live: position every ${positionEveryMs / 1000} s, health every ${healthEveryMs / 1000} s -> ${url} as ${id}`);
  let lastHealth = 0;
  for (;;) {
    const t = Date.now();
    if (t - lastHealth >= healthEveryMs) {
      lastHealth = t;
      await send(healthQuery(t), t);
    }
    if (stateAt(t) !== 'acquiring') await send(positionQuery(t), t);
    if (args.once) return;
    await new Promise((r) => setTimeout(r, positionEveryMs));
  }
}

if (historyMs > 0) await backfill();
await live();
