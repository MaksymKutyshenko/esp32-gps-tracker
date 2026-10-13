// Access keys: hashing, constant-time verification and a per-address
// brute-force limiter. Pure (clock injected), unit-tested.

import { createHash, randomBytes, scrypt as scryptCb, timingSafeEqual } from 'node:crypto';
import { promisify } from 'node:util';

const scrypt = promisify(scryptCb) as (password: string, salt: Buffer, keylen: number) => Promise<Buffer>;
const HASH_BYTES = 32;

// "scrypt$<salt hex>$<hash hex>", safe to keep in the database: a leaked
// table does not give away the key.
export async function hashKey(key: string): Promise<string> {
  const salt = randomBytes(16);
  const hash = await scrypt(key, salt, HASH_BYTES);
  return `scrypt$${salt.toString('hex')}$${hash.toString('hex')}`;
}

export async function verifyKey(candidate: string, stored: string): Promise<boolean> {
  const [scheme, saltHex, hashHex] = stored.split('$');
  if (scheme !== 'scrypt' || !saltHex || !hashHex) return false;
  const expected = Buffer.from(hashHex, 'hex');
  const actual = await scrypt(candidate, Buffer.from(saltHex, 'hex'), expected.length);
  return actual.length === expected.length && timingSafeEqual(actual, expected);
}

// Identifies a key generation without revealing it: sessions carry it, so
// rotating the key invalidates every session issued for the old one.
export function keyFingerprint(stored: string): string {
  return createHash('sha256').update(stored).digest('hex').slice(0, 16);
}

// 24 characters from a URL-safe alphabet, ~143 bits.
export function generateKey(): string {
  return randomBytes(18).toString('base64').replace(/\+/g, '-').replace(/\//g, '_');
}

export type LimitVerdict = 'allowed' | 'locked';

export interface LimiterOptions {
  maxAttempts: number;
  windowMs: number;
}

export class AttemptLimiter {
  private readonly attempts = new Map<string, { count: number; resetAt: number }>();

  constructor(private readonly options: LimiterOptions = { maxAttempts: 10, windowMs: 15 * 60_000 }) {}

  check(address: string, now = Date.now()): LimitVerdict {
    const entry = this.attempts.get(address);
    if (entry && entry.resetAt <= now) this.attempts.delete(address);
    const live = this.attempts.get(address);
    return live && live.count >= this.options.maxAttempts ? 'locked' : 'allowed';
  }

  failure(address: string, now = Date.now()): void {
    const live = this.attempts.get(address);
    if (live && live.resetAt > now) live.count += 1;
    else this.attempts.set(address, { count: 1, resetAt: now + this.options.windowMs });
  }

  success(address: string): void {
    this.attempts.delete(address);
  }
}
