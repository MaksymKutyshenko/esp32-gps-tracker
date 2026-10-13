import { useMemo } from 'react';
import { Bar, BarChart, CartesianGrid, ResponsiveContainer, Tooltip, XAxis, YAxis } from 'recharts';

import type { BootStats, HealthReport } from '../../../shared/types';
import { RESET_LABELS } from '../../../shared/types';
import { ago, duration, fmtTime } from '../format';
import type { Theme } from '../hooks';
import { useTokens } from '../hooks';

const CLEAN_RESETS = new Set(['poweron', 'sw', 'deepsleep']);

function reasonLabel(reason: string | null): string {
  if (!reason) return '–';
  return RESET_LABELS[reason] ?? reason;
}

function hours(seconds: number): number {
  return Math.round((seconds / 3600) * 100) / 100;
}

interface Props {
  stats: BootStats | null;
  lastHealth: HealthReport | null;
  online: boolean;
  now: number;
  theme: Theme;
}

// How long the tracker has been running, and how its runs ended.
export function UptimePanel({ stats, lastHealth, online, now, theme }: Props) {
  const tokens = useTokens(theme);

  // The current run keeps counting between reports while the device is online.
  const currentUptimeS =
    lastHealth?.uptimeS !== null && lastHealth?.uptimeS !== undefined
      ? lastHealth.uptimeS + (online ? Math.max(0, Math.floor((now - Date.parse(lastHealth.receivedAt)) / 1000)) : 0)
      : null;

  const topReason = useMemo(() => {
    if (!stats) return null;
    const entries = Object.entries(stats.byReason).sort((a, b) => b[1] - a[1]);
    return entries[0] ?? null;
  }, [stats]);

  const bars = useMemo(
    () =>
      (stats?.sessions ?? []).map((s) => ({
        name: `#${s.index}`,
        bootAt: s.bootAt,
        hours: hours(s.uptimeS),
        endedBy: s.endedBy,
        clean: s.endedBy === null || CLEAN_RESETS.has(s.endedBy),
      })),
    [stats],
  );

  if (!stats) return <div className="empty">Loading…</div>;
  if (stats.sessions.length === 0) return <div className="empty">No health reports in this range, so no uptime to show.</div>;

  const tiles: { label: string; value: string; sub?: string }[] = [
    {
      label: online ? 'Current run' : 'Last run',
      value: duration(currentUptimeS),
      sub: lastHealth?.resetReason ? `started by ${reasonLabel(lastHealth.resetReason).toLowerCase()}` : undefined,
    },
    { label: 'Total run time in range', value: duration(stats.totalUptimeS) },
    { label: 'Reboots in range', value: `${stats.reboots}`, sub: topReason ? `most often: ${reasonLabel(topReason[0])} (${topReason[1]})` : undefined },
    { label: 'Longest run', value: duration(stats.longestS) },
    { label: 'Mean run before a reset', value: stats.meanBetweenResetsS === null ? '–' : duration(stats.meanBetweenResetsS) },
    { label: 'Unexpected resets', value: `${Object.entries(stats.byReason).filter(([r]) => !CLEAN_RESETS.has(r)).reduce((a, [, n]) => a + n, 0)}`, sub: 'watchdogs, panics, brownouts' },
  ];

  return (
    <div>
      <div className="tiles" style={{ padding: '12px 14px 0' }}>
        {tiles.map((t) => (
          <div key={t.label} className="tile">
            <div className="label">{t.label}</div>
            <div className="value">{t.value}</div>
            {t.sub && <div className="label">{t.sub}</div>}
          </div>
        ))}
      </div>

      <div className="charts" style={{ gridTemplateColumns: '1fr' }}>
        <div className="chart">
          <h3>Run length per boot (hours)</h3>
          <div className="legend">
            <span><i style={{ background: tokens.series[0], height: 8 }} /> ended cleanly or still running</span>
            <span><i style={{ background: tokens.err, height: 8 }} /> ended by a watchdog, panic or brownout</span>
          </div>
          <ResponsiveContainer width="100%" height={Math.max(140, Math.min(320, 40 + bars.length * 14))}>
            <BarChart data={bars} margin={{ top: 6, right: 12, bottom: 0, left: 0 }} barCategoryGap={2}>
              <CartesianGrid stroke={tokens.grid} vertical={false} />
              <XAxis dataKey="name" tick={{ fill: tokens.text2, fontSize: 11 }} axisLine={{ stroke: tokens.grid }} tickLine={false} interval="preserveStartEnd" />
              <YAxis width={44} tick={{ fill: tokens.text2, fontSize: 11 }} axisLine={false} tickLine={false} />
              <Tooltip
                cursor={{ fill: tokens.grid, opacity: 0.5 }}
                content={({ active, payload }) => {
                  const d = payload?.[0]?.payload as (typeof bars)[number] | undefined;
                  if (!active || !d) return null;
                  return (
                    <div className="tip">
                      <div className="t">Boot {d.name} · {fmtTime(d.bootAt)}</div>
                      <div>Ran {duration(Math.round(d.hours * 3600))}</div>
                      <div>{d.endedBy ? `Ended by ${reasonLabel(d.endedBy)}` : 'Still running or no later report'}</div>
                    </div>
                  );
                }}
              />
              <Bar dataKey="hours" maxBarSize={24} radius={[4, 4, 0, 0]} isAnimationActive={false} shape={(props: { x?: number; y?: number; width?: number; height?: number; payload?: { clean: boolean } }) => {
                const { x = 0, y = 0, width = 0, height = 0, payload } = props;
                const fill = payload?.clean ? tokens.series[0] : tokens.err;
                return <rect x={x} y={y} width={width} height={height} rx={Math.min(4, width / 2)} fill={fill} />;
              }} />
            </BarChart>
          </ResponsiveContainer>
        </div>
      </div>

      <div className="table-wrap">
        <table>
          <thead>
            <tr>
              <th>#</th>
              <th>Booted</th>
              <th>Started by</th>
              <th className="num">Ran for</th>
              <th>Ended by</th>
              <th>Last report</th>
              <th className="num">Reports</th>
              <th>Firmware</th>
            </tr>
          </thead>
          <tbody>
            {[...stats.sessions].reverse().map((s) => (
              <tr key={s.index}>
                <td className="muted">{s.index}</td>
                <td className="mono">{fmtTime(s.bootAt)}</td>
                <td className={s.resetReason && !CLEAN_RESETS.has(s.resetReason) ? 'level warn' : ''}>{reasonLabel(s.resetReason)}</td>
                <td className="num">{duration(s.uptimeS)}</td>
                <td className={s.endedBy && !CLEAN_RESETS.has(s.endedBy) ? 'level error' : ''}>
                  {s.endedBy ? reasonLabel(s.endedBy) : online && s.index === stats.sessions.length ? 'still running' : 'no later report'}
                </td>
                <td className="muted">{ago(s.lastReportAt, now)}</td>
                <td className="num">{s.reports}</td>
                <td className="mono">{s.firmware ?? '–'}</td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>
    </div>
  );
}
