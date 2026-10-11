// Boot sessions from the health stream. Each report says how long the
// device has been up (`up`) and why it last started (`rst`); uptime going
// backwards between two reports means a reboot. Pure, unit-tested.

import type { BootSession, BootStats } from '../../shared/types.js';

export interface UptimeRow {
  receivedAt: Date;
  uptimeS: number | null;
  resetReason: string | null;
  firmware: string | null;
}

// `rows` oldest first.
export function bootSessions(rows: UptimeRow[]): BootStats {
  type Building = BootSession & { bootMs: number };
  const sessions: Building[] = [];
  let current: Building | null = null;
  let previousUptime = -1;

  for (const row of rows) {
    if (row.uptimeS === null) continue;
    const receivedMs = row.receivedAt.getTime();
    // Reports arrive late (modem, retries), never early: the earliest
    // estimate of the boot instant is the most accurate one.
    const bootEstimate = receivedMs - row.uptimeS * 1000;
    const rebooted = current === null || row.uptimeS < previousUptime;
    if (rebooted) {
      current = {
        index: sessions.length + 1,
        bootAt: new Date(bootEstimate).toISOString(),
        bootMs: bootEstimate,
        firstReportAt: row.receivedAt.toISOString(),
        lastReportAt: row.receivedAt.toISOString(),
        uptimeS: row.uptimeS,
        resetReason: row.resetReason,
        endedBy: null,
        reports: 0,
        firmware: row.firmware,
      };
      sessions.push(current);
      if (sessions.length > 1) sessions[sessions.length - 2].endedBy = row.resetReason ?? 'unknown';
    }
    const s = current!;
    if (bootEstimate < s.bootMs) {
      s.bootMs = bootEstimate;
      s.bootAt = new Date(bootEstimate).toISOString();
    }
    s.lastReportAt = row.receivedAt.toISOString();
    s.uptimeS = Math.max(s.uptimeS, row.uptimeS);
    s.reports += 1;
    if (row.firmware) s.firmware = row.firmware;
    previousUptime = row.uptimeS;
  }

  const clean = sessions.map(({ bootMs: _bootMs, ...s }) => s);
  const totalUptimeS = clean.reduce((acc, s) => acc + s.uptimeS, 0);
  const longestS = clean.reduce((acc, s) => Math.max(acc, s.uptimeS), 0);
  const byReason: Record<string, number> = {};
  for (const s of clean.slice(1)) {
    const reason = s.resetReason ?? 'unknown';
    byReason[reason] = (byReason[reason] ?? 0) + 1;
  }
  const reboots = clean.length > 1 ? clean.length - 1 : 0;
  // Mean run length of the sessions that actually ended in a reset.
  const ended = clean.filter((s) => s.endedBy !== null);
  const meanBetweenResetsS = ended.length > 0 ? Math.round(ended.reduce((a, s) => a + s.uptimeS, 0) / ended.length) : null;

  return { sessions: clean, totalUptimeS, longestS, reboots, byReason, meanBetweenResetsS };
}
