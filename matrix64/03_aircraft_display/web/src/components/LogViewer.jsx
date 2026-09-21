import { useEffect, useRef } from 'react';
import Box from '@mui/material/Box';
import Chip from '@mui/material/Chip';
import Stack from '@mui/material/Stack';
import { getText } from '../api.js';

const LOG_POLL_MS = 5000;

export default function LogViewer() {
  const preRef = useRef(null);

  useEffect(() => {
    const refresh = () => {
      getText('/api/log')
        .then((text) => {
          const el = preRef.current;
          if (!el) return;
          const atBottom = el.scrollHeight - el.scrollTop - el.clientHeight < 20;
          el.textContent = text;
          if (atBottom) el.scrollTop = el.scrollHeight;
        })
        .catch(() => {});
    };
    refresh();
    const id = setInterval(refresh, LOG_POLL_MS);
    return () => clearInterval(id);
  }, []);

  return (
    <Stack spacing={1}>
      <Chip label="live" size="small" color="success" variant="outlined" sx={{ alignSelf: 'flex-start' }} />
      <Box
        ref={preRef}
        component="pre"
        sx={{
          bgcolor: '#0a0a0a',
          color: '#8f8',
          fontSize: '0.8em',
          minHeight: 320,
          maxHeight: '60vh',
          overflowY: 'auto',
          p: 1.5,
          borderRadius: 1,
          whiteSpace: 'pre-wrap',
          wordBreak: 'break-all',
          m: 0,
        }}
      >
        loading...
      </Box>
    </Stack>
  );
}
