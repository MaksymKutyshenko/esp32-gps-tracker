import { buildApp } from './app.js';
import { config, warnAboutConfig } from './config.js';
import { migrate } from './db.js';
import { initKeys } from './keys.js';

const app = await buildApp();
warnAboutConfig(app.log);
await migrate(app.log);
await initKeys(app.log);
await app.listen({ port: config.port, host: config.host });
app.log.info(`device endpoint: http://<this host>:${config.port}/?id=<device>&lat=..&lon=..`);
