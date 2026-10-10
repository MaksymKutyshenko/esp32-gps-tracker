// Google sign-in: the browser gets an ID token from Google Identity
// Services, posts it here, and gets a signed session cookie back. Only the
// e-mails in ALLOWED_EMAILS are let in. Sessions are stateless JWTs so the
// server keeps no session table.

import type { FastifyReply, FastifyRequest } from 'fastify';
import { OAuth2Client } from 'google-auth-library';
import { SignJWT, jwtVerify } from 'jose';

import type { Me } from '../../shared/types.js';
import { config } from './config.js';

export const SESSION_COOKIE = 'tracker_session';
const secret = new TextEncoder().encode(config.sessionSecret);
const google = config.googleClientId ? new OAuth2Client(config.googleClientId) : null;

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
  return { email, name: payload.name ?? null, picture: payload.picture ?? null };
}

export async function issueSession(reply: FastifyReply, request: FastifyRequest, me: Me): Promise<void> {
  const token = await new SignJWT({ name: me.name, picture: me.picture })
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
  if (config.authDisabled) return { email: 'dev@localhost', name: 'Developer', picture: null };
  const token = request.cookies[SESSION_COOKIE];
  if (!token) return null;
  try {
    const { payload } = await jwtVerify(token, secret);
    const email = payload.sub;
    if (!email || !config.allowedEmails.includes(email)) return null;
    return {
      email,
      name: typeof payload.name === 'string' ? payload.name : null,
      picture: typeof payload.picture === 'string' ? payload.picture : null,
    };
  } catch {
    return null;
  }
}
