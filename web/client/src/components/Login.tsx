import { useCallback, useEffect, useRef, useState } from 'react';
import type { FormEvent } from 'react';

import type { AppConfig, Me } from '../../../shared/types';
import { ApiError, signInDev, signInWithGoogle, signInWithKey } from '../api';

// Google Identity Services renders its own button; we only receive the ID
// token and hand it to the server for verification.
declare global {
  interface Window {
    google?: {
      accounts: {
        id: {
          initialize(options: { client_id: string; callback: (r: { credential: string }) => void; ux_mode?: string }): void;
          renderButton(parent: HTMLElement, options: Record<string, unknown>): void;
        };
      };
    };
  }
}

const GSI_SRC = 'https://accounts.google.com/gsi/client';

function loadGsi(): Promise<void> {
  if (window.google?.accounts) return Promise.resolve();
  return new Promise((resolve, reject) => {
    const existing = document.querySelector<HTMLScriptElement>(`script[src="${GSI_SRC}"]`);
    if (existing) {
      existing.addEventListener('load', () => resolve());
      existing.addEventListener('error', () => reject(new Error('Google sign-in script failed to load')));
      return;
    }
    const script = document.createElement('script');
    script.src = GSI_SRC;
    script.async = true;
    script.onload = () => resolve();
    script.onerror = () => reject(new Error('Google sign-in script failed to load'));
    document.head.appendChild(script);
  });
}

export function Login({ config, onSignedIn }: { config: AppConfig; onSignedIn: (me: Me) => void }) {
  const button = useRef<HTMLDivElement>(null);
  const [error, setError] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);

  useEffect(() => {
    if (!config.googleClientId || config.authDisabled) return;
    let cancelled = false;
    loadGsi()
      .then(() => {
        if (cancelled || !button.current || !window.google) return;
        window.google.accounts.id.initialize({
          client_id: config.googleClientId!,
          callback: async ({ credential }) => {
            setBusy(true);
            setError(null);
            try {
              onSignedIn(await signInWithGoogle(credential));
            } catch (err) {
              setError(err instanceof ApiError ? err.message : 'Sign-in failed');
            } finally {
              setBusy(false);
            }
          },
        });
        window.google.accounts.id.renderButton(button.current, {
          theme: 'outline',
          size: 'large',
          shape: 'pill',
          text: 'signin_with',
          width: 280,
        });
      })
      .catch((err: Error) => setError(err.message));
    return () => {
      cancelled = true;
    };
  }, [config.googleClientId, config.authDisabled, onSignedIn]);

  // Shared access key: typed in, or carried in a share link's fragment
  // (https://host/#key=...), which the browser never sends to the server.
  const [key, setKey] = useState('');
  const submitKey = useCallback(
    async (candidate: string) => {
      if (!candidate) return;
      setBusy(true);
      setError(null);
      try {
        onSignedIn(await signInWithKey(candidate));
      } catch (err) {
        setError(err instanceof ApiError ? err.message : 'Sign-in failed');
      } finally {
        setBusy(false);
      }
    },
    [onSignedIn],
  );
  useEffect(() => {
    if (!config.accessKeyEnabled) return;
    const fromHash = () => {
      const match = /[#&]key=([^&]+)/.exec(location.hash);
      if (!match) return;
      history.replaceState(null, '', location.pathname + location.search);
      void submitKey(decodeURIComponent(match[1]));
    };
    fromHash();
    window.addEventListener('hashchange', fromHash);
    return () => window.removeEventListener('hashchange', fromHash);
  }, [config.accessKeyEnabled, submitKey]);
  const onKeySubmit = (e: FormEvent) => {
    e.preventDefault();
    void submitKey(key.trim());
  };

  const devSignIn = async () => {
    setBusy(true);
    try {
      onSignedIn(await signInDev());
    } catch (err) {
      setError(err instanceof ApiError ? err.message : 'Sign-in failed');
    } finally {
      setBusy(false);
    }
  };

  return (
    <div className="login">
      <div className="card">
        <h1>GPS Tracker</h1>
        <p>Live position, track history and device health for the ESP32-S3 tracker.</p>
        {config.authDisabled ? (
          <button className="btn primary" onClick={devSignIn} disabled={busy}>
            Continue (development mode, no sign-in)
          </button>
        ) : (
          <>
            {config.accessKeyEnabled && (
              <form className="keyform" onSubmit={onKeySubmit}>
                <input
                  type="password"
                  autoComplete="current-password"
                  placeholder="Access key"
                  value={key}
                  onChange={(e) => setKey(e.target.value)}
                  disabled={busy}
                  autoFocus
                />
                <button className="btn primary" type="submit" disabled={busy || !key.trim()}>
                  Open
                </button>
              </form>
            )}
            {config.accessKeyEnabled && config.googleClientId && <p className="muted small">or</p>}
            {config.googleClientId && <div className="gsi" ref={button} />}
            {!config.accessKeyEnabled && !config.googleClientId && (
              <p className="error">The server has neither ACCESS_KEY nor GOOGLE_CLIENT_ID configured. See web/.env.example.</p>
            )}
          </>
        )}
        {busy && <p className="muted small">Signing in…</p>}
        {error && <p className="error">{error}</p>}
      </div>
    </div>
  );
}
