// The shared viewer key lives in the settings table so the owner can
// rotate it from the dashboard; the admin key is a server secret so a
// viewer can never lock the owner out. Both are hashed; a short-lived
// cache keeps sign-in and session checks off the database.

import { generateKey, hashKey, keyFingerprint, verifyKey } from './accessKey.js';
import { config } from './config.js';
import { pool } from './db.js';

const SETTING = 'access_key_hash';
const CACHE_MS = 15_000;

interface Cached {
  stored: string | null;
  fingerprint: string | null;
  updatedAt: Date | null;
  updatedBy: string | null;
  readAt: number;
}

let cache: Cached | null = null;
let adminStored: string | null = null;

export async function initKeys(log: { info: (msg: string) => void; warn: (msg: string) => void }): Promise<void> {
  if (config.adminKey) adminStored = await hashKey(config.adminKey);
  const current = await readViewer(true);
  if (!current.stored && config.accessKey) {
    // First start: the ACCESS_KEY secret seeds the database once; after
    // that the dashboard owns the key and the secret can be removed.
    await setViewerKey(config.accessKey, 'ACCESS_KEY env');
    log.info('viewer access key seeded from ACCESS_KEY');
  } else if (!current.stored && !config.adminKey && !config.googleClientId) {
    log.warn('No viewer key in the database, no ADMIN_KEY and no Google: nobody can sign in. Set ACCESS_KEY or ADMIN_KEY.');
  }
}

async function readViewer(force = false): Promise<Cached> {
  if (!force && cache && Date.now() - cache.readAt < CACHE_MS) return cache;
  const { rows } = await pool.query<{ value: string; updated_at: Date; updated_by: string | null }>(
    'SELECT value, updated_at, updated_by FROM settings WHERE key = $1',
    [SETTING],
  );
  const row = rows[0];
  cache = {
    stored: row?.value ?? null,
    fingerprint: row ? keyFingerprint(row.value) : null,
    updatedAt: row?.updated_at ?? null,
    updatedBy: row?.updated_by ?? null,
    readAt: Date.now(),
  };
  return cache;
}

export interface ViewerKeyInfo {
  configured: boolean;
  updatedAt: string | null;
  updatedBy: string | null;
}

export async function viewerKeyInfo(): Promise<ViewerKeyInfo> {
  const c = await readViewer(true);
  return { configured: c.stored !== null, updatedAt: c.updatedAt?.toISOString() ?? null, updatedBy: c.updatedBy };
}

export async function viewerFingerprint(): Promise<string | null> {
  return (await readViewer()).fingerprint;
}

export function adminFingerprint(): string | null {
  return adminStored ? keyFingerprint(adminStored) : null;
}

export type KeyMatch = 'admin' | 'viewer' | null;

export async function matchKey(candidate: string): Promise<KeyMatch> {
  if (adminStored && (await verifyKey(candidate, adminStored))) return 'admin';
  const viewer = await readViewer();
  if (viewer.stored && (await verifyKey(candidate, viewer.stored))) return 'viewer';
  return null;
}

// Stores a new viewer key (given, or freshly generated) and returns the
// clear text once, for the owner to hand out.
export async function setViewerKey(key: string | null, by: string): Promise<string> {
  const clear = key ?? generateKey();
  const stored = await hashKey(clear);
  await pool.query(
    `INSERT INTO settings (key, value, updated_at, updated_by) VALUES ($1, $2, now(), $3)
     ON CONFLICT (key) DO UPDATE SET value = EXCLUDED.value, updated_at = now(), updated_by = EXCLUDED.updated_by`,
    [SETTING, stored, by],
  );
  await readViewer(true);
  return clear;
}
