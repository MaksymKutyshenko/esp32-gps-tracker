import type { FastifyInstance } from 'fastify';

import type { AppConfig } from '../../../shared/types.js';
import { AuthError, clearSession, currentUser, issueSession, verifyAccessKey, verifyGoogleCredential } from '../auth.js';
import { config } from '../config.js';
import { hub } from '../events.js';
import * as repo from '../repo.js';
import { buildOverview } from './ingest.js';

const PUBLIC = new Set(['/api/config', '/api/auth/google', '/api/auth/key', '/api/auth/dev', '/api/auth/logout', '/api/healthz']);

function parseRange(query: Record<string, unknown>, defaultHours: number, maxLimit: number): repo.RangeQuery {
  const now = Date.now();
  const to = typeof query.to === 'string' && !Number.isNaN(Date.parse(query.to)) ? new Date(query.to) : new Date(now);
  const from =
    typeof query.from === 'string' && !Number.isNaN(Date.parse(query.from))
      ? new Date(query.from)
      : new Date(to.getTime() - defaultHours * 3600_000);
  const requested = Number(query.limit);
  const limit = Number.isFinite(requested) && requested > 0 ? Math.min(requested, maxLimit) : maxLimit;
  return { from, to, limit };
}

export async function apiRoutes(app: FastifyInstance): Promise<void> {
  app.addHook('onRequest', async (request, reply) => {
    if (!request.url.startsWith('/api/') || PUBLIC.has(request.url.split('?')[0])) return;
    const me = await currentUser(request);
    if (!me) return reply.code(401).send({ error: 'sign in required' });
  });

  app.get('/api/healthz', async () => ({ ok: true, streams: hub.size }));

  app.get('/api/config', async (): Promise<AppConfig> => ({
    googleClientId: config.googleClientId,
    accessKeyEnabled: config.accessKey !== null,
    authDisabled: config.authDisabled,
    onlineTimeoutS: config.onlineTimeoutS,
  }));

  app.post<{ Body: { credential?: string } }>('/api/auth/google', async (request, reply) => {
    const credential = request.body?.credential;
    if (!credential) return reply.code(400).send({ error: 'credential missing' });
    try {
      const me = await verifyGoogleCredential(credential);
      await issueSession(reply, request, me);
      app.log.info(`signed in: ${me.email}`);
      return me;
    } catch (err) {
      const status = err instanceof AuthError ? err.status : 401;
      app.log.warn(`sign-in refused: ${(err as Error).message}`);
      return reply.code(status).send({ error: (err as Error).message });
    }
  });

  app.post<{ Body: { key?: string } }>('/api/auth/key', async (request, reply) => {
    const key = request.body?.key;
    if (typeof key !== 'string' || key.length === 0 || key.length > 256) {
      return reply.code(400).send({ error: 'key missing' });
    }
    try {
      const me = verifyAccessKey(key, request.ip);
      await issueSession(reply, request, me);
      app.log.info(`signed in with the access key from ${request.ip}`);
      return me;
    } catch (err) {
      const status = err instanceof AuthError ? err.status : 401;
      app.log.warn(`access key refused from ${request.ip}: ${(err as Error).message}`);
      return reply.code(status).send({ error: (err as Error).message });
    }
  });

  // Development only (AUTH_DISABLED=true): there is no cookie to issue, the
  // session hook already treats everyone as signed in.
  app.post('/api/auth/dev', async (request, reply) => {
    if (!config.authDisabled) return reply.code(404).send({ error: 'not available' });
    return currentUser(request);
  });

  app.post('/api/auth/logout', async (_request, reply) => {
    clearSession(reply);
    return { ok: true };
  });

  app.get('/api/me', async (request) => currentUser(request));

  app.get('/api/devices', async () => repo.listDevices(new Date(), config.onlineTimeoutS));

  app.get<{ Params: { id: string } }>('/api/devices/:id/overview', async (request, reply) => {
    const overview = await buildOverview(request.params.id);
    return overview ?? reply.code(404).send({ error: 'unknown device' });
  });

  app.get<{ Params: { id: string } }>('/api/devices/:id/positions', async (request) =>
    repo.positionsInRange(request.params.id, parseRange(request.query as Record<string, unknown>, 24, 20_000)),
  );

  app.get<{ Params: { id: string } }>('/api/devices/:id/health', async (request) =>
    repo.healthInRange(request.params.id, parseRange(request.query as Record<string, unknown>, 24, 5_000)),
  );

  app.get('/api/stream', async (_request, reply) => {
    hub.subscribe(reply);
    // The reply is owned by the hub now; Fastify must not end it.
    return reply;
  });
}
