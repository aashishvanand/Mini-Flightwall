import { useEffect, useState } from 'react';
import Alert from '@mui/material/Alert';
import Box from '@mui/material/Box';
import Brightness6Icon from '@mui/icons-material/Brightness6';
import ImageIcon from '@mui/icons-material/Image';
import MemoryIcon from '@mui/icons-material/Memory';
import SaveIcon from '@mui/icons-material/Save';
import Slider from '@mui/material/Slider';
import Stack from '@mui/material/Stack';
import StorageIcon from '@mui/icons-material/Storage';
import TimerIcon from '@mui/icons-material/Timer';
import Typography from '@mui/material/Typography';
import UpdateIcon from '@mui/icons-material/Update';
import WifiIcon from '@mui/icons-material/Wifi';
import { postConfig } from '../api.js';
import FooterBar from './FooterBar.jsx';
import Screen from './Screen.jsx';
import StatCard from './StatCard.jsx';

function fmtUptime(sec) {
  const h = Math.floor(sec / 3600);
  const m = Math.floor((sec % 3600) / 60);
  return h > 0 ? `${h}h ${m}m` : `${m}m`;
}

// dBm -> a rough 0-100 signal-strength bar. -50 or better is "full bars",
// -90 or worse is "none", same ballpark most OS WiFi icons use.
function rssiToPercent(rssi) {
  return Math.round(((Math.max(-90, Math.min(-50, rssi)) + 90) / 40) * 100);
}

export default function Dashboard({ status }) {
  const [brightness, setBrightness] = useState(null);
  const [saving, setSaving] = useState(false);

  // Hydrate the slider from live status once, then let local drags win --
  // same "don't stomp on the user" rule as ConfigForm's one-shot hydration.
  useEffect(() => {
    if (brightness === null && status) setBrightness(status.brightness);
  }, [status, brightness]);

  const commitBrightness = (_e, value) => {
    setSaving(true);
    postConfig({ brightness: value }).finally(() => setSaving(false));
  };

  if (!status) {
    return <Typography color="text.secondary">Loading status...</Typography>;
  }

  const lastPush =
    status.secondsSinceLastPush < 0 ? 'none yet this boot' : `${status.secondsSinceLastPush}s ago`;
  const stale = status.secondsSinceLastPush >= 300;

  return (
    <Stack spacing={2}>
      {status.usingDefaultAdminCreds && (
        <Alert severity="warning">
          Still using the default admin/changeme credentials -- anyone on this network can flash new
          firmware or reboot the board. Set ADMIN_USER/ADMIN_PASSWORD in secrets.h and reflash.
        </Alert>
      )}

      <Screen />

      <Box sx={{ display: 'flex', alignItems: 'center', gap: 2, px: 1 }}>
        <Brightness6Icon color="action" />
        <Slider
          value={brightness ?? 0}
          min={1}
          max={255}
          onChange={(_e, v) => setBrightness(v)}
          onChangeCommitted={commitBrightness}
          sx={{ flex: 1 }}
        />
        <Typography variant="body2" color="text.secondary" sx={{ width: 32, textAlign: 'right' }}>
          {brightness ?? '-'}
        </Typography>
        {saving && <SaveIcon fontSize="small" color="disabled" />}
      </Box>

      <Box
        sx={{
          display: 'grid',
          gridTemplateColumns: 'repeat(auto-fill, minmax(150px, 1fr))',
          gap: 2,
        }}
      >
        <StatCard
          icon={<WifiIcon fontSize="small" />}
          label="WiFi"
          value={`${status.rssi}`}
          unit="dBm"
          sub={status.wifiSsid}
          progress={rssiToPercent(status.rssi)}
        />
        <StatCard
          icon={<UpdateIcon fontSize="small" />}
          label="Last push"
          value={lastPush}
          sub={stale ? 'stale -- fell back to clock' : status.route || undefined}
          subColor={stale ? 'warning.main' : undefined}
        />
        <StatCard icon={<TimerIcon fontSize="small" />} label="Uptime" value={fmtUptime(status.uptimeSec)} />
        <StatCard icon={<MemoryIcon fontSize="small" />} label="Free heap" value={status.freeHeapKB} unit="KB" />
        <StatCard icon={<StorageIcon fontSize="small" />} label="Free PSRAM" value={status.freePsramKB} unit="KB" />
        <StatCard icon={<ImageIcon fontSize="small" />} label="Icons loaded" value={status.iconCount} />
      </Box>

      <FooterBar status={status} />
    </Stack>
  );
}
