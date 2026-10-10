import type { DeviceSummary, Me } from '../../../shared/types';
import { ago } from '../format';
import type { Theme } from '../hooks';
import type { RangeKey } from './Dashboard';

const RANGES: { key: RangeKey; label: string }[] = [
  { key: '1h', label: '1 h' },
  { key: '6h', label: '6 h' },
  { key: '24h', label: '24 h' },
  { key: '7d', label: '7 d' },
  { key: '30d', label: '30 d' },
];

interface Props {
  me: Me;
  devices: DeviceSummary[];
  selected: DeviceSummary | null;
  onSelect: (id: string) => void;
  range: RangeKey;
  onRange: (r: RangeKey) => void;
  theme: Theme;
  onToggleTheme: () => void;
  onSignOut: () => void;
  streamOk: boolean;
  now: number;
}

export function Header(p: Props) {
  const d = p.selected;
  return (
    <header className="header">
      <div className="brand">
        <span className="dot" />
        GPS Tracker
      </div>
      {p.devices.length > 1 ? (
        <select className="select" value={d?.id ?? ''} onChange={(e) => p.onSelect(e.target.value)} aria-label="Device">
          {p.devices.map((dev) => (
            <option key={dev.id} value={dev.id}>
              {dev.name ?? dev.id}
            </option>
          ))}
        </select>
      ) : (
        d && <span className="chip mono">{d.name ?? d.id}</span>
      )}
      {d && (
        <span className={`pill ${d.online ? 'online' : 'offline'}`} title={`Last report ${new Date(d.lastSeen).toLocaleString()}`}>
          <span className="led" />
          {d.online ? 'Online' : 'Offline'}
          <span className="sub">· last report {ago(d.lastSeen, p.now)}</span>
        </span>
      )}
      {!p.streamOk && (
        <span className="chip" title="Live updates are reconnecting">
          ⟳ reconnecting…
        </span>
      )}
      <div className="spacer" />
      <div className="segmented" role="group" aria-label="Time range">
        {RANGES.map((r) => (
          <button key={r.key} className={r.key === p.range ? 'active' : ''} onClick={() => p.onRange(r.key)}>
            {r.label}
          </button>
        ))}
      </div>
      <button className="btn icon" onClick={p.onToggleTheme} title="Toggle light / dark">
        {p.theme === 'dark' ? '☀' : '☾'}
      </button>
      <span className="chip" title={p.me.email}>
        {p.me.picture && <img src={p.me.picture} alt="" referrerPolicy="no-referrer" />}
        {p.me.name ?? p.me.email}
      </span>
      <button className="btn" onClick={p.onSignOut}>
        Sign out
      </button>
    </header>
  );
}
