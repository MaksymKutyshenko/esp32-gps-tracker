import { existsSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

import cookie from '@fastify/cookie';
import fastifyStatic from '@fastify/static';
import Fastify from 'fastify';

import { apiRoutes } from './routes/api.js';
import { ingestRoutes } from './routes/ingest.js';

export async function buildApp() {
  const app = Fastify({
    logger: { level: process.env.LOG_LEVEL ?? 'info' },
    trustProxy: true,
    // The SIM800C sends a bare HTTP/1.1 GET; nothing exotic is needed.
    disableRequestLogging: true,
  });

  await app.register(cookie);
  await app.register(ingestRoutes);
  await app.register(apiRoutes);

  // The built client, when present (production image / `pnpm build`).
  const clientDist = join(dirname(fileURLToPath(import.meta.url)), '../../../../client/dist');
  const hasClient = existsSync(join(clientDist, 'index.html'));
  if (hasClient) {
    await app.register(fastifyStatic, { root: clientDist, wildcard: false, index: false });
  }
  app.setNotFoundHandler(async (request, reply) => {
    if (request.method === 'GET' && !request.url.startsWith('/api/') && hasClient) {
      return reply.sendFile('index.html');
    }
    return reply.code(404).send({ error: 'not found' });
  });

  return app;
}
