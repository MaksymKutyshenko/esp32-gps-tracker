export function ago(iso: string | null | undefined, now: number): string {
  if (!iso) return 'never';
  const s = Math.max(0, (now - Date.parse(iso)) / 1000);
  if (s < 60) return `${Math.floor(s)} s ago`;
  if (s < 3600) return `${Math.floor(s / 60)} min ago`;
  if (s < 86400) return `${(s / 3600).toFixed(1)} h ago`;
  return `${(s / 86400).toFixed(1)} d ago`;
}

export function duration(seconds: number | null | undefined): string {
  if (seconds === null || seconds === undefined) return '–';
  if (seconds < 60) return `${seconds} s`;
  if (seconds < 3600) return `${Math.floor(seconds / 60)} min ${seconds % 60} s`;
  if (seconds < 86400) return `${Math.floor(seconds / 3600)} h ${Math.floor((seconds % 3600) / 60)} min`;
  return `${Math.floor(seconds / 86400)} d ${Math.floor((seconds % 86400) / 3600)} h`;
}

const timeFormat = new Intl.DateTimeFormat(undefined, {
  month: 'short',
  day: '2-digit',
  hour: '2-digit',
  minute: '2-digit',
  second: '2-digit',
});
const clockFormat = new Intl.DateTimeFormat(undefined, { hour: '2-digit', minute: '2-digit', second: '2-digit' });

export const fmtTime = (iso: string | number) => timeFormat.format(typeof iso === 'number' ? iso : Date.parse(iso));
export const fmtClock = (ms: number) => clockFormat.format(ms);

export function num(value: number | null | undefined, digits = 0, unit = ''): string {
  if (value === null || value === undefined || Number.isNaN(value)) return '–';
  return `${value.toLocaleString(undefined, { minimumFractionDigits: digits, maximumFractionDigits: digits })}${unit}`;
}

export const knotsToKmh = (kn: number | null | undefined) => (kn === null || kn === undefined ? null : kn * 1.852);

export function compact(value: number): string {
  if (Math.abs(value) >= 1_000_000) return `${(value / 1_000_000).toFixed(1)}M`;
  if (Math.abs(value) >= 10_000) return `${(value / 1000).toFixed(0)}K`;
  if (Math.abs(value) >= 1_000) return `${(value / 1000).toFixed(1)}K`;
  return `${value}`;
}
