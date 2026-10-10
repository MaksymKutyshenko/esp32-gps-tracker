import L from 'leaflet';
import { useEffect, useMemo, useRef, useState } from 'react';
import { CircleMarker, MapContainer, Marker, Polyline, Popup, TileLayer, useMap } from 'react-leaflet';

import type { DeviceSummary, Position } from '../../../shared/types';
import { ago, fmtTime, knotsToKmh, num } from '../format';
import type { Theme } from '../hooks';

// OpenStreetMap's own tiles (no key). Dark mode re-tints them with a CSS
// filter on the tile pane (see styles.css) instead of a second provider.
const TILES = {
  url: 'https://tile.openstreetmap.org/{z}/{x}/{y}.png',
  attribution: '&copy; <a href="https://www.openstreetmap.org/copyright">OpenStreetMap</a> contributors',
};

const KYIV: [number, number] = [50.4501, 30.5234];

function markerIcon(online: boolean): L.DivIcon {
  return L.divIcon({
    className: '',
    html: `<div class="marker ${online ? 'pulse' : 'offline'}"></div>`,
    iconSize: [18, 18],
    iconAnchor: [9, 9],
    popupAnchor: [0, -12],
  });
}

// Keeps the view on the data: fits the trail when the device or range
// changes, follows the newest point while "Follow" is on, pans to a row
// picked in the table.
function ViewController({
  trail,
  latest,
  follow,
  focus,
  fitKey,
}: {
  trail: [number, number][];
  latest: [number, number] | null;
  follow: boolean;
  focus: [number, number] | null;
  fitKey: string;
}) {
  const map = useMap();
  const lastFit = useRef<string>('');
  const lastLatest = useRef<string>('');

  useEffect(() => {
    if (lastFit.current === fitKey) return;
    if (trail.length > 1) {
      map.fitBounds(L.latLngBounds(trail), { padding: [32, 32], maxZoom: 16 });
      lastFit.current = fitKey;
    } else if (latest) {
      map.setView(latest, 15);
      lastFit.current = fitKey;
    }
  }, [map, trail, latest, fitKey]);

  useEffect(() => {
    if (!latest || !follow) return;
    const key = latest.join(',');
    if (key === lastLatest.current) return;
    lastLatest.current = key;
    if (!map.getBounds().pad(-0.2).contains(latest)) map.panTo(latest, { animate: true });
  }, [map, latest, follow]);

  useEffect(() => {
    if (focus) map.panTo(focus, { animate: true });
  }, [map, focus]);

  return null;
}

interface Props {
  device: DeviceSummary;
  positions: Position[];
  focus: Position | null;
  theme: Theme;
  now: number;
}

export function MapView({ device, positions, focus, theme, now }: Props) {
  const [follow, setFollow] = useState(true);
  const last = device.lastPosition;
  const trail = useMemo(() => positions.map((p) => [p.lat, p.lon] as [number, number]), [positions]);
  const latest: [number, number] | null = last ? [last.lat, last.lon] : null;
  const fixAge = last ? (now - Date.parse(last.fixTime)) / 1000 : null;

  return (
    <div className="card map-card">
      <div className="card-head">
        <h2>Map</h2>
        {last ? (
          <span className={`small ${device.online ? '' : 'muted'}`}>
            {device.online ? 'Current position' : 'Last known position'} · fix {ago(last.fixTime, now)}
          </span>
        ) : (
          <span className="small muted">No position yet</span>
        )}
        <div className="map-toolbar">
          <span className="small muted">{trail.length} points in range</span>
          <label className="btn small" style={{ cursor: 'pointer' }}>
            <input type="checkbox" checked={follow} onChange={(e) => setFollow(e.target.checked)} /> Follow
          </label>
        </div>
      </div>
      <div className="map">
        <MapContainer center={latest ?? KYIV} zoom={latest ? 15 : 11} style={{ height: '100%' }} zoomControl>
          <TileLayer key={theme} url={TILES.url} attribution={TILES.attribution} maxZoom={19} className={theme === 'dark' ? 'tiles-dark' : ''} />
          {trail.length > 1 && (
            <Polyline
              positions={trail}
              pathOptions={{ color: device.online ? '#2a78d6' : '#8a8984', weight: 3, opacity: device.online ? 0.85 : 0.55 }}
            />
          )}
          {trail.length > 1 && (
            <CircleMarker center={trail[0]} radius={4} pathOptions={{ color: '#fff', fillColor: '#8a8984', fillOpacity: 1, weight: 2 }}>
              <Popup>Range start · {fmtTime(positions[0].fixTime)}</Popup>
            </CircleMarker>
          )}
          {focus && (
            <CircleMarker center={[focus.lat, focus.lon]} radius={7} pathOptions={{ color: '#eb6834', weight: 2, fillOpacity: 0.2 }}>
              <Popup>
                {fmtTime(focus.fixTime)}
                <br />
                {num(knotsToKmh(focus.speedKn), 1)} km/h · {num(focus.altitudeM, 0)} m
              </Popup>
            </CircleMarker>
          )}
          {last && latest && (
            <Marker position={latest} icon={markerIcon(device.online)} zIndexOffset={1000}>
              <Popup>
                <strong>{device.online ? 'Current' : 'Last known'} position</strong>
                <br />
                {fmtTime(last.fixTime)} ({fixAge !== null ? ago(last.fixTime, now) : ''})
                <br />
                <span className="mono">
                  {last.lat.toFixed(6)}, {last.lon.toFixed(6)}
                </span>
                <br />
                {num(knotsToKmh(last.speedKn), 1)} km/h · {num(last.bearingDeg, 0)}° · {num(last.altitudeM, 0)} m
              </Popup>
            </Marker>
          )}
          <ViewController trail={trail} latest={latest} follow={follow} focus={focus ? [focus.lat, focus.lon] : null} fitKey={`${device.id}:${positions[0]?.id ?? 'none'}`} />
        </MapContainer>
      </div>
    </div>
  );
}
