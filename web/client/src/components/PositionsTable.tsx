import type { Position } from '../../../shared/types';
import { ago, fmtTime, knotsToKmh, num } from '../format';

const MAX_ROWS = 1000;

interface Props {
  positions: Position[];
  now: number;
  focus: Position | null;
  onFocus: (p: Position | null) => void;
  latest: Position | null;
}

export function PositionsTable({ positions, now, focus, onFocus, latest }: Props) {
  if (positions.length === 0) return <div className="empty">No positions in this range.</div>;
  const rows = positions.slice(-MAX_ROWS).reverse();
  return (
    <div className="table-wrap">
      <table>
        <thead>
          <tr>
            <th>Fix time (UTC)</th>
            <th>Age</th>
            <th className="num">Latitude</th>
            <th className="num">Longitude</th>
            <th className="num">Speed km/h</th>
            <th className="num">Course</th>
            <th className="num">Altitude m</th>
            <th className="num">HDOP</th>
            <th className="num">Battery %</th>
            <th className="num">Received after</th>
          </tr>
        </thead>
        <tbody>
          {rows.map((p) => {
            const delay = (Date.parse(p.receivedAt) - Date.parse(p.fixTime)) / 1000;
            return (
              <tr
                key={p.id}
                className="clickable"
                style={focus?.id === p.id ? { background: 'var(--accent-soft)' } : undefined}
                onClick={() => onFocus(focus?.id === p.id ? null : p)}
                title="Click to show on the map"
              >
                <td className="mono">
                  {fmtTime(p.fixTime)} {latest?.id === p.id && <span className="chip">latest</span>}
                </td>
                <td className="muted">{ago(p.fixTime, now)}</td>
                <td className="num mono">{p.lat.toFixed(7)}</td>
                <td className="num mono">{p.lon.toFixed(7)}</td>
                <td className="num">{num(knotsToKmh(p.speedKn), 1)}</td>
                <td className="num">{num(p.bearingDeg, 1)}</td>
                <td className="num">{num(p.altitudeM, 1)}</td>
                <td className="num">{num(p.hdop, 2)}</td>
                <td className="num">{num(p.batteryPct, 0)}</td>
                <td className="num muted" title="Delay between the fix and the server receiving it">
                  {delay >= 0 && delay < 864000 ? `${delay < 120 ? `${Math.round(delay)} s` : `${(delay / 60).toFixed(0)} min`}` : '–'}
                </td>
              </tr>
            );
          })}
        </tbody>
      </table>
      {positions.length > MAX_ROWS && <div className="small muted" style={{ padding: 8 }}>Showing the newest {MAX_ROWS} of {positions.length}.</div>}
    </div>
  );
}
