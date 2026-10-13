import { useEffect, useState } from 'react';

// A clock that ticks once a second, for "12 s ago" labels.
export function useNow(intervalMs = 1000): number {
  const [now, setNow] = useState(() => Date.now());
  useEffect(() => {
    const id = setInterval(() => setNow(Date.now()), intervalMs);
    return () => clearInterval(id);
  }, [intervalMs]);
  return now;
}

export type Theme = 'light' | 'dark';

function systemTheme(): Theme {
  return window.matchMedia('(prefers-color-scheme: dark)').matches ? 'dark' : 'light';
}

function storedTheme(): Theme | null {
  try {
    const t = localStorage.getItem('theme');
    return t === 'light' || t === 'dark' ? t : null;
  } catch {
    return null;
  }
}

// The effective theme (stored choice, else the OS), and a toggle.
export function useTheme(): [Theme, () => void] {
  const [theme, setTheme] = useState<Theme>(() => storedTheme() ?? systemTheme());

  useEffect(() => {
    const media = window.matchMedia('(prefers-color-scheme: dark)');
    const onChange = () => {
      if (!storedTheme()) setTheme(systemTheme());
    };
    media.addEventListener('change', onChange);
    return () => media.removeEventListener('change', onChange);
  }, []);

  useEffect(() => {
    document.documentElement.dataset.theme = theme;
  }, [theme]);

  const toggle = () => {
    const next: Theme = theme === 'dark' ? 'light' : 'dark';
    try {
      localStorage.setItem('theme', next);
    } catch {
      /* private mode */
    }
    setTheme(next);
  };
  return [theme, toggle];
}

// Reads the chart colour tokens from CSS so charts follow the theme.
export interface Tokens {
  series: string[];
  text: string;
  text2: string;
  grid: string;
  surface: string;
  ok: string;
  warn: string;
  err: string;
}

export function useTokens(theme: Theme): Tokens {
  const [tokens, setTokens] = useState<Tokens>(() => readTokens());
  useEffect(() => {
    // After the data-theme attribute has been applied.
    const id = requestAnimationFrame(() => setTokens(readTokens()));
    return () => cancelAnimationFrame(id);
  }, [theme]);
  return tokens;
}

function readTokens(): Tokens {
  const style = getComputedStyle(document.documentElement);
  const v = (name: string) => style.getPropertyValue(name).trim();
  return {
    series: [v('--series-1'), v('--series-2'), v('--series-3')],
    text: v('--text'),
    text2: v('--text-2'),
    grid: v('--grid'),
    surface: v('--surface'),
    ok: v('--ok'),
    warn: v('--warn'),
    err: v('--err'),
  };
}
