import Box from '@mui/material/Box';
import Typography from '@mui/material/Typography';

function fmtUptime(sec) {
  const h = Math.floor(sec / 3600);
  const m = Math.floor((sec % 3600) / 60);
  return h > 0 ? `${h}h ${m}m` : `${m}m`;
}

function Item({ label, value }) {
  return (
    <Typography variant="caption" sx={{ whiteSpace: 'nowrap' }}>
      <Box component="span" sx={{ color: 'text.secondary' }}>
        {label}{' '}
      </Box>
      <Box component="span" sx={{ fontWeight: 600 }}>
        {value}
      </Box>
    </Typography>
  );
}

export default function FooterBar({ status }) {
  if (!status) return null;

  const items = [
    ['Firmware', status.firmwareVersion],
    ['Hostname', status.hostname],
    ['IP', status.ip],
    ['MAC', status.mac],
    ['Uptime', fmtUptime(status.uptimeSec)],
    ['Free heap', `${status.freeHeapKB} KB`],
    ['Free PSRAM', `${status.freePsramKB} KB`],
  ];

  return (
    <Box sx={{ border: '1px solid', borderColor: 'divider', borderRadius: 2, p: 1.5, display: 'flex', flexWrap: 'wrap', gap: 2.5 }}>
      {items.map(([label, value]) => (
        <Item key={label} label={label} value={value} />
      ))}
    </Box>
  );
}
