// The shared access key: one secret the owner hands to whoever may watch.
// Compared in constant time; an address that keeps guessing is locked out
// for a while. Pure (clock injected), unit-tested.

import { createHash, timingSafeEqual } from 'node:crypto';

export type KeyVerdict = 'ok' | 'bad' | 'locked';

export interface GateOptions {
  maxAttempts: number;
  windowMs: number;
}

export function keyFingerprint(key: string): string {
  return createHash('sha256').update(key).digest('hex').slice(0, 16);
}

export class AccessKeyGate {
  private readonly digest: Buffer;
  private readonly attempts = new Map<string, { count: number; resetAt: number }>();

  constructor(
    key: string,
    private readonly options: GateOptions = { maxAttempts: 10, windowMs: 15 * 60_000 },
  ) {
    this.digest = createHash('sha256').update(key).digest();
  }

  check(candidate: string, address: string, now = Date.now()): KeyVerdict {
    const entry = this.attempts.get(address);
    if (entry && entry.resetAt <= now) this.attempts.delete(address);
    const live = this.attempts.get(address);
    if (live && live.count >= this.options.maxAttempts) return 'locked';

    const ok = timingSafeEqual(this.digest, createHash('sha256').update(candidate).digest());
    if (ok) {
      this.attempts.delete(address);
      return 'ok';
    }
    if (live) live.count += 1;
    else this.attempts.set(address, { count: 1, resetAt: now + this.options.windowMs });
    return 'bad';
  }
}
