import { createTheme } from '@mui/material/styles';

// Accent matched to the previous plain-HTML admin page's #6cf in dark mode;
// a darker, more saturated blue in light mode for contrast against white.
export default function buildTheme(mode) {
  const isDark = mode === 'dark';
  return createTheme({
    palette: {
      mode,
      primary: { main: isDark ? '#6cf' : '#0369a1' },
      background: isDark ? { default: '#111', paper: '#1a1a1a' } : { default: '#f4f6f8', paper: '#ffffff' },
    },
    typography: {
      // No external font -- this page is served from the board itself over
      // the LAN, and the client's browser may have no internet access at all.
      fontFamily: 'ui-monospace, "SF Mono", "Cascadia Code", Consolas, monospace',
    },
    shape: { borderRadius: 8 },
  });
}
