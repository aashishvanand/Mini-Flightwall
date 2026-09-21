import { useState } from 'react';
import Box from '@mui/material/Box';
import Button from '@mui/material/Button';
import LockIcon from '@mui/icons-material/Lock';
import Stack from '@mui/material/Stack';
import Typography from '@mui/material/Typography';
import { getJson } from '../api.js';

export default function WifiScan() {
  const [nets, setNets] = useState(null);
  const [scanning, setScanning] = useState(false);

  const scan = () => {
    setScanning(true);
    getJson('/api/wifi/scan')
      .then((list) => {
        list.sort((a, b) => b.rssi - a.rssi);
        setNets(list);
      })
      .finally(() => setScanning(false));
  };

  return (
    <Stack spacing={2}>
      <Button variant="outlined" size="small" onClick={scan} disabled={scanning} sx={{ alignSelf: 'flex-start' }}>
        {scanning ? 'Scanning (~2s, panel scroll will pause)...' : 'Scan nearby networks'}
      </Button>
      {nets && (
        <Stack sx={{ border: '1px solid', borderColor: 'divider', borderRadius: 2, px: 2 }}>
          {nets.length === 0 && (
            <Typography variant="body2" color="text.secondary" sx={{ py: 1 }}>
              none found
            </Typography>
          )}
          {nets.map((n, i) => (
            <Box
              key={n.ssid}
              sx={{
                display: 'flex',
                justifyContent: 'space-between',
                alignItems: 'center',
                py: 1,
                borderBottom: i < nets.length - 1 ? '1px solid' : 'none',
                borderColor: 'divider',
              }}
            >
              <Stack direction="row" spacing={0.75} alignItems="center">
                {n.secure && <LockIcon sx={{ fontSize: 16 }} color="action" />}
                <Typography variant="body2">{n.ssid}</Typography>
              </Stack>
              <Typography variant="body2" color="text.secondary">
                {n.rssi} dBm
              </Typography>
            </Box>
          ))}
        </Stack>
      )}
    </Stack>
  );
}
