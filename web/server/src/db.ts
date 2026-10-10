import { readdir, readFile } from 'node:fs/promises';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

import pg from 'pg';

import { config } from './config.js';

export const pool = new pg.Pool({ connectionString: config.databaseUrl, max: 10 });

// Plain SQL files in src/migrations, applied in name order exactly once.
export async function migrate(log: { info: (msg: string) => void }): Promise<void> {
  const dir = join(dirname(fileURLToPath(import.meta.url)), 'migrations');
  const files = (await readdir(dir)).filter((f) => f.endsWith('.sql')).sort();
  const client = await pool.connect();
  try {
    await client.query(
      'CREATE TABLE IF NOT EXISTS schema_migrations (name text PRIMARY KEY, applied_at timestamptz NOT NULL DEFAULT now())',
    );
    const applied = new Set(
      (await client.query<{ name: string }>('SELECT name FROM schema_migrations')).rows.map((r) => r.name),
    );
    for (const file of files) {
      if (applied.has(file)) continue;
      const sql = await readFile(join(dir, file), 'utf8');
      await client.query('BEGIN');
      try {
        await client.query(sql);
        await client.query('INSERT INTO schema_migrations (name) VALUES ($1)', [file]);
        await client.query('COMMIT');
      } catch (err) {
        await client.query('ROLLBACK');
        throw err;
      }
      log.info(`migration applied: ${file}`);
    }
  } finally {
    client.release();
  }
}
