import { useEffect, useState } from 'react';

const STORAGE_KEY = 'colorMode'; // 'light' | 'dark' -- absent means "follow system"

function systemMode() {
  return window.matchMedia?.('(prefers-color-scheme: dark)').matches ? 'dark' : 'light';
}

function readOverride() {
  try {
    return localStorage.getItem(STORAGE_KEY);
  } catch {
    return null;
  }
}

// Defaults to the browser/OS light-or-dark preference; a manual toggle
// overrides it and is remembered (per-browser) from then on.
export default function useColorMode() {
  const [override, setOverride] = useState(readOverride);
  const [system, setSystem] = useState(systemMode);

  useEffect(() => {
    const mql = window.matchMedia?.('(prefers-color-scheme: dark)');
    if (!mql) return;
    const onChange = (e) => setSystem(e.matches ? 'dark' : 'light');
    mql.addEventListener('change', onChange);
    return () => mql.removeEventListener('change', onChange);
  }, []);

  const mode = override || system;

  const toggle = () => {
    const next = mode === 'dark' ? 'light' : 'dark';
    setOverride(next);
    try {
      localStorage.setItem(STORAGE_KEY, next);
    } catch {
      // private browsing / storage blocked -- toggle still works for this load
    }
  };

  return [mode, toggle];
}
