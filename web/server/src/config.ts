// Environment parsing in one place; everything else reads `config`.

function list(value: string | undefined): string[] {
  return (value ?? '')
    .split(',')
    .map((s) => s.trim().toLowerCase())
    .filter(Boolean);
}

function int(value: string | undefined, fallback: number): number {
  const n = Number(value);
  return Number.isFinite(n) && n > 0 ? n : fallback;
}

export const config = {
  port: int(process.env.PORT, 5055),
  host: process.env.HOST ?? '0.0.0.0',
  databaseUrl: process.env.DATABASE_URL ?? 'postgres://tracker:tracker@localhost:5432/tracker',
  googleClientId: process.env.GOOGLE_CLIENT_ID || null,
  accessKey: process.env.ACCESS_KEY || null, // seeds the viewer key once
  adminKey: process.env.ADMIN_KEY || null,
  allowedEmails: list(process.env.ALLOWED_EMAILS),
  sessionSecret: process.env.SESSION_SECRET || 'dev-only-insecure-secret',
  authDisabled: process.env.AUTH_DISABLED === 'true',
  onlineTimeoutS: int(process.env.ONLINE_TIMEOUT_S, 150),
  allowedDeviceIds: list(process.env.ALLOWED_DEVICE_IDS),
  sessionDays: 30,
};

export function warnAboutConfig(log: { warn: (msg: string) => void }): void {
  if (config.authDisabled) {
    log.warn('AUTH_DISABLED=true: everyone is signed in as dev@localhost. Never run like this on the internet.');
  } else if (!config.adminKey && !config.googleClientId) {
    log.warn('No ADMIN_KEY and no GOOGLE_CLIENT_ID: nobody can rotate the viewer key from the dashboard.');
  } else if (config.googleClientId && config.allowedEmails.length === 0) {
    log.warn('ALLOWED_EMAILS is empty: every Google sign-in will be refused.');
  }
  for (const [name, value] of [['ACCESS_KEY', config.accessKey], ['ADMIN_KEY', config.adminKey]] as const) {
    if (value && value.length < 12) log.warn(`${name} is short; use at least 12 random characters (openssl rand -base64 18).`);
  }
  if (config.sessionSecret === 'dev-only-insecure-secret' && !config.authDisabled) {
    log.warn('SESSION_SECRET is the built-in default; set a long random string.');
  }
}
