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
  } else if (!config.googleClientId) {
    log.warn('GOOGLE_CLIENT_ID is empty: nobody can sign in. Set it, or AUTH_DISABLED=true for local work.');
  } else if (config.allowedEmails.length === 0) {
    log.warn('ALLOWED_EMAILS is empty: every Google sign-in will be refused.');
  }
  if (config.sessionSecret === 'dev-only-insecure-secret' && !config.authDisabled) {
    log.warn('SESSION_SECRET is the built-in default; set a long random string.');
  }
}
