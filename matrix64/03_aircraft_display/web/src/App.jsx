import { useEffect, useMemo, useState } from 'react';
import AppBar from '@mui/material/AppBar';
import Box from '@mui/material/Box';
import Brightness4Icon from '@mui/icons-material/Brightness4';
import Brightness7Icon from '@mui/icons-material/Brightness7';
import Chip from '@mui/material/Chip';
import Container from '@mui/material/Container';
import CssBaseline from '@mui/material/CssBaseline';
import DashboardIcon from '@mui/icons-material/Dashboard';
import IconButton from '@mui/material/IconButton';
import ImageIcon from '@mui/icons-material/Image';
import MemoryIcon from '@mui/icons-material/Memory';
import Stack from '@mui/material/Stack';
import Tab from '@mui/material/Tab';
import Tabs from '@mui/material/Tabs';
import TerminalIcon from '@mui/icons-material/Terminal';
import { ThemeProvider } from '@mui/material/styles';
import Toolbar from '@mui/material/Toolbar';
import Tooltip from '@mui/material/Tooltip';
import TuneIcon from '@mui/icons-material/Tune';
import Typography from '@mui/material/Typography';
import WifiIcon from '@mui/icons-material/Wifi';
import { getJson } from './api.js';
import ConfigForm from './components/ConfigForm.jsx';
import Dashboard from './components/Dashboard.jsx';
import Icons from './components/Icons.jsx';
import LogViewer from './components/LogViewer.jsx';
import Logo from './components/Logo.jsx';
import SystemTab from './components/SystemTab.jsx';
import WifiScan from './components/WifiScan.jsx';
import buildTheme from './theme.js';
import useColorMode from './useColorMode.js';

const STATUS_POLL_MS = 3000;

const TABS = [
  { label: 'Dashboard', icon: <DashboardIcon fontSize="small" />, Component: Dashboard, needsStatus: true },
  { label: 'Icons', icon: <ImageIcon fontSize="small" />, Component: Icons },
  { label: 'WiFi', icon: <WifiIcon fontSize="small" />, Component: WifiScan },
  { label: 'Config', icon: <TuneIcon fontSize="small" />, Component: ConfigForm, needsStatus: true },
  { label: 'System', icon: <MemoryIcon fontSize="small" />, Component: SystemTab, needsStatus: true },
  { label: 'Log', icon: <TerminalIcon fontSize="small" />, Component: LogViewer },
];

export default function App() {
  const [status, setStatus] = useState(null);
  const [tab, setTab] = useState(0);
  const [mode, toggleMode] = useColorMode();
  const theme = useMemo(() => buildTheme(mode), [mode]);

  useEffect(() => {
    let cancelled = false;
    const poll = () => {
      getJson('/api/status')
        .then((s) => {
          if (!cancelled) setStatus(s);
        })
        .catch(() => {});
    };
    poll();
    const id = setInterval(poll, STATUS_POLL_MS);
    return () => {
      cancelled = true;
      clearInterval(id);
    };
  }, []);

  useEffect(() => {
    if (status?.hostname) document.title = status.hostname;
  }, [status?.hostname]);

  const { Component, needsStatus } = TABS[tab];

  return (
    <ThemeProvider theme={theme}>
      <CssBaseline />
      <Box>
        <AppBar position="static" color="transparent" elevation={0} sx={{ borderBottom: '1px solid', borderColor: 'divider' }}>
          <Toolbar sx={{ gap: 1.5 }}>
            <Logo />
            <Typography variant="h6" sx={{ fontWeight: 700, letterSpacing: 0.5 }}>
              {status?.hostname || 'Mini Flightwall'}
            </Typography>
            <Box sx={{ flex: 1 }} />
            {status && (
              <Stack direction="row" spacing={1} alignItems="center">
                <Chip size="small" label={status.nowShowing} variant="outlined" />
                <Chip
                  size="small"
                  label={`${status.rssi} dBm`}
                  color={status.rssi > -70 ? 'success' : 'warning'}
                  variant="outlined"
                />
              </Stack>
            )}
            <Tooltip title={mode === 'dark' ? 'Switch to light theme' : 'Switch to dark theme'}>
              <IconButton size="small" onClick={toggleMode} color="inherit">
                {mode === 'dark' ? <Brightness7Icon fontSize="small" /> : <Brightness4Icon fontSize="small" />}
              </IconButton>
            </Tooltip>
          </Toolbar>
          <Tabs
            value={tab}
            onChange={(_e, v) => setTab(v)}
            variant="scrollable"
            scrollButtons="auto"
            sx={{ px: 2, minHeight: 44 }}
          >
            {TABS.map((t) => (
              <Tab
                key={t.label}
                label={t.label}
                icon={t.icon}
                iconPosition="start"
                sx={{ minHeight: 44, textTransform: 'none' }}
              />
            ))}
          </Tabs>
        </AppBar>

        <Container maxWidth="md" sx={{ py: 3 }}>
          <Component status={needsStatus ? status : undefined} />
        </Container>
      </Box>
    </ThemeProvider>
  );
}
