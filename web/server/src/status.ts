// Turns the last two health reports and the last position into one verdict
// per firmware module. Pure: no I/O, unit-tested.
//
// Counters in a report are totals since boot, so "is something wrong now"
// is the delta between the latest report and the one before it (when both
// come from the same boot). Lifetime totals are still shown as details.

import type { HealthReport, ModuleDetail, ModuleLevel, ModuleStatus, Position } from '../../shared/types.js';
import { RESET_LABELS, STATE_LABELS, TASK_NAMES } from '../../shared/types.js';

export interface StatusInput {
  latest: HealthReport | null;
  previous: HealthReport | null;
  lastPosition: Position | null;
  lastSeen: Date | null;
  now: Date;
  onlineTimeoutS: number;
}

export function isOnline(lastSeen: Date | null, now: Date, onlineTimeoutS: number): boolean {
  return lastSeen !== null && now.getTime() - lastSeen.getTime() < onlineTimeoutS * 1000;
}

const worse: Record<ModuleLevel, number> = { unknown: 0, ok: 1, warn: 2, error: 3 };

class Verdict {
  level: ModuleLevel = 'ok';
  issues: string[] = [];
  details: ModuleDetail[] = [];

  raise(level: ModuleLevel, issue: string): void {
    // "unknown" only replaces a default "ok"; a real finding always wins.
    if (level === 'unknown') {
      if (this.level === 'ok') this.level = 'unknown';
    } else if (worse[level] > worse[this.level]) {
      this.level = level;
    }
    this.issues.push(issue);
  }
  detail(label: string, value: string | number | null | undefined, unit = ''): void {
    if (value === null || value === undefined) return;
    this.details.push({ label, value: `${value}${unit}` });
  }
  build(key: string, name: string, summary: string): ModuleStatus {
    return { key, name, level: this.level, summary, issues: this.issues, details: this.details };
  }
}

// Change of a since-boot counter between the two reports, or null when
// there is no comparable earlier report (first report, or a reboot between).
function delta(latest: HealthReport, previous: HealthReport | null, key: string): number | null {
  if (!previous) return null;
  const a = latest.metrics[key];
  const b = previous.metrics[key];
  if (a === undefined || b === undefined) return null;
  const rebooted =
    latest.uptimeS !== null && previous.uptimeS !== null && latest.uptimeS < previous.uptimeS;
  if (rebooted) return null;
  return a - b;
}

function fmtAge(seconds: number): string {
  if (seconds < 90) return `${Math.round(seconds)} s`;
  if (seconds < 5400) return `${Math.round(seconds / 60)} min`;
  if (seconds < 172800) return `${(seconds / 3600).toFixed(1)} h`;
  return `${(seconds / 86400).toFixed(1)} d`;
}

export function deriveModules(input: StatusInput): ModuleStatus[] {
  const { latest, previous, lastPosition, lastSeen, now, onlineTimeoutS } = input;
  const online = isOnline(lastSeen, now, onlineTimeoutS);
  const modules: ModuleStatus[] = [];

  // ---- Device / link ---------------------------------------------------
  {
    const v = new Verdict();
    if (!lastSeen) {
      v.raise('unknown', 'No report received yet');
    } else {
      const age = (now.getTime() - lastSeen.getTime()) / 1000;
      v.detail('Last report', `${fmtAge(age)} ago`);
      if (!online) v.raise('error', `Silent for ${fmtAge(age)} (limit ${onlineTimeoutS} s)`);
    }
    if (latest) {
      v.detail('Firmware', latest.firmware);
      v.detail('Uptime', latest.uptimeS !== null ? fmtAge(latest.uptimeS) : null);
      v.detail('Reset reason', latest.resetReason ? RESET_LABELS[latest.resetReason] ?? latest.resetReason : null);
      const reportAge = (now.getTime() - Date.parse(latest.receivedAt)) / 1000;
      if (reportAge > onlineTimeoutS * 2) v.raise('warn', `Health report is ${fmtAge(reportAge)} old`);
      if (latest.resetReason && !['poweron', 'sw', 'deepsleep'].includes(latest.resetReason)) {
        v.raise('warn', `Last reset: ${RESET_LABELS[latest.resetReason] ?? latest.resetReason}`);
      }
      if (previous && latest.uptimeS !== null && previous.uptimeS !== null && latest.uptimeS < previous.uptimeS) {
        v.raise('warn', 'Rebooted between the last two reports');
      }
    } else {
      v.raise('unknown', 'No health report yet (position reports only)');
    }
    modules.push(v.build('device', 'Device', online ? 'Online' : lastSeen ? 'Offline' : 'Never seen'));
  }

  // ---- GNSS ----------------------------------------------------------------
  {
    const v = new Verdict();
    let summary = 'No data';
    if (latest) {
      const m = latest.metrics;
      const state = latest.state ?? 'unknown';
      summary = STATE_LABELS[state] ?? state;
      v.detail('Used / tracked / in view', `${m.sat ?? '?'} / ${m.sig ?? '?'} / ${m.sky ?? '?'}`);
      v.detail('Strongest C/N0', m.cn0, ' dB-Hz');
      v.detail('Fix quality (GGA)', m.q);
      v.detail('Sentences', m.nrx);
      v.detail('Rejected (checksum / fields)', `${m.nrej ?? 0} (${m.ncks ?? 0} / ${m.nfld ?? 0})`);
      v.detail('Silences', m.sil);

      if (state === 'nosignal') v.raise('error', 'Receiver silent: check the UART wiring and baud rate');
      else if (state === 'boot') v.raise('warn', 'No NMEA sentence seen since reset');
      else if (state === 'acquiring') {
        if ((m.sig ?? 0) === 0) v.raise('warn', 'No satellite signal: antenna has no sky view');
        else v.raise('warn', `Searching: ${m.sig} satellites tracked, C/N0 ${m.cn0 ?? 0} dB-Hz`);
      }
      const rx = delta(latest, previous, 'nrx');
      const rej = delta(latest, previous, 'nrej');
      if (rx !== null && rej !== null && rx + rej > 0 && rej > (rx + rej) * 0.1) {
        v.raise('warn', `${rej} of ${rx + rej} sentences rejected since the last report: wrong baud rate?`);
      }
      if (rx === 0 && state !== 'nosignal') v.raise('warn', 'No sentences received since the last report');
      const sil = delta(latest, previous, 'sil');
      if (sil !== null && sil > 0) v.raise('warn', `Receiver went silent ${sil}× since the last report`);
      if (m.fix === 1 && (m.sat ?? 0) < 5) v.raise('warn', `Fix with only ${m.sat} satellites`);
    } else if (lastPosition) {
      summary = 'Reporting positions';
      v.detail('Satellites / HDOP', `${lastPosition.extra.sat ?? '?'} / ${lastPosition.hdop ?? '?'}`);
    } else {
      v.raise('unknown', 'No report yet');
    }
    if (lastPosition) {
      const age = (now.getTime() - Date.parse(lastPosition.fixTime)) / 1000;
      v.detail('Last fix', `${fmtAge(age)} ago`);
      v.detail('HDOP', lastPosition.hdop);
    }
    modules.push(v.build('gnss', 'GNSS receiver', summary));
  }

  // ---- Storage -------------------------------------------------------------
  {
    const v = new Verdict();
    let summary = 'No data';
    if (latest) {
      const m = latest.metrics;
      const ready = m.sd === 1;
      summary = ready ? 'Card mounted' : 'Card unavailable';
      v.detail('Records logged', m.lok);
      v.detail('Dropped (queue full)', m.ldrop);
      v.detail('Write errors', m.lerr);
      v.detail('Remounts', m.lrm);
      if (!ready) v.raise('error', 'MicroSD not mounted: records are lost');
      const err = delta(latest, previous, 'lerr');
      if (err !== null && err > 0) v.raise('warn', `${err} write errors since the last report`);
      const rm = delta(latest, previous, 'lrm');
      if (rm !== null && rm > 0) v.raise('warn', `Card remounted ${rm}× since the last report`);
      const drop = delta(latest, previous, 'ldrop');
      if (drop !== null && drop > 0) v.raise('warn', `${drop} records dropped: the card is too slow`);
    } else {
      v.raise('unknown', 'No health report yet');
    }
    modules.push(v.build('storage', 'MicroSD log', summary));
  }

  // ---- Uplink / modem ------------------------------------------------------
  {
    const v = new Verdict();
    let summary = 'No data';
    if (latest) {
      const m = latest.metrics;
      const csq = m.csq;
      summary = csq === undefined || csq === 99 ? 'Connected' : `Connected, CSQ ${csq}`;
      v.detail('GSM signal (CSQ)', csq === 99 ? 'unknown' : csq);
      v.detail('Uploaded', m.uok);
      v.detail('Backlog', m.uq);
      v.detail('Failures', m.ufail);
      v.detail('Dropped (queue full)', m.udrop);
      v.detail('Modem power cycles', m.ucyc);
      if (csq !== undefined && csq !== 99 && csq < 10) v.raise('warn', `Weak GSM signal (CSQ ${csq})`);
      const fail = delta(latest, previous, 'ufail');
      if (fail !== null && fail > 0) v.raise('warn', `${fail} upload failures since the last report`);
      const cyc = delta(latest, previous, 'ucyc');
      if (cyc !== null && cyc > 0) v.raise('error', `Modem power-cycled ${cyc}× since the last report`);
      const drop = delta(latest, previous, 'udrop');
      if (drop !== null && drop > 0) v.raise('warn', `${drop} records dropped: uplink cannot keep up`);
      if ((m.uq ?? 0) >= 32) v.raise('warn', `${m.uq} records waiting to upload`);
    } else if (lastPosition) {
      summary = 'Reporting positions';
    } else {
      v.raise('unknown', 'No report yet');
    }
    modules.push(v.build('uplink', 'GPRS uplink', summary));
  }

  // ---- Display and button --------------------------------------------------
  {
    const v = new Verdict();
    let summary = 'No data';
    if (latest) {
      const m = latest.metrics;
      summary = (m.derr ?? 0) === 0 && (m.bto ?? 0) === 0 ? 'OK' : 'Errors seen';
      v.detail('Display errors', m.derr);
      v.detail('I2C/SPI bus timeouts', m.bto);
      v.detail('Button edges dropped', m.bdrop);
      const derr = delta(latest, previous, 'derr');
      if (derr !== null && derr > 0) v.raise('warn', `${derr} display errors since the last report`);
      const bto = delta(latest, previous, 'bto');
      if (bto !== null && bto > 0) v.raise('warn', `${bto} bus timeouts since the last report`);
      const bdrop = delta(latest, previous, 'bdrop');
      if (bdrop !== null && bdrop > 0) v.raise('warn', `${bdrop} button edges dropped`);
    } else {
      v.raise('unknown', 'No health report yet');
    }
    modules.push(v.build('ui', 'Display and button', summary));
  }

  // ---- Battery -------------------------------------------------------------
  {
    const v = new Verdict();
    let summary = 'No data';
    const mv = latest?.batteryMv ?? null;
    if (mv !== null) {
      summary = `${(mv / 1000).toFixed(2)} V`;
      v.detail('Cell voltage', mv, ' mV');
      v.detail('Read errors', latest?.metrics.berr);
      if (mv < 3400) v.raise('error', 'Battery nearly empty');
      else if (mv < 3600) v.raise('warn', 'Battery low');
      else if (mv > 4300) v.raise('warn', 'Voltage above a full cell: divider or ADC calibration?');
      const berr = latest ? delta(latest, previous, 'berr') : null;
      if (berr !== null && berr > 0) v.raise('warn', `${berr} ADC read errors since the last report`);
    } else if (lastPosition?.batteryPct !== null && lastPosition?.batteryPct !== undefined) {
      summary = `${lastPosition.batteryPct} %`;
      if (lastPosition.batteryPct < 15) v.raise('warn', 'Battery low');
    } else {
      v.raise('unknown', 'No battery reading yet');
    }
    if (lastPosition?.batteryPct !== null && lastPosition?.batteryPct !== undefined) {
      v.detail('Charge (from last position)', lastPosition.batteryPct, ' %');
    }
    modules.push(v.build('battery', 'Battery', summary));
  }

  // ---- Memory and tasks ----------------------------------------------------
  {
    const v = new Verdict();
    let summary = 'No data';
    if (latest) {
      const m = latest.metrics;
      summary = m.hmin !== undefined ? `${Math.round(m.hmin / 1024)} KiB min free heap` : 'OK';
      v.detail('Free heap', m.heap, ' B');
      v.detail('Min free heap', m.hmin, ' B');
      if (m.hmin !== undefined && m.hmin < 20000) v.raise('warn', `Heap low-water mark ${m.hmin} bytes`);
      if (latest.stackFree) {
        latest.stackFree.forEach((free, i) => {
          const task = TASK_NAMES[i] ?? `task${i}`;
          v.detail(`Stack free: ${task}`, free, ' B');
          if (free < 512) v.raise('warn', `${task} task has only ${free} bytes of stack left`);
        });
      }
    } else {
      v.raise('unknown', 'No health report yet');
    }
    modules.push(v.build('system', 'Memory and tasks', summary));
  }

  return modules;
}
