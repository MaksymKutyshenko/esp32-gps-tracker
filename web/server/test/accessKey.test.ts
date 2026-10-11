import assert from 'node:assert/strict';
import { test } from 'node:test';

import { AccessKeyGate, keyFingerprint } from '../src/accessKey.js';

test('right key passes, wrong key fails, lockout after the limit and release after the window', () => {
  const gate = new AccessKeyGate('correct-horse-battery', { maxAttempts: 3, windowMs: 1000 });
  assert.equal(gate.check('correct-horse-battery', 'a', 0), 'ok');
  assert.equal(gate.check('nope', 'a', 1), 'bad');
  assert.equal(gate.check('nope', 'a', 2), 'bad');
  assert.equal(gate.check('nope', 'a', 3), 'bad');
  assert.equal(gate.check('correct-horse-battery', 'a', 4), 'locked', 'even the right key is refused while locked');
  assert.equal(gate.check('nope', 'b', 4), 'bad', 'other addresses are not affected');
  assert.equal(gate.check('correct-horse-battery', 'a', 1002), 'ok', 'window elapsed');
});

test('a success clears the counter; a key of another length is just wrong', () => {
  const gate = new AccessKeyGate('abc', { maxAttempts: 2, windowMs: 1000 });
  assert.equal(gate.check('x', 'a', 0), 'bad');
  assert.equal(gate.check('abc', 'a', 1), 'ok');
  assert.equal(gate.check('x', 'a', 2), 'bad');
  assert.equal(gate.check('abcd', 'a', 3), 'bad');
  assert.equal(gate.check('abc', 'a', 4), 'locked');
});

test('fingerprint is stable and short', () => {
  assert.equal(keyFingerprint('abc'), keyFingerprint('abc'));
  assert.notEqual(keyFingerprint('abc'), keyFingerprint('abd'));
  assert.equal(keyFingerprint('abc').length, 16);
});
