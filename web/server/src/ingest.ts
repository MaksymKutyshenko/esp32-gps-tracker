// Parses what the device sends: the OsmAnd position GET
//   /?id=..&lat=..&lon=..&timestamp=..&speed=..&bearing=..&altitude=..&hdop=..&batt=..
// and the firmware's health GET (same shape, health=1, see HealthReport.hpp).
// Pure: no I/O, unit-tested.

export interface PositionReport {
  kind: 'position';
  deviceId: string;
  fixTime: Date;
  lat: number;
  lon: number;
  altitudeM: number | null;
  speedKn: number | null;
  bearingDeg: number | null;
  hdop: number | null;
  batteryPct: number | null;
  extra: Record<string, string>;
}

export interface HealthReportIn {
  kind: 'health';
  deviceId: string;
  // When the device stamped the report itself (ts=unix seconds); the
  // firmware does not today, the simulator does when it backfills.
  reportedAt: Date | null;
  uptimeS: number | null;
  resetReason: string | null;
  state: string | null;
  batteryMv: number | null;
  firmware: string | null;
  metrics: Record<string, number>;
  stackFree: number[] | null;
}

export interface InvalidReport {
  kind: 'invalid';
  reason: string;
}

export type Report = PositionReport | HealthReportIn | InvalidReport;

const POSITION_KEYS = new Set(['id', 'lat', 'lon', 'timestamp', 'speed', 'bearing', 'altitude', 'hdop', 'batt']);
const HEALTH_TEXT_KEYS = new Set(['id', 'health', 'fw', 'rst', 'st', 'stk', 'ts']);
const DEVICE_ID = /^[A-Za-z0-9_.:-]{1,64}$/;

function num(value: string | undefined): number | null {
  if (value === undefined || value === '') return null;
  const n = Number(value);
  return Number.isFinite(n) ? n : null;
}

// OsmAnd clients send unix seconds; some send milliseconds or ISO 8601.
function parseTimestamp(value: string | undefined, now: Date): Date | null {
  if (value === undefined || value === '') return now;
  const n = Number(value);
  if (Number.isFinite(n)) {
    const ms = n > 1e11 ? n : n * 1000;
    return ms > 0 ? new Date(ms) : null;
  }
  const parsed = Date.parse(value);
  return Number.isNaN(parsed) ? null : new Date(parsed);
}

export function parseReport(query: Record<string, unknown>, now = new Date()): Report {
  const q: Record<string, string> = {};
  for (const [k, v] of Object.entries(query)) {
    if (typeof v === 'string') q[k] = v;
    else if (Array.isArray(v) && typeof v[0] === 'string') q[k] = v[0];
  }
  const deviceId = q.id;
  if (!deviceId || !DEVICE_ID.test(deviceId)) return { kind: 'invalid', reason: 'missing or malformed id' };

  if (q.health === '1') return parseHealth(deviceId, q, now);

  const lat = num(q.lat);
  const lon = num(q.lon);
  if (lat === null || lon === null || Math.abs(lat) > 90 || Math.abs(lon) > 180) {
    return { kind: 'invalid', reason: 'missing or out-of-range lat/lon' };
  }
  const fixTime = parseTimestamp(q.timestamp, now);
  if (!fixTime) return { kind: 'invalid', reason: 'unreadable timestamp' };

  const extra: Record<string, string> = {};
  for (const [k, v] of Object.entries(q)) if (!POSITION_KEYS.has(k)) extra[k] = v;

  return {
    kind: 'position',
    deviceId,
    fixTime,
    lat,
    lon,
    altitudeM: num(q.altitude),
    speedKn: num(q.speed),
    bearingDeg: num(q.bearing),
    hdop: num(q.hdop),
    batteryPct: num(q.batt),
    extra,
  };
}

function parseHealth(deviceId: string, q: Record<string, string>, now: Date): HealthReportIn {
  // A device clock is trusted only when it is plausible: not in the future
  // and not older than a month.
  const stamped = q.ts ? parseTimestamp(q.ts, now) : null;
  const reportedAt =
    stamped && stamped.getTime() <= now.getTime() + 60_000 && now.getTime() - stamped.getTime() < 31 * 86400_000
      ? stamped
      : null;
  const metrics: Record<string, number> = {};
  for (const [k, v] of Object.entries(q)) {
    if (HEALTH_TEXT_KEYS.has(k)) continue;
    const n = num(v);
    if (n !== null) metrics[k] = n;
  }
  let stackFree: number[] | null = null;
  if (q.stk) {
    const parts = q.stk.split('/').map((p) => num(p));
    if (parts.every((p) => p !== null)) stackFree = parts as number[];
  }
  return {
    kind: 'health',
    deviceId,
    reportedAt,
    uptimeS: num(q.up),
    resetReason: q.rst || null,
    state: q.st || null,
    batteryMv: num(q.bmv),
    firmware: q.fw || null,
    metrics,
    stackFree,
  };
}
