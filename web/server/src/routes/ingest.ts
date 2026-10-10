// The device-facing endpoint. The firmware builds "<url>/?id=..." so the
// report lands on GET /; /ingest is an alias for people who prefer a path.
// The firmware pops a record from its queue only on HTTP 200, so anything
// stored (or already stored) answers 200 and anything unreadable 400.

import type { FastifyInstance } from 'fastify';

import type { DeviceOverview } from '../../../shared/types.js';
import { config } from '../config.js';
import { hub } from '../events.js';
import { parseReport } from '../ingest.js';
import * as repo from '../repo.js';
import { deriveModules } from '../status.js';

export async function buildOverview(deviceId: string, now = new Date()): Promise<DeviceOverview | null> {
  const device = await repo.deviceSummary(deviceId, now, config.onlineTimeoutS);
  if (!device) return null;
  const [latest = null, previous = null] = await repo.latestHealth(deviceId, 2);
  const modules = deriveModules({
    latest,
    previous,
    lastPosition: device.lastPosition,
    lastSeen: new Date(device.lastSeen),
    now,
    onlineTimeoutS: config.onlineTimeoutS,
  });
  return { device, lastHealth: latest, previousHealth: previous, modules, onlineTimeoutS: config.onlineTimeoutS };
}

export async function ingestRoutes(app: FastifyInstance): Promise<void> {
  const handler = async (query: Record<string, unknown>): Promise<{ status: number; body: string }> => {
    const now = new Date();
    const report = parseReport(query, now);
    if (report.kind === 'invalid') {
      app.log.warn({ query }, `ingest rejected: ${report.reason}`);
      return { status: 400, body: report.reason };
    }
    if (config.allowedDeviceIds.length > 0 && !config.allowedDeviceIds.includes(report.deviceId.toLowerCase())) {
      app.log.warn(`ingest refused: unknown device ${report.deviceId}`);
      return { status: 403, body: 'unknown device' };
    }

    await repo.touchDevice(report.deviceId, now);
    if (report.kind === 'position') {
      const position = await repo.insertPosition(report, now);
      if (position) {
        const overview = await buildOverview(report.deviceId, now);
        if (overview) hub.broadcast({ type: 'position', deviceId: report.deviceId, position, overview });
        app.log.info(`position ${report.deviceId} ${report.lat},${report.lon} @ ${report.fixTime.toISOString()}`);
      } else {
        app.log.info(`position ${report.deviceId} duplicate of ${report.fixTime.toISOString()}`);
      }
    } else {
      const health = await repo.insertHealth(report, report.reportedAt ?? now);
      const overview = await buildOverview(report.deviceId, now);
      if (overview) hub.broadcast({ type: 'health', deviceId: report.deviceId, health, overview });
      app.log.info(`health ${report.deviceId} state=${report.state} up=${report.uptimeS}s`);
    }
    return { status: 200, body: 'OK' };
  };

  app.get('/ingest', async (request, reply) => {
    const { status, body } = await handler(request.query as Record<string, unknown>);
    return reply.code(status).type('text/plain').send(body);
  });

  // GET / is the SPA unless it carries a device report.
  app.get('/', async (request, reply) => {
    const query = request.query as Record<string, unknown>;
    if (typeof query.id === 'string') {
      const { status, body } = await handler(query);
      return reply.code(status).type('text/plain').send(body);
    }
    return reply.callNotFound();
  });
}
