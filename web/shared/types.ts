// Types shared by the server and the client. The server is the source of
// truth for every value here; the client only renders them.

export type ModuleLevel = 'ok' | 'warn' | 'error' | 'unknown';

export interface ModuleDetail {
  label: string;
  value: string;
}

// One card on the dashboard: a firmware module (GNSS, storage, uplink...)
// with a verdict derived from the last two health reports.
export interface ModuleStatus {
  key: string;
  name: string;
  level: ModuleLevel;
  summary: string;
  issues: string[];
  details: ModuleDetail[];
}

export interface Position {
  id: number;
  deviceId: string;
  fixTime: string;
  receivedAt: string;
  lat: number;
  lon: number;
  altitudeM: number | null;
  speedKn: number | null;
  bearingDeg: number | null;
  hdop: number | null;
  batteryPct: number | null;
  extra: Record<string, string>;
}

// A health report as the firmware sends it (HealthReport.hpp): the short
// query keys are kept as-is in `metrics`, see HEALTH_KEYS for their meaning.
export interface HealthReport {
  id: number;
  deviceId: string;
  receivedAt: string;
  uptimeS: number | null;
  resetReason: string | null;
  state: string | null;
  batteryMv: number | null;
  firmware: string | null;
  metrics: Record<string, number>;
  // Free stack bytes per task: gps, display, logger, uplink, ui.
  stackFree: number[] | null;
}

export interface DeviceSummary {
  id: string;
  name: string | null;
  firstSeen: string;
  lastSeen: string;
  online: boolean;
  state: string | null;
  lastPosition: Position | null;
}

export interface DeviceOverview {
  device: DeviceSummary;
  lastHealth: HealthReport | null;
  previousHealth: HealthReport | null;
  modules: ModuleStatus[];
  onlineTimeoutS: number;
}

export type Role = 'admin' | 'viewer';

export interface Me {
  email: string;
  name: string | null;
  picture: string | null;
  // Admins (admin key, Google, dev mode) may rotate the shared viewer key.
  role: Role;
}

export interface AccessKeyInfo {
  configured: boolean;
  updatedAt: string | null;
  updatedBy: string | null;
}

export interface AppConfig {
  googleClientId: string | null;
  accessKeyEnabled: boolean;
  authDisabled: boolean;
  onlineTimeoutS: number;
}

export type StreamEvent =
  | { type: 'position'; deviceId: string; position: Position; overview: DeviceOverview }
  | { type: 'health'; deviceId: string; health: HealthReport; overview: DeviceOverview }
  | { type: 'ping' };

export const TASK_NAMES = ['gps', 'display', 'logger', 'uplink', 'ui'] as const;

export const STATE_LABELS: Record<string, string> = {
  boot: 'Booting',
  nosignal: 'No signal',
  acquiring: 'Acquiring',
  tracking: 'Tracking',
  stationary: 'Parked',
  sleeping: 'Sleeping',
};

export const RESET_LABELS: Record<string, string> = {
  poweron: 'Power-on',
  sw: 'Software reset',
  panic: 'Panic',
  intwdt: 'Interrupt watchdog',
  taskwdt: 'Task watchdog',
  wdt: 'Other watchdog',
  deepsleep: 'Deep-sleep wake',
  brownout: 'Brownout',
  other: 'Other',
};

// The health query keys, grouped by module, with a human label. Order is
// the display order.
export interface HealthKey {
  key: string;
  label: string;
  group: 'gnss' | 'storage' | 'uplink' | 'ui' | 'system';
  counter: boolean; // monotonic since boot: show the delta between reports
}

export const HEALTH_KEYS: HealthKey[] = [
  { key: 'fix', label: 'Fix valid', group: 'gnss', counter: false },
  { key: 'sat', label: 'Satellites used', group: 'gnss', counter: false },
  { key: 'sky', label: 'Satellites in view', group: 'gnss', counter: false },
  { key: 'sig', label: 'Satellites tracked', group: 'gnss', counter: false },
  { key: 'cn0', label: 'Strongest C/N0 (dB-Hz)', group: 'gnss', counter: false },
  { key: 'q', label: 'GGA fix quality', group: 'gnss', counter: false },
  { key: 'nrx', label: 'NMEA sentences received', group: 'gnss', counter: true },
  { key: 'nrej', label: 'NMEA sentences rejected', group: 'gnss', counter: true },
  { key: 'ncks', label: 'Checksum failures', group: 'gnss', counter: true },
  { key: 'nfld', label: 'Field failures', group: 'gnss', counter: true },
  { key: 'sil', label: 'Receiver silences', group: 'gnss', counter: true },
  { key: 'sd', label: 'Card mounted', group: 'storage', counter: false },
  { key: 'lok', label: 'Records logged', group: 'storage', counter: true },
  { key: 'ldrop', label: 'Records dropped (log queue)', group: 'storage', counter: true },
  { key: 'lerr', label: 'Storage errors', group: 'storage', counter: true },
  { key: 'lrm', label: 'Card remounts', group: 'storage', counter: true },
  { key: 'uok', label: 'Records uploaded', group: 'uplink', counter: true },
  { key: 'udrop', label: 'Records dropped (uplink queue)', group: 'uplink', counter: true },
  { key: 'ufail', label: 'Upload failures', group: 'uplink', counter: true },
  { key: 'ucyc', label: 'Modem power cycles', group: 'uplink', counter: true },
  { key: 'uq', label: 'Upload backlog', group: 'uplink', counter: false },
  { key: 'csq', label: 'GSM signal (CSQ 0-31)', group: 'uplink', counter: false },
  { key: 'derr', label: 'Display errors', group: 'ui', counter: true },
  { key: 'bto', label: 'Bus timeouts', group: 'ui', counter: true },
  { key: 'berr', label: 'Battery read errors', group: 'ui', counter: true },
  { key: 'bdrop', label: 'Button edges dropped', group: 'ui', counter: true },
  { key: 'bmv', label: 'Battery (mV)', group: 'system', counter: false },
  { key: 'heap', label: 'Free heap (bytes)', group: 'system', counter: false },
  { key: 'hmin', label: 'Min free heap (bytes)', group: 'system', counter: false },
];
