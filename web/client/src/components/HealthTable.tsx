import { Fragment, useState } from 'react';

import type { HealthReport } from '../../../shared/types';
import { HEALTH_KEYS, RESET_LABELS, STATE_LABELS, TASK_NAMES } from '../../../shared/types';
import { ago, duration, fmtTime, num } from '../format';

function counterDelta(h: HealthReport, prev: HealthReport | undefined, key: string): string {
  if (!prev) return '–';
  const a = h.metrics[key];
  const b = prev.metrics[key];
  if (a === undefined || b === undefined) return '–';
  if (h.uptimeS !== null && prev.uptimeS !== null && h.uptimeS < prev.uptimeS) return 'reboot';
  const d = a - b;
  return d === 0 ? '0' : `+${d}`;
}

function Raw({ h }: { h: HealthReport }) {
  return (
    <div className="raw">
      <dl>
        <div className="row"><dt>Firmware</dt><dd>{h.firmware ?? '–'}</dd></div>
        <div className="row"><dt>Uptime</dt><dd>{duration(h.uptimeS)}</dd></div>
        <div className="row"><dt>Reset reason</dt><dd>{h.resetReason ? RESET_LABELS[h.resetReason] ?? h.resetReason : '–'}</dd></div>
        <div className="row"><dt>State</dt><dd>{h.state ? STATE_LABELS[h.state] ?? h.state : '–'}</dd></div>
        {HEALTH_KEYS.map((k) => (
          <div className="row" key={k.key}>
            <dt>{k.label} <span className="mono muted">{k.key}</span></dt>
            <dd>{h.metrics[k.key] === undefined ? '–' : h.metrics[k.key].toLocaleString()}</dd>
          </div>
        ))}
        {h.stackFree?.map((free, i) => (
          <div className="row" key={`stk${i}`}>
            <dt>Stack free: {TASK_NAMES[i] ?? i} task</dt>
            <dd>{free.toLocaleString()} B</dd>
          </div>
        ))}
        {Object.keys(h.metrics)
          .filter((k) => !HEALTH_KEYS.some((hk) => hk.key === k))
          .map((k) => (
            <div className="row" key={k}>
              <dt><span className="mono">{k}</span></dt>
              <dd>{h.metrics[k]}</dd>
            </div>
          ))}
      </dl>
    </div>
  );
}

export function HealthTable({ reports, now }: { reports: HealthReport[]; now: number }) {
  const [open, setOpen] = useState<number | null>(null);
  if (reports.length === 0) return <div className="empty">No health reports in this range.</div>;
  const rows = [...reports].reverse();
  return (
    <div className="table-wrap">
      <table>
        <thead>
          <tr>
            <th>Received</th>
            <th>Uptime</th>
            <th>State</th>
            <th>Reset</th>
            <th className="num">Batt V</th>
            <th className="num" title="used / tracked / in view">Sats</th>
            <th className="num">C/N0</th>
            <th className="num">CSQ</th>
            <th className="num" title="since the previous report">Rejected Δ</th>
            <th className="num" title="since the previous report">Card err Δ</th>
            <th className="num" title="since the previous report">Upload fail Δ</th>
            <th className="num">Backlog</th>
            <th className="num" title="since the previous report">Bus/disp err Δ</th>
            <th className="num">Min heap</th>
            <th className="num">Min stack</th>
          </tr>
        </thead>
        <tbody>
          {rows.map((h, i) => {
            const prev = rows[i + 1];
            const m = h.metrics;
            const minStack = h.stackFree && h.stackFree.length ? Math.min(...h.stackFree) : null;
            const bus = prev ? `${counterDelta(h, prev, 'bto')} / ${counterDelta(h, prev, 'derr')}` : '–';
            return (
              <Fragment key={h.id}>
                <tr className="clickable" onClick={() => setOpen(open === h.id ? null : h.id)} title="Click for every counter">
                  <td className="mono">
                    {fmtTime(h.receivedAt)} <span className="muted">({ago(h.receivedAt, now)})</span>
                  </td>
                  <td>{duration(h.uptimeS)}</td>
                  <td>{h.state ? STATE_LABELS[h.state] ?? h.state : '–'}</td>
                  <td className={h.resetReason && !['poweron', 'sw', 'deepsleep'].includes(h.resetReason) ? 'level warn' : ''}>
                    {h.resetReason ? RESET_LABELS[h.resetReason] ?? h.resetReason : '–'}
                  </td>
                  <td className="num">{h.batteryMv === null ? '–' : (h.batteryMv / 1000).toFixed(2)}</td>
                  <td className="num">{m.sat ?? '–'} / {m.sig ?? '–'} / {m.sky ?? '–'}</td>
                  <td className="num">{num(m.cn0, 0)}</td>
                  <td className="num">{m.csq === undefined || m.csq === 99 ? '–' : m.csq}</td>
                  <td className="num">{counterDelta(h, prev, 'nrej')}</td>
                  <td className="num">{counterDelta(h, prev, 'lerr')}</td>
                  <td className="num">{counterDelta(h, prev, 'ufail')}</td>
                  <td className="num">{num(m.uq, 0)}</td>
                  <td className="num">{bus}</td>
                  <td className="num">{m.hmin === undefined ? '–' : `${Math.round(m.hmin / 1024)} K`}</td>
                  <td className="num">{minStack === null ? '–' : minStack}</td>
                </tr>
                {open === h.id && (
                  <tr>
                    <td colSpan={15} style={{ padding: 0 }}>
                      <Raw h={h} />
                    </td>
                  </tr>
                )}
              </Fragment>
            );
          })}
        </tbody>
      </table>
    </div>
  );
}
