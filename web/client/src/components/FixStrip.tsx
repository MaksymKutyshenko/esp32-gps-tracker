import type { DeviceSummary, HealthReport } from '../../../shared/types';
import { STATE_LABELS } from '../../../shared/types';
import { ago, knotsToKmh, num } from '../format';

// The headline numbers under the map: where, how fast, how good the fix.
export function FixStrip({ device, health, now }: { device: DeviceSummary; health: HealthReport | null; now: number }) {
  const p = device.lastPosition;
  const stale = !device.online;
  const m = health?.metrics ?? {};
  const state = health?.state ?? device.state;
  const tiles: { label: string; value: string; small?: boolean; mono?: boolean }[] = [
    { label: 'Latitude', value: p ? p.lat.toFixed(6) : '–', mono: true },
    { label: 'Longitude', value: p ? p.lon.toFixed(6) : '–', mono: true },
    { label: 'Speed', value: num(knotsToKmh(p?.speedKn), 1, ' km/h') },
    { label: 'Course', value: num(p?.bearingDeg, 0, '°') },
    { label: 'Altitude', value: num(p?.altitudeM, 0, ' m') },
    { label: 'HDOP', value: num(p?.hdop, 2) },
    { label: 'Satellites used / tracked / in view', value: `${m.sat ?? (p?.extra.sat ?? '–')} / ${m.sig ?? '–'} / ${m.sky ?? '–'}` },
    { label: 'Strongest C/N0', value: num(m.cn0, 0, ' dB-Hz') },
    { label: 'Tracker state', value: state ? STATE_LABELS[state] ?? state : '–', small: true },
    { label: 'Battery', value: health?.batteryMv ? `${(health.batteryMv / 1000).toFixed(2)} V` : num(p?.batteryPct, 0, ' %') },
    { label: 'Last fix', value: p ? ago(p.fixTime, now) : '–', small: true },
    { label: 'GSM signal (CSQ)', value: m.csq === undefined || m.csq === 99 ? '–' : `${m.csq} / 31` },
  ];
  return (
    <div className="tiles">
      {tiles.map((t) => (
        <div key={t.label} className={`tile ${stale ? 'stale' : ''}`} title={stale ? 'Device offline: last known value' : undefined}>
          <div className="label">{t.label}</div>
          <div className={`value ${t.small ? 'small' : ''} ${t.mono ? 'mono' : ''}`}>{t.value}</div>
        </div>
      ))}
    </div>
  );
}
