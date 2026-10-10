import { useMemo } from 'react';
import { CartesianGrid, Legend, Line, LineChart, ResponsiveContainer, Tooltip, XAxis, YAxis } from 'recharts';

import type { HealthReport, Position } from '../../../shared/types';
import { compact, fmtClock, fmtTime, knotsToKmh } from '../format';
import type { Theme } from '../hooks';
import { useTokens } from '../hooks';

interface Series {
  key: string;
  name: string;
}

interface ChartSpec {
  title: string;
  series: Series[];
  unit?: string;
  rows: Record<string, number | null>[];
  digits?: number;
}

function TooltipBox({ active, payload, label, unit, digits }: { active?: boolean; payload?: { name: string; value: number | null; color: string }[]; label?: number; unit?: string; digits?: number }) {
  if (!active || !payload?.length || label === undefined) return null;
  return (
    <div className="tip">
      <div className="t">{fmtTime(label)}</div>
      {payload.map((p) => (
        <div key={p.name}>
          <i style={{ display: 'inline-block', width: 10, height: 2, background: p.color, verticalAlign: 'middle', marginRight: 6 }} />
          {p.name}: <strong>{p.value === null || p.value === undefined ? '–' : `${p.value.toLocaleString(undefined, { maximumFractionDigits: digits ?? 1 })}${unit ?? ''}`}</strong>
        </div>
      ))}
    </div>
  );
}

function Chart({ spec, theme }: { spec: ChartSpec; theme: Theme }) {
  const tokens = useTokens(theme);
  const hasData = spec.rows.some((r) => spec.series.some((s) => r[s.key] !== null && r[s.key] !== undefined));
  return (
    <div className="chart">
      <h3>{spec.title}{spec.unit ? ` (${spec.unit.trim()})` : ''}</h3>
      {spec.series.length > 1 && (
        <div className="legend">
          {spec.series.map((s, i) => (
            <span key={s.key}>
              <i style={{ background: tokens.series[i] }} />
              {s.name}
            </span>
          ))}
        </div>
      )}
      {hasData ? (
        <ResponsiveContainer width="100%" height={170}>
          <LineChart data={spec.rows} margin={{ top: 6, right: 12, bottom: 0, left: 0 }}>
            <CartesianGrid stroke={tokens.grid} vertical={false} />
            <XAxis
              dataKey="t"
              type="number"
              domain={['dataMin', 'dataMax']}
              scale="time"
              tickFormatter={(v: number) => fmtClock(v)}
              tick={{ fill: tokens.text2, fontSize: 11 }}
              axisLine={{ stroke: tokens.grid }}
              tickLine={false}
              minTickGap={40}
            />
            <YAxis
              width={44}
              tick={{ fill: tokens.text2, fontSize: 11 }}
              axisLine={false}
              tickLine={false}
              tickFormatter={(v: number) => compact(v)}
              domain={['auto', 'auto']}
              allowDecimals={spec.digits !== 0}
            />
            <Tooltip content={<TooltipBox unit={spec.unit} digits={spec.digits} />} cursor={{ stroke: tokens.text2, strokeWidth: 1 }} />
            {spec.series.length > 1 && <Legend wrapperStyle={{ display: 'none' }} />}
            {spec.series.map((s, i) => (
              <Line
                key={s.key}
                type="monotone"
                dataKey={s.key}
                name={s.name}
                stroke={tokens.series[i]}
                strokeWidth={2}
                dot={false}
                activeDot={{ r: 4, stroke: tokens.surface, strokeWidth: 2 }}
                isAnimationActive={false}
                connectNulls
              />
            ))}
          </LineChart>
        </ResponsiveContainer>
      ) : (
        <div className="empty small">No data in this range</div>
      )}
    </div>
  );
}

// Change of a since-boot counter between consecutive reports, null across
// a reboot (uptime went backwards) and for the first report.
function deltas(reports: HealthReport[], key: string): (number | null)[] {
  return reports.map((h, i) => {
    if (i === 0) return null;
    const prev = reports[i - 1];
    const a = h.metrics[key];
    const b = prev.metrics[key];
    if (a === undefined || b === undefined) return null;
    if (h.uptimeS !== null && prev.uptimeS !== null && h.uptimeS < prev.uptimeS) return null;
    return a - b;
  });
}

export function Charts({ positions, health, theme }: { positions: Position[]; health: HealthReport[]; theme: Theme }) {
  const specs = useMemo<ChartSpec[]>(() => {
    const t = (h: HealthReport) => Date.parse(h.receivedAt);
    const m = (h: HealthReport, key: string) => (h.metrics[key] === undefined ? null : h.metrics[key]);
    const fails = deltas(health, 'ufail');
    const rejected = deltas(health, 'nrej');
    const errors = health.map((h, i) => {
      const e = [deltas(health, 'lerr')[i], deltas(health, 'derr')[i], deltas(health, 'bto')[i]];
      return e.every((x) => x === null) ? null : e.reduce<number>((acc, x) => acc + (x ?? 0), 0);
    });
    return [
      {
        title: 'Speed',
        unit: ' km/h',
        series: [{ key: 'v', name: 'Speed' }],
        rows: positions.map((p) => ({ t: Date.parse(p.fixTime), v: knotsToKmh(p.speedKn) })),
      },
      {
        title: 'Battery',
        unit: ' V',
        digits: 2,
        series: [{ key: 'v', name: 'Cell voltage' }],
        rows: health.map((h) => ({ t: t(h), v: h.batteryMv === null ? null : h.batteryMv / 1000 })),
      },
      {
        title: 'Satellites',
        series: [
          { key: 'sat', name: 'Used' },
          { key: 'sig', name: 'Tracked' },
          { key: 'sky', name: 'In view' },
        ],
        digits: 0,
        rows: health.map((h) => ({ t: t(h), sat: m(h, 'sat'), sig: m(h, 'sig'), sky: m(h, 'sky') })),
      },
      {
        title: 'Strongest C/N0',
        unit: ' dB-Hz',
        digits: 0,
        series: [{ key: 'v', name: 'C/N0' }],
        rows: health.map((h) => ({ t: t(h), v: m(h, 'cn0') })),
      },
      {
        title: 'GSM signal',
        unit: ' CSQ',
        digits: 0,
        series: [{ key: 'v', name: 'CSQ (0–31)' }],
        rows: health.map((h) => ({ t: t(h), v: h.metrics.csq === 99 ? null : m(h, 'csq') })),
      },
      {
        title: 'Upload failures per report',
        digits: 0,
        series: [{ key: 'v', name: 'Failures' }],
        rows: health.map((h, i) => ({ t: t(h), v: fails[i] })),
      },
      {
        title: 'Upload backlog',
        unit: ' records',
        digits: 0,
        series: [{ key: 'v', name: 'Waiting' }],
        rows: health.map((h) => ({ t: t(h), v: m(h, 'uq') })),
      },
      {
        title: 'NMEA sentences rejected per report',
        digits: 0,
        series: [{ key: 'v', name: 'Rejected' }],
        rows: health.map((h, i) => ({ t: t(h), v: rejected[i] })),
      },
      {
        title: 'Card, display and bus errors per report',
        digits: 0,
        series: [{ key: 'v', name: 'Errors' }],
        rows: health.map((h, i) => ({ t: t(h), v: errors[i] })),
      },
      {
        title: 'Free heap',
        unit: ' KiB',
        digits: 0,
        series: [
          { key: 'heap', name: 'Now' },
          { key: 'hmin', name: 'Minimum since boot' },
        ],
        rows: health.map((h) => ({ t: t(h), heap: h.metrics.heap === undefined ? null : h.metrics.heap / 1024, hmin: h.metrics.hmin === undefined ? null : h.metrics.hmin / 1024 })),
      },
      {
        title: 'Uptime',
        unit: ' min',
        digits: 0,
        series: [{ key: 'v', name: 'Uptime' }],
        rows: health.map((h) => ({ t: t(h), v: h.uptimeS === null ? null : h.uptimeS / 60 })),
      },
      {
        title: 'Lowest task stack headroom',
        unit: ' B',
        digits: 0,
        series: [{ key: 'v', name: 'Min free stack' }],
        rows: health.map((h) => ({ t: t(h), v: h.stackFree && h.stackFree.length ? Math.min(...h.stackFree) : null })),
      },
    ];
  }, [positions, health]);

  return (
    <div className="charts">
      {specs.map((spec) => (
        <Chart key={spec.title} spec={spec} theme={theme} />
      ))}
    </div>
  );
}
