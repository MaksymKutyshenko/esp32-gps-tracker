import { useCallback, useEffect, useState } from 'react';

import type { AppConfig, Me } from '../../shared/types';
import { getConfig, getMe, setUnauthorizedHandler, signOut } from './api';
import { Dashboard } from './components/Dashboard';
import { Login } from './components/Login';
import { useTheme } from './hooks';

type Session = { state: 'loading' } | { state: 'anonymous' } | { state: 'signed-in'; me: Me };

export function App() {
  const [config, setConfig] = useState<AppConfig | null>(null);
  const [session, setSession] = useState<Session>({ state: 'loading' });
  const [theme, toggleTheme] = useTheme();

  useEffect(() => {
    setUnauthorizedHandler(() => setSession({ state: 'anonymous' }));
    Promise.all([getConfig(), getMe().catch(() => null)])
      .then(([cfg, me]) => {
        setConfig(cfg);
        setSession(me ? { state: 'signed-in', me } : { state: 'anonymous' });
      })
      .catch(() => setSession({ state: 'anonymous' }));
  }, []);

  const handleSignOut = useCallback(async () => {
    await signOut().catch(() => undefined);
    setSession({ state: 'anonymous' });
  }, []);

  if (!config || session.state === 'loading') {
    return <div className="empty">Loading…</div>;
  }
  if (session.state === 'anonymous') {
    return <Login config={config} onSignedIn={(me) => setSession({ state: 'signed-in', me })} />;
  }
  return (
    <Dashboard me={session.me} config={config} theme={theme} onToggleTheme={toggleTheme} onSignOut={handleSignOut} />
  );
}
