import { useCallback, useEffect, useMemo, useRef, useState } from 'react';

import type { AppConfig, BootStats, DeviceOverview, DeviceSummary, HealthReport, Me, Position, StreamEvent } from '../../../shared/types';
import { getBoots, getHealth, getOverview, getPositions, listDevices } from '../api';
import type { Theme } from '../hooks';
import { useNow } from '../hooks';
import { AccessKeyDialog } from './AccessKeyDialog';
import { Charts } from './Charts';
import { FixStrip } from './FixStrip';
import { Header } from './Header';
import { HealthTable } from './HealthTable';
import { MapView } from './MapView';
import { ModulesPanel } from './ModulesPanel';
import { PositionsTable } from './PositionsTable';
import { UptimePanel } from './UptimePanel';

export type RangeKey = '1h' | '6h' | '24h' | '7d' | '30d';
const RANGE_MS: Record<RangeKey, number> = {
  '1h': 3600_000,
  '6h': 6 * 3600_000,
  '24h': 24 * 3600_000,
  '7d': 7 * 86400_000,
  '30d': 30 * 86400_000,
};

type Tab = 'charts' | 'uptime' | 'positions' | 'health';

interface Props {
  me: Me;
  config: AppConfig;
  theme: Theme;
  onToggleTheme: () => void;
  onSignOut: () => void;
}

export function Dashboard({ me, config, theme, onToggleTheme, onSignOut }: Props) {
  const [devices, setDevices] = useState<DeviceSummary[]>([]);
  const [selectedId, setSelectedId] = useState<string | null>(() => localStorage.getItem('device'));
  const [overview, setOverview] = useState<DeviceOverview | null>(null);
  const [positions, setPositions] = useState<Position[]>([]);
  const [health, setHealth] = useState<HealthReport[]>([]);
  const [boots, setBoots] = useState<BootStats | null>(null);
  const [range, setRange] = useState<RangeKey>(() => (localStorage.getItem('range') as RangeKey) || '24h');
  const [tab, setTab] = useState<Tab>('charts');
  const [focus, setFocus] = useState<Position | null>(null);
  const [streamOk, setStreamOk] = useState(true);
  const [loaded, setLoaded] = useState(false);
  const [keyDialog, setKeyDialog] = useState(false);
  const now = useNow();

  const selected = useMemo(() => devices.find((d) => d.id === selectedId) ?? devices[0] ?? null, [devices, selectedId]);
  const deviceId = selected?.id ?? null;

  const refreshDevices = useCallback(async () => {
    const list = await listDevices();
    setDevices(list);
    setLoaded(true);
  }, []);

  useEffect(() => {
    refreshDevices().catch(() => setLoaded(true));
    // The list's online flags are recomputed by the server; refresh now and then.
    const id = setInterval(() => refreshDevices().catch(() => undefined), 30_000);
    return () => clearInterval(id);
  }, [refreshDevices]);

  useEffect(() => {
    if (deviceId) localStorage.setItem('device', deviceId);
  }, [deviceId]);
  useEffect(() => localStorage.setItem('range', range), [range]);

  // History for the selected device and range.
  const rangeWindow = useCallback(() => ({ from: new Date(Date.now() - RANGE_MS[range]), to: new Date() }), [range]);
  useEffect(() => {
    if (!deviceId) return;
    let cancelled = false;
    setFocus(null);
    const r = rangeWindow();
    setBoots(null);
    Promise.all([getOverview(deviceId), getPositions(deviceId, r), getHealth(deviceId, r), getBoots(deviceId, r)])
      .then(([o, p, h, b]) => {
        if (cancelled) return;
        setOverview(o);
        setPositions(p);
        setHealth(h);
        setBoots(b);
      })
      .catch(() => undefined);
    return () => {
      cancelled = true;
    };
  }, [deviceId, rangeWindow]);

  // Live updates. The stream carries a fresh overview with every report, so
  // nothing needs refetching; positions and health rows are appended.
  const deviceRef = useRef(deviceId);
  deviceRef.current = deviceId;
  const rangeRef = useRef(rangeWindow);
  rangeRef.current = rangeWindow;
  useEffect(() => {
    const source = new EventSource('/api/stream');
    const onEvent = (e: MessageEvent) => {
      const event = JSON.parse(e.data) as StreamEvent;
      if (event.type === 'ping') return;
      setDevices((list) => {
        const known = list.some((d) => d.id === event.deviceId);
        const next = known ? list.map((d) => (d.id === event.deviceId ? event.overview.device : d)) : [...list, event.overview.device];
        return next;
      });
      if (event.deviceId !== deviceRef.current) return;
      setOverview(event.overview);
      if (event.type === 'position') {
        setPositions((list) => (list.some((p) => p.id === event.position.id) ? list : [...list, event.position]));
      } else {
        setHealth((list) => (list.some((h) => h.id === event.health.id) ? list : [...list, event.health]));
        // Boot statistics are cheap to recompute server-side; refresh them
        // with every health report so a reboot shows up at once.
        const id = event.deviceId;
        getBoots(id, rangeRef.current()).then((b) => deviceRef.current === id && setBoots(b)).catch(() => undefined);
      }
    };
    source.addEventListener('position', onEvent);
    source.addEventListener('health', onEvent);
    source.onopen = () => setStreamOk(true);
    source.onerror = () => setStreamOk(false);
    return () => source.close();
  }, []);

  // Online status also ages locally between server events.
  const liveOverview = useMemo(() => {
    if (!overview) return null;
    const online = now - Date.parse(overview.device.lastSeen) < overview.onlineTimeoutS * 1000;
    if (online === overview.device.online) return overview;
    const device = { ...overview.device, online };
    const modules = overview.modules.map((m) =>
      m.key === 'device'
        ? { ...m, level: online ? m.level : 'error' as const, summary: online ? 'Online' : 'Offline' }
        : m,
    );
    return { ...overview, device, modules };
  }, [overview, now]);

  // Old points leave the window once a minute, not on every clock tick:
  // every change here re-renders the map and the charts.
  const minute = Math.floor(now / 60_000);
  const positionsInRange = useMemo(() => {
    const from = minute * 60_000 - RANGE_MS[range];
    return positions.filter((p) => Date.parse(p.fixTime) >= from);
  }, [positions, range, minute]);
  const healthInRange = useMemo(() => {
    const from = minute * 60_000 - RANGE_MS[range];
    return health.filter((h) => Date.parse(h.receivedAt) >= from);
  }, [health, range, minute]);

  const header = (
    <Header
      me={me}
      devices={devices.map((d) => (liveOverview && d.id === liveOverview.device.id ? liveOverview.device : d))}
      selected={liveOverview?.device ?? selected}
      onSelect={setSelectedId}
      range={range}
      onRange={setRange}
      theme={theme}
      onToggleTheme={onToggleTheme}
      onSignOut={onSignOut}
      streamOk={streamOk}
      now={now}
      onAccessKey={() => setKeyDialog(true)}
    />
  );
  const dialog = keyDialog ? <AccessKeyDialog onClose={() => setKeyDialog(false)} /> : null;

  if (!selected) {
    return (
      <div className="app">
        {header}
        {dialog}
        <div className="empty">
          {loaded ? (
            <>
              <p>No device has reported yet.</p>
              <p className="small">
                Point the tracker at this server: <code className="mono">set url http://&lt;host&gt;:{location.port || 5055}</code> on
                its console, then <code className="mono">save</code>. The first health report arrives within a minute of the
                GPRS bearer coming up.
              </p>
            </>
          ) : (
            'Loading…'
          )}
        </div>
      </div>
    );
  }

  const device = liveOverview?.device ?? selected;
  const lastPosition = device.lastPosition;

  return (
    <div className="app">
      {config.authDisabled && <div className="banner">Development mode: authentication is disabled on this server.</div>}
      {header}
      {dialog}
      <div className="main">
        <div className="left">
          <MapView
            device={device}
            positions={positionsInRange}
            focus={focus}
            theme={theme}
            now={now}
            rangeKey={range}
          />
          <FixStrip device={device} health={liveOverview?.lastHealth ?? null} now={now} />
        </div>
        <div className="right">
          <ModulesPanel overview={liveOverview} now={now} />
        </div>
      </div>
      <div className="bottom">
        <div className="card">
          <div className="tabs">
            <button className={tab === 'charts' ? 'active' : ''} onClick={() => setTab('charts')}>
              Charts
            </button>
            <button className={tab === 'uptime' ? 'active' : ''} onClick={() => setTab('uptime')}>
              Uptime{boots ? ` (${boots.reboots} reboot${boots.reboots === 1 ? '' : 's'})` : ''}
            </button>
            <button className={tab === 'positions' ? 'active' : ''} onClick={() => setTab('positions')}>
              Positions ({positionsInRange.length})
            </button>
            <button className={tab === 'health' ? 'active' : ''} onClick={() => setTab('health')}>
              Health reports ({healthInRange.length})
            </button>
          </div>
          {tab === 'charts' && <Charts positions={positionsInRange} health={healthInRange} theme={theme} />}
          {tab === 'uptime' && (
            <UptimePanel stats={boots} lastHealth={liveOverview?.lastHealth ?? null} online={device.online} now={now} theme={theme} />
          )}
          {tab === 'positions' && (
            <PositionsTable positions={positionsInRange} now={now} focus={focus} onFocus={setFocus} latest={lastPosition} />
          )}
          {tab === 'health' && <HealthTable reports={healthInRange} now={now} />}
        </div>
      </div>
    </div>
  );
}
