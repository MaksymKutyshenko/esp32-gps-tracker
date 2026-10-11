// Google sign-in: the browser gets an ID token from Google Identity
// Services, posts it here, and gets a signed session cookie back. Only the
// e-mails in ALLOWED_EMAILS are let in. Sessions are stateless JWTs so the
// server keeps no session table.

import type { FastifyReply, FastifyRequest } from 'fastify';
import { OAuth2Client } from 'google-auth-library';
import { SignJWT, jwtVerify } from 'jose';

import type { Me, Role } from '../../shared/types.js';
import { AttemptLimiter } from './accessKey.js';
import { config } from './config.js';
import { adminFingerprint, matchKey, viewerFingerprint } from './keys.js';

export const SESSION_COOKIE = 'tracker_session';
const secret = new TextEncoder().encode(config.sessionSecret);
const google = config.googleClientId ? new OAuth2Client(config.googleClientId) : null;
const limiter = new AttemptLimiter();

export const VIEWER: Me = { email: 'viewer', name: 'Shared access', picture: null, role: 'viewer' };
export const ADMIN: Me = { email: 'admin', name: 'Owner', picture: null, role: 'admin' };

export class AuthError extends Error {
  constructor(message: string, readonly status = 401) {
    super(message);
  }
}

export async function verifyGoogleCredential(credential: string): Promise<Me> {
  if (!google || !config.googleClientId) throw new AuthError('Google sign-in is not configured', 503);
  const ticket = await google.verifyIdToken({ idToken: credential, audience: config.googleClientId });
  const payload = ticket.getPayload();
  const email = payload?.email?.toLowerCase();
  if (!email || !payload?.email_verified) throw new AuthError('Google account has no verified e-mail');
  if (!config.allowedEmails.includes(email)) throw new AuthError(`${email} is not on the allow list`, 403);
  return { email, name: payload.name ?? null, picture: payload.picture ?? null, role: 'admin' };
}

// One field, two keys: the admin key (server secret) or the shared viewer
// key (database). Wrong guesses are limited per address.
export async function verifyAccessKey(key: string, address: string): Promise<Me> {
  if (limiter.check(address) === 'locked') throw new AuthError('Too many attempts, try again in 15 minutes', 429);
  const match = await matchKey(key);
  if (!match) {
    limiter.failure(address);
    throw new AuthError('Wrong access key');
  }
  limiter.success(address);
  return match === 'admin' ? ADMIN : VIEWER;
}

export async function issueSession(reply: FastifyReply, request: FastifyRequest, me: Me): Promise<void> {
  // Key sessions remember the generation of the key they were issued for.
  const kid = me.email === VIEWER.email ? await viewerFingerprint() : me.email === ADMIN.email ? adminFingerprint() : undefined;
  const token = await new SignJWT({ name: me.name, picture: me.picture, role: me.role, kid: kid ?? undefined })
    .setProtectedHeader({ alg: 'HS256' })
    .setSubject(me.email)
    .setIssuedAt()
    .setExpirationTime(`${config.sessionDays}d`)
    .sign(secret);
  reply.setCookie(SESSION_COOKIE, token, {
    path: '/',
    httpOnly: true,
    sameSite: 'lax',
    secure: request.protocol === 'https',
    maxAge: config.sessionDays * 86400,
  });
}

export function clearSession(reply: FastifyReply): void {
  reply.clearCookie(SESSION_COOKIE, { path: '/' });
}

export async function currentUser(request: FastifyRequest): Promise<Me | null> {
  if (config.authDisabled) return { email: 'dev@localhost', name: 'Developer', picture: null, role: 'admin' };
  const token = request.cookies[SESSION_COOKIE];
  if (!token) return null;
  try {
    const { payload } = await jwtVerify(token, secret);
    const email = payload.sub;
    if (!email) return null;
    if (email === VIEWER.email) {
      const current = await viewerFingerprint();
      return current && payload.kid === current ? VIEWER : null;
    }
    if (email === ADMIN.email) {
      const current = adminFingerprint();
      return current && payload.kid === current ? ADMIN : null;
    }
    if (!config.allowedEmails.includes(email)) return null;
    const role: Role = 'admin';
    return {
      email,
      name: typeof payload.name === 'string' ? payload.name : null,
      picture: typeof payload.picture === 'string' ? payload.picture : null,
      role,
    };
  } catch {
    return null;
  }
}
