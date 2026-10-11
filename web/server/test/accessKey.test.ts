import assert from 'node:assert/strict';
import { test } from 'node:test';

import { AttemptLimiter, generateKey, hashKey, keyFingerprint, verifyKey } from '../src/accessKey.js';

test('hash verifies the right key only and never stores it in clear', async () => {
  const stored = await hashKey('correct-horse-battery');
  assert.match(stored, /^scrypt\$[0-9a-f]{32}\$[0-9a-f]{64}$/);
  assert.equal(stored.includes('correct'), false);
  assert.equal(await verifyKey('correct-horse-battery', stored), true);
  assert.equal(await verifyKey('correct-horse-batter', stored), false);
  assert.equal(await verifyKey('', stored), false);
  assert.equal(await verifyKey('x', 'garbage'), false);
});

test('two hashes of one key differ (salted) but both verify; fingerprints differ', async () => {
  const a = await hashKey('same');
  const b = await hashKey('same');
  assert.notEqual(a, b);
  assert.equal(await verifyKey('same', a), true);
  assert.equal(await verifyKey('same', b), true);
  assert.notEqual(keyFingerprint(a), keyFingerprint(b));
  assert.equal(keyFingerprint(a).length, 16);
});

test('generated keys are long, URL-safe and distinct', () => {
  const k = generateKey();
  assert.equal(k.length, 24);
  assert.match(k, /^[A-Za-z0-9_-]+$/);
  assert.notEqual(k, generateKey());
});

test('limiter locks an address after the limit and releases after the window', () => {
  const limiter = new AttemptLimiter({ maxAttempts: 3, windowMs: 1000 });
  assert.equal(limiter.check('a', 0), 'allowed');
  limiter.failure('a', 1);
  limiter.failure('a', 2);
  assert.equal(limiter.check('a', 3), 'allowed');
  limiter.failure('a', 3);
  assert.equal(limiter.check('a', 4), 'locked');
  assert.equal(limiter.check('b', 4), 'allowed', 'other addresses are not affected');
  assert.equal(limiter.check('a', 1002), 'allowed', 'window elapsed');
  limiter.failure('a', 1002);
  limiter.success('a');
  limiter.failure('a', 1003);
  assert.equal(limiter.check('a', 1004), 'allowed', 'a success cleared the count');
});
