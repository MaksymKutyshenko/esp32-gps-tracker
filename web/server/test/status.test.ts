import assert from 'node:assert/strict';
import { test } from 'node:test';

import type { HealthReport, Position } from '../../shared/types.js';
import { deriveModules, isOnline } from '../src/status.js';

const now = new Date('2026-10-11T10:00:00Z');

function health(overrides: Partial<HealthReport> & { metrics?: Record<string, number> }): HealthReport {
  return {
    id: 1,
    deviceId: 'd',
    receivedAt: new Date(now.getTime() - 20_000).toISOString(),
    uptimeS: 600,
    resetReason: 'poweron',
    state: 'tracking',
    batteryMv: 3900,
    firmware: '0.2.0',
    stackFree: [1200, 900, 2100, 2400, 1500],
    ...overrides,
    metrics: {
      fix: 1, sat: 8, sky: 12, sig: 9, cn0: 40, q: 1, nrx: 1800, nrej: 0, ncks: 0, nfld: 0, sil: 0,
      sd: 1, lok: 600, ldrop: 0, lerr: 0, lrm: 0, uok: 120, udrop: 0, ufail: 0, ucyc: 0, uq: 0, csq: 18,
      derr: 0, bto: 0, berr: 0, bdrop: 0, bmv: 3900, heap: 200000, hmin: 150000,
      ...(overrides.metrics ?? {}),
    },
  };
}

const position: Position = {
  id: 1, deviceId: 'd', fixTime: new Date(now.getTime() - 5000).toISOString(), receivedAt: now.toISOString(),
  lat: 50.45, lon: 30.52, altitudeM: 179, speedKn: 10, bearingDeg: 90, hdop: 0.9, batteryPct: 66, extra: {},
};

function byKey(modules: ReturnType<typeof deriveModules>, key: string) {
  const m = modules.find((x) => x.key === key);
  assert.ok(m, `module ${key}`);
  return m;
}

test('online is a window after the last report', () => {
  assert.equal(isOnline(new Date(now.getTime() - 100_000), now, 150), true);
  assert.equal(isOnline(new Date(now.getTime() - 200_000), now, 150), false);
  assert.equal(isOnline(null, now, 150), false);
});

test('a healthy tracking device is all green', () => {
  const latest = health({});
  const previous = health({ id: 0, uptimeS: 540, metrics: { nrx: 1620, lok: 540, uok: 108 } });
  const modules = deriveModules({ latest, previous, lastPosition: position, lastSeen: new Date(latest.receivedAt), now, onlineTimeoutS: 150 });
  for (const m of modules) assert.equal(m.level, 'ok', `${m.key}: ${m.issues.join('; ')}`);
  assert.equal(byKey(modules, 'device').summary, 'Online');
  assert.equal(byKey(modules, 'gnss').summary, 'Tracking');
});

test('silence makes the device offline and an error', () => {
  const latest = health({ receivedAt: new Date(now.getTime() - 600_000).toISOString() });
  const modules = deriveModules({ latest, previous: null, lastPosition: null, lastSeen: new Date(latest.receivedAt), now, onlineTimeoutS: 150 });
  const device = byKey(modules, 'device');
  assert.equal(device.level, 'error');
  assert.equal(device.summary, 'Offline');
});

test('watchdog reset and reboot between reports are flagged', () => {
  const latest = health({ resetReason: 'taskwdt', uptimeS: 30 });
  const previous = health({ id: 0, uptimeS: 900 });
  const device = byKey(deriveModules({ latest, previous, lastPosition: null, lastSeen: now, now, onlineTimeoutS: 150 }), 'device');
  assert.equal(device.level, 'warn');
  assert.ok(device.issues.some((i) => i.includes('Task watchdog')));
  assert.ok(device.issues.some((i) => i.includes('Rebooted')));
});

test('counter deltas drive the module verdicts', () => {
  const previous = health({ id: 0, uptimeS: 540 });
  const latest = health({ metrics: { lerr: 3, ufail: 2, ucyc: 1, bto: 4, nrej: 500, nrx: 1900 } });
  const modules = deriveModules({ latest, previous, lastPosition: position, lastSeen: now, now, onlineTimeoutS: 150 });
  assert.equal(byKey(modules, 'storage').level, 'warn');
  assert.equal(byKey(modules, 'uplink').level, 'error');
  assert.equal(byKey(modules, 'ui').level, 'warn');
  assert.ok(byKey(modules, 'gnss').issues.some((i) => i.includes('baud')));
});

test('a reboot between reports does not produce negative deltas', () => {
  const previous = health({ id: 0, uptimeS: 900, metrics: { lerr: 10 } });
  const latest = health({ uptimeS: 30, metrics: { lerr: 0 } });
  const storage = byKey(deriveModules({ latest, previous, lastPosition: null, lastSeen: now, now, onlineTimeoutS: 150 }), 'storage');
  assert.equal(storage.level, 'ok');
});

test('card missing, weak signal, low battery, low stack', () => {
  const latest = health({ batteryMv: 3350, stackFree: [300, 900, 2100, 2400, 1500], metrics: { sd: 0, csq: 5, hmin: 10000 } });
  const modules = deriveModules({ latest, previous: null, lastPosition: null, lastSeen: now, now, onlineTimeoutS: 150 });
  assert.equal(byKey(modules, 'storage').level, 'error');
  assert.equal(byKey(modules, 'uplink').level, 'warn');
  assert.equal(byKey(modules, 'battery').level, 'error');
  const system = byKey(modules, 'system');
  assert.equal(system.level, 'warn');
  assert.ok(system.issues.some((i) => i.includes('gps task')));
});

test('no health report yet: unknown modules, GNSS from positions', () => {
  const modules = deriveModules({ latest: null, previous: null, lastPosition: position, lastSeen: now, now, onlineTimeoutS: 150 });
  assert.equal(byKey(modules, 'storage').level, 'unknown');
  assert.equal(byKey(modules, 'gnss').summary, 'Reporting positions');
  assert.equal(byKey(modules, 'battery').summary, '66 %');
});
