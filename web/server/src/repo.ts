// All SQL in one place. Rows come back as the shared types.

import type { DeviceSummary, HealthReport, Position } from '../../shared/types.js';
import type { UptimeRow } from './boots.js';
import { pool } from './db.js';
import type { HealthReportIn, PositionReport } from './ingest.js';
import { isOnline } from './status.js';

interface PositionRow {
  id: string;
  device_id: string;
  fix_time: Date;
  received_at: Date;
  lat: number;
  lon: number;
  altitude_m: number | null;
  speed_kn: number | null;
  bearing_deg: number | null;
  hdop: number | null;
  battery_pct: number | null;
  extra: Record<string, string>;
}

interface HealthRow {
  id: string;
  device_id: string;
  received_at: Date;
  uptime_s: number | null;
  reset_reason: string | null;
  state: string | null;
  battery_mv: number | null;
  firmware: string | null;
  metrics: Record<string, number>;
  stack_free: number[] | null;
}

interface DeviceRow {
  id: string;
  name: string | null;
  first_seen: Date;
  last_seen: Date;
}

const POSITION_COLUMNS =
  'id, device_id, fix_time, received_at, lat, lon, altitude_m, speed_kn, bearing_deg, hdop, battery_pct, extra';
const HEALTH_COLUMNS =
  'id, device_id, received_at, uptime_s, reset_reason, state, battery_mv, firmware, metrics, stack_free';

function toPosition(r: PositionRow): Position {
  return {
    id: Number(r.id),
    deviceId: r.device_id,
    fixTime: r.fix_time.toISOString(),
    receivedAt: r.received_at.toISOString(),
    lat: r.lat,
    lon: r.lon,
    altitudeM: r.altitude_m,
    speedKn: r.speed_kn,
    bearingDeg: r.bearing_deg,
    hdop: r.hdop,
    batteryPct: r.battery_pct,
    extra: r.extra ?? {},
  };
}

function toHealth(r: HealthRow): HealthReport {
  return {
    id: Number(r.id),
    deviceId: r.device_id,
    receivedAt: r.received_at.toISOString(),
    uptimeS: r.uptime_s,
    resetReason: r.reset_reason,
    state: r.state,
    batteryMv: r.battery_mv,
    firmware: r.firmware,
    metrics: r.metrics ?? {},
    stackFree: r.stack_free,
  };
}

export async function touchDevice(id: string, seenAt: Date): Promise<void> {
  await pool.query(
    `INSERT INTO devices (id, first_seen, last_seen) VALUES ($1, $2, $2)
     ON CONFLICT (id) DO UPDATE SET last_seen = GREATEST(devices.last_seen, EXCLUDED.last_seen)`,
    [id, seenAt],
  );
}

// Null when this fix was already stored (a re-sent record).
export async function insertPosition(p: PositionReport, receivedAt: Date): Promise<Position | null> {
  const { rows } = await pool.query<PositionRow>(
    `INSERT INTO positions (device_id, fix_time, received_at, lat, lon, altitude_m, speed_kn, bearing_deg, hdop, battery_pct, extra)
     VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11)
     ON CONFLICT (device_id, fix_time) DO NOTHING
     RETURNING ${POSITION_COLUMNS}`,
    [p.deviceId, p.fixTime, receivedAt, p.lat, p.lon, p.altitudeM, p.speedKn, p.bearingDeg, p.hdop, p.batteryPct, p.extra],
  );
  return rows[0] ? toPosition(rows[0]) : null;
}

export async function insertHealth(h: HealthReportIn, receivedAt: Date): Promise<HealthReport> {
  const { rows } = await pool.query<HealthRow>(
    `INSERT INTO health_reports (device_id, received_at, uptime_s, reset_reason, state, battery_mv, firmware, metrics, stack_free)
     VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9)
     RETURNING ${HEALTH_COLUMNS}`,
    [h.deviceId, receivedAt, h.uptimeS, h.resetReason, h.state, h.batteryMv, h.firmware, h.metrics, h.stackFree],
  );
  return toHealth(rows[0]);
}

export async function getDevice(id: string): Promise<DeviceRow | null> {
  const { rows } = await pool.query<DeviceRow>('SELECT id, name, first_seen, last_seen FROM devices WHERE id = $1', [id]);
  return rows[0] ?? null;
}

export async function latestPosition(deviceId: string): Promise<Position | null> {
  const { rows } = await pool.query<PositionRow>(
    `SELECT ${POSITION_COLUMNS} FROM positions WHERE device_id = $1 ORDER BY fix_time DESC LIMIT 1`,
    [deviceId],
  );
  return rows[0] ? toPosition(rows[0]) : null;
}

// Newest first.
export async function latestHealth(deviceId: string, count: number): Promise<HealthReport[]> {
  const { rows } = await pool.query<HealthRow>(
    `SELECT ${HEALTH_COLUMNS} FROM health_reports WHERE device_id = $1 ORDER BY received_at DESC, id DESC LIMIT $2`,
    [deviceId, count],
  );
  return rows.map(toHealth);
}

export async function deviceSummary(id: string, now: Date, onlineTimeoutS: number): Promise<DeviceSummary | null> {
  const device = await getDevice(id);
  if (!device) return null;
  const [position, health] = await Promise.all([latestPosition(id), latestHealth(id, 1)]);
  return {
    id: device.id,
    name: device.name,
    firstSeen: device.first_seen.toISOString(),
    lastSeen: device.last_seen.toISOString(),
    online: isOnline(device.last_seen, now, onlineTimeoutS),
    state: health[0]?.state ?? null,
    lastPosition: position,
  };
}

export async function listDevices(now: Date, onlineTimeoutS: number): Promise<DeviceSummary[]> {
  const { rows } = await pool.query<DeviceRow>('SELECT id, name, first_seen, last_seen FROM devices ORDER BY last_seen DESC');
  const summaries = await Promise.all(rows.map((r) => deviceSummary(r.id, now, onlineTimeoutS)));
  return summaries.filter((s): s is DeviceSummary => s !== null);
}

export interface RangeQuery {
  from: Date;
  to: Date;
  limit: number;
}

// Oldest first, so the client can draw the trail in order.
export async function positionsInRange(deviceId: string, r: RangeQuery): Promise<Position[]> {
  const { rows } = await pool.query<PositionRow>(
    `SELECT ${POSITION_COLUMNS} FROM (
       SELECT ${POSITION_COLUMNS} FROM positions
       WHERE device_id = $1 AND fix_time >= $2 AND fix_time <= $3
       ORDER BY fix_time DESC LIMIT $4
     ) newest ORDER BY fix_time ASC`,
    [deviceId, r.from, r.to, r.limit],
  );
  return rows.map(toPosition);
}

export async function healthInRange(deviceId: string, r: RangeQuery): Promise<HealthReport[]> {
  const { rows } = await pool.query<HealthRow>(
    `SELECT ${HEALTH_COLUMNS} FROM (
       SELECT ${HEALTH_COLUMNS} FROM health_reports
       WHERE device_id = $1 AND received_at >= $2 AND received_at <= $3
       ORDER BY received_at DESC LIMIT $4
     ) newest ORDER BY received_at ASC`,
    [deviceId, r.from, r.to, r.limit],
  );
  return rows.map(toHealth);
}

// Just what the boot statistics need, so a month of reports stays cheap.
export async function uptimeRows(deviceId: string, r: RangeQuery): Promise<UptimeRow[]> {
  const { rows } = await pool.query<{ received_at: Date; uptime_s: number | null; reset_reason: string | null; firmware: string | null }>(
    `SELECT received_at, uptime_s, reset_reason, firmware FROM (
       SELECT received_at, uptime_s, reset_reason, firmware FROM health_reports
       WHERE device_id = $1 AND received_at >= $2 AND received_at <= $3
       ORDER BY received_at DESC LIMIT $4
     ) newest ORDER BY received_at ASC`,
    [deviceId, r.from, r.to, r.limit],
  );
  return rows.map((row) => ({ receivedAt: row.received_at, uptimeS: row.uptime_s, resetReason: row.reset_reason, firmware: row.firmware }));
}
