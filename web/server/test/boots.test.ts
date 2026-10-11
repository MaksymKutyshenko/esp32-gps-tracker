import assert from 'node:assert/strict';
import { test } from 'node:test';

import { bootSessions } from '../src/boots.js';

const t0 = Date.parse('2026-10-11T10:00:00Z');
const row = (minutes: number, uptimeS: number | null, resetReason = 'poweron', firmware = '0.2.0') => ({
  receivedAt: new Date(t0 + minutes * 60_000),
  uptimeS,
  resetReason,
  firmware,
});

test('one run, no reboot', () => {
  const s = bootSessions([row(0, 60), row(1, 120), row(2, 180)]);
  assert.equal(s.sessions.length, 1);
  assert.equal(s.reboots, 0);
  assert.equal(s.sessions[0].uptimeS, 180);
  assert.equal(s.sessions[0].bootAt, new Date(t0 - 60_000).toISOString());
  assert.equal(s.sessions[0].endedBy, null);
  assert.equal(s.meanBetweenResetsS, null);
  assert.deepEqual(s.byReason, {});
});

test('uptime going backwards starts a new session and names what ended the previous one', () => {
  const s = bootSessions([
    row(0, 60), row(1, 120), row(2, 180),
    row(5, 30, 'taskwdt'), row(6, 90, 'taskwdt'),
    row(10, 20, 'brownout'),
  ]);
  assert.equal(s.sessions.length, 3);
  assert.equal(s.reboots, 2);
  assert.equal(s.sessions[0].endedBy, 'taskwdt');
  assert.equal(s.sessions[1].resetReason, 'taskwdt');
  assert.equal(s.sessions[1].uptimeS, 90);
  assert.equal(s.sessions[1].endedBy, 'brownout');
  assert.equal(s.sessions[2].endedBy, null);
  assert.equal(s.totalUptimeS, 180 + 90 + 20);
  assert.equal(s.longestS, 180);
  assert.deepEqual(s.byReason, { taskwdt: 1, brownout: 1 });
  assert.equal(s.meanBetweenResetsS, Math.round((180 + 90) / 2));
});

test('boot instant uses the earliest estimate; late reports do not move it later', () => {
  // Second report arrived 30 s late relative to its uptime.
  const s = bootSessions([row(0, 60), { ...row(1, 90), receivedAt: new Date(t0 + 60_000 + 30_000) }]);
  assert.equal(s.sessions[0].bootAt, new Date(t0 - 60_000).toISOString());
});

test('rows without uptime are ignored; equal uptimes (duplicate report) do not split', () => {
  const s = bootSessions([row(0, 60), row(0.5, null), row(1, 60), row(2, 120)]);
  assert.equal(s.sessions.length, 1);
  assert.equal(s.sessions[0].reports, 3);
});
