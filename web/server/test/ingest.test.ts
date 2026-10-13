import assert from 'node:assert/strict';
import { test } from 'node:test';

import { parseReport } from '../src/ingest.js';

const now = new Date('2026-10-11T10:00:00Z');

test('position report as the firmware sends it', () => {
  const r = parseReport(
    {
      id: 'gps-tracker-01', lat: '50.4520575', lon: '30.5257202', timestamp: '1791541845',
      speed: '12.34', bearing: '210.5', altitude: '179.30', hdop: '0.95', batt: '66',
    },
    now,
  );
  assert.equal(r.kind, 'position');
  if (r.kind !== 'position') return;
  assert.equal(r.deviceId, 'gps-tracker-01');
  assert.equal(r.fixTime.toISOString(), '2026-10-09T10:30:45.000Z');
  assert.equal(r.lat, 50.4520575);
  assert.equal(r.lon, 30.5257202);
  assert.equal(r.speedKn, 12.34);
  assert.equal(r.bearingDeg, 210.5);
  assert.equal(r.altitudeM, 179.3);
  assert.equal(r.hdop, 0.95);
  assert.equal(r.batteryPct, 66);
  assert.deepEqual(r.extra, {});
});

test('position without timestamp uses the receive time; unknown keys go to extra', () => {
  const r = parseReport({ id: 'x', lat: '1', lon: '2', sat: '7' }, now);
  assert.equal(r.kind, 'position');
  if (r.kind !== 'position') return;
  assert.equal(r.fixTime, now);
  assert.deepEqual(r.extra, { sat: '7' });
});

test('millisecond and ISO timestamps are accepted', () => {
  const ms = parseReport({ id: 'x', lat: '1', lon: '2', timestamp: '1791541845000' }, now);
  const iso = parseReport({ id: 'x', lat: '1', lon: '2', timestamp: '2026-10-09T10:30:45Z' }, now);
  assert.equal(ms.kind, 'position');
  assert.equal(iso.kind, 'position');
  if (ms.kind === 'position' && iso.kind === 'position') {
    assert.equal(ms.fixTime.getTime(), iso.fixTime.getTime());
  }
});

test('invalid reports are refused with a reason', () => {
  assert.equal(parseReport({ lat: '1', lon: '2' }, now).kind, 'invalid');
  assert.equal(parseReport({ id: 'bad id!', lat: '1', lon: '2' }, now).kind, 'invalid');
  assert.equal(parseReport({ id: 'x', lat: '91', lon: '2' }, now).kind, 'invalid');
  assert.equal(parseReport({ id: 'x', lon: '2' }, now).kind, 'invalid');
  assert.equal(parseReport({ id: 'x', lat: '1', lon: '2', timestamp: 'yesterday' }, now).kind, 'invalid');
});

test('health report keeps every counter and splits the stack list', () => {
  const r = parseReport(
    {
      id: 'trk-1', health: '1', fw: '0.2.0', up: '3725', rst: 'taskwdt', st: 'acquiring',
      fix: '0', sat: '3', sky: '11', sig: '6', cn0: '38', q: '0',
      nrx: '12345', nrej: '7', ncks: '5', nfld: '2', sil: '1',
      sd: '1', lok: '900', ldrop: '0', lerr: '4', lrm: '1',
      uok: '450', udrop: '3', ufail: '9', ucyc: '2', uq: '12', csq: '17',
      derr: '0', bto: '1', berr: '0', bdrop: '0',
      bmv: '3912', heap: '210000', hmin: '180000', stk: '1200/900/2100/2400/1500',
    },
    now,
  );
  assert.equal(r.kind, 'health');
  if (r.kind !== 'health') return;
  assert.equal(r.firmware, '0.2.0');
  assert.equal(r.uptimeS, 3725);
  assert.equal(r.resetReason, 'taskwdt');
  assert.equal(r.state, 'acquiring');
  assert.equal(r.batteryMv, 3912);
  assert.equal(r.metrics.nrx, 12345);
  assert.equal(r.metrics.csq, 17);
  assert.equal(r.metrics.hmin, 180000);
  assert.equal('fw' in r.metrics, false);
  assert.deepEqual(r.stackFree, [1200, 900, 2100, 2400, 1500]);
  assert.equal(r.reportedAt, null);
});

test('health report may carry its own plausible timestamp', () => {
  const recent = Math.floor(now.getTime() / 1000) - 3600;
  const r = parseReport({ id: 'x', health: '1', up: '1', ts: String(recent) }, now);
  assert.equal(r.kind, 'health');
  if (r.kind === 'health') assert.equal(r.reportedAt?.getTime(), recent * 1000);
  const future = parseReport({ id: 'x', health: '1', up: '1', ts: String(recent + 7200) }, now);
  if (future.kind === 'health') assert.equal(future.reportedAt, null);
});
