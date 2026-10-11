import type { AppConfig, DeviceOverview, DeviceSummary, HealthReport, Me, Position } from '../../shared/types';

export class ApiError extends Error {
  constructor(readonly status: number, message: string) {
    super(message);
  }
}

let onUnauthorized: (() => void) | null = null;
export function setUnauthorizedHandler(handler: () => void): void {
  onUnauthorized = handler;
}

async function api<T>(path: string, init?: RequestInit): Promise<T> {
  const response = await fetch(path, { credentials: 'same-origin', ...init });
  // A 401 anywhere but on a sign-in call means the session is gone.
  if (response.status === 401 && !path.startsWith('/api/auth/')) {
    onUnauthorized?.();
    throw new ApiError(401, 'sign in required');
  }
  if (!response.ok) {
    let message = response.statusText;
    try {
      const body = (await response.json()) as { error?: string };
      if (body.error) message = body.error;
    } catch {
      /* not JSON */
    }
    throw new ApiError(response.status, message);
  }
  return (await response.json()) as T;
}

function json(body: unknown): RequestInit {
  return { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) };
}

export const getConfig = () => api<AppConfig>('/api/config');
export const getMe = () => api<Me | null>('/api/me');
export const signInWithGoogle = (credential: string) => api<Me>('/api/auth/google', json({ credential }));
export const signInWithKey = (key: string) => api<Me>('/api/auth/key', json({ key }));
export const signInDev = () => api<Me>('/api/auth/dev', json({}));
export const signOut = () => api<{ ok: boolean }>('/api/auth/logout', json({}));
export const listDevices = () => api<DeviceSummary[]>('/api/devices');
export const getOverview = (id: string) => api<DeviceOverview>(`/api/devices/${encodeURIComponent(id)}/overview`);

export interface Range {
  from: Date;
  to: Date;
}

function rangeQuery(r: Range): string {
  return `from=${encodeURIComponent(r.from.toISOString())}&to=${encodeURIComponent(r.to.toISOString())}`;
}

export const getPositions = (id: string, r: Range) =>
  api<Position[]>(`/api/devices/${encodeURIComponent(id)}/positions?${rangeQuery(r)}`);
export const getHealth = (id: string, r: Range) =>
  api<HealthReport[]>(`/api/devices/${encodeURIComponent(id)}/health?${rangeQuery(r)}`);
