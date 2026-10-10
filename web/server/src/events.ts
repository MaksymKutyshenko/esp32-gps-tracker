import type { FastifyReply } from 'fastify';

import type { StreamEvent } from '../../shared/types.js';

// Server-sent events: every signed-in browser tab holds one connection and
// gets each ingested report the moment it lands.
class EventHub {
  private readonly clients = new Set<FastifyReply>();

  subscribe(reply: FastifyReply): void {
    reply.raw.writeHead(200, {
      'Content-Type': 'text/event-stream',
      'Cache-Control': 'no-cache, no-transform',
      Connection: 'keep-alive',
      'X-Accel-Buffering': 'no',
    });
    reply.raw.write(': connected\n\n');
    this.clients.add(reply);
    reply.raw.on('close', () => this.clients.delete(reply));
  }

  broadcast(event: StreamEvent): void {
    const frame = `event: ${event.type}\ndata: ${JSON.stringify(event)}\n\n`;
    for (const client of this.clients) {
      if (!client.raw.write(frame)) client.raw.once('drain', () => undefined);
    }
  }

  get size(): number {
    return this.clients.size;
  }
}

export const hub = new EventHub();

setInterval(() => hub.broadcast({ type: 'ping' }), 25_000).unref();
