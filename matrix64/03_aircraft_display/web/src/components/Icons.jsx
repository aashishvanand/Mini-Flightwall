import { useEffect, useMemo, useRef, useState } from 'react';
import Box from '@mui/material/Box';
import Button from '@mui/material/Button';
import Dialog from '@mui/material/Dialog';
import DialogActions from '@mui/material/DialogActions';
import DialogContent from '@mui/material/DialogContent';
import DialogTitle from '@mui/material/DialogTitle';
import Pagination from '@mui/material/Pagination';
import Stack from '@mui/material/Stack';
import TextField from '@mui/material/TextField';
import Typography from '@mui/material/Typography';
import { getJson, postEmpty, postFile } from '../api.js';

// Each icon renders as an <img> that fetches /api/icon.bmp from the board
// itself -- with 300+ icons that's 300+ blocking HTTP requests queued
// against a single-threaded synchronous WebServer (see 03_aircraft_display.ino
// header notes), competing with the display's own periodic pushes and
// syncIcons(). Paginating what's actually mounted in the DOM keeps the
// in-flight request count bounded to one page's worth at a time.
const PAGE_SIZE = 40;

export default function Icons() {
  const [names, setNames] = useState([]);
  const [uploadStatus, setUploadStatus] = useState('');
  const [pendingDelete, setPendingDelete] = useState(null);
  const [filter, setFilter] = useState('');
  const [page, setPage] = useState(1);
  const fileInput = useRef(null);

  const loadIcons = () => {
    getJson('/api/icons')
      .then(setNames)
      .catch(() => {});
  };

  useEffect(loadIcons, []);

  const filtered = useMemo(() => {
    const f = filter.trim().toLowerCase();
    return f ? names.filter((n) => n.toLowerCase().includes(f)) : names;
  }, [names, filter]);

  const pageCount = Math.max(1, Math.ceil(filtered.length / PAGE_SIZE));
  const clampedPage = Math.min(page, pageCount);
  const pageNames = filtered.slice((clampedPage - 1) * PAGE_SIZE, clampedPage * PAGE_SIZE);

  // Filtering/reloading can shrink the result set below the current page
  // (or add/remove icons entirely) -- reset to page 1 rather than land on
  // a now out-of-range or silently-clamped page.
  useEffect(() => setPage(1), [filter, names.length]);

  const handleDelete = (name) => {
    postEmpty(`/api/icons/delete?name=${encodeURIComponent(name)}`).then(loadIcons);
    setPendingDelete(null);
  };

  const handleUpload = () => {
    const file = fileInput.current?.files?.[0];
    if (!file) {
      setUploadStatus('choose a file first');
      return;
    }
    setUploadStatus('uploading...');
    postFile('/api/icons/upload', 'icon', file)
      .then((r) => r.json())
      .then((j) => {
        setUploadStatus(j.ok ? 'done' : `error: ${j.error}`);
        if (j.ok) {
          fileInput.current.value = '';
          loadIcons();
        }
      })
      .catch(() => setUploadStatus('upload failed'));
  };

  return (
    <Stack spacing={2}>
      <Stack direction="row" justifyContent="space-between" alignItems="center" flexWrap="wrap" gap={1}>
        <Typography variant="body1">
          {filter ? `${filtered.length} of ${names.length}` : `${names.length} loaded`}
        </Typography>
        <Stack direction="row" spacing={1} alignItems="center">
          <TextField
            size="small"
            placeholder="Filter (e.g. sq, ai)"
            value={filter}
            onChange={(e) => setFilter(e.target.value)}
            sx={{ width: 180 }}
          />
          <Button size="small" onClick={loadIcons}>
            Refresh
          </Button>
        </Stack>
      </Stack>

      {pageCount > 1 && (
        <Pagination count={pageCount} page={clampedPage} onChange={(_, p) => setPage(p)} size="small" />
      )}

      <Box sx={{ display: 'flex', flexWrap: 'wrap', gap: 1 }}>
        {pageNames.map((n) => (
          <Box key={n} sx={{ textAlign: 'center', fontSize: '0.7em', color: 'text.secondary', width: 56 }}>
            <Box
              component="img"
              src={`/api/icon.bmp?name=${n}`}
              sx={{
                width: 48,
                height: 48,
                imageRendering: 'pixelated',
                border: '1px solid',
                borderColor: 'divider',
                borderRadius: 0.5,
                display: 'block',
              }}
            />
            <Typography variant="caption" noWrap sx={{ display: 'block' }}>
              {n}
            </Typography>
            <Button size="small" sx={{ fontSize: '0.65em', minWidth: 0, p: '1px 4px' }} onClick={() => setPendingDelete(n)}>
              delete
            </Button>
          </Box>
        ))}
      </Box>

      <Box sx={{ border: '1px solid', borderColor: 'divider', borderRadius: 2, p: 2 }}>
        <Typography variant="body2" color="text.secondary" sx={{ mb: 1 }}>
          Upload a 24x24 raw565 .bin (from tools/convert_tiles.py) -- shows on the panel immediately;
          merged onto the SD card on the next reboot.
        </Typography>
        <Stack direction="row" spacing={1} alignItems="center">
          <input ref={fileInput} type="file" accept=".bin" />
          <Button variant="outlined" size="small" onClick={handleUpload}>
            Upload
          </Button>
          <Typography variant="caption">{uploadStatus}</Typography>
        </Stack>
      </Box>

      <Dialog open={pendingDelete !== null} onClose={() => setPendingDelete(null)}>
        <DialogTitle>Delete {pendingDelete}?</DialogTitle>
        <DialogContent>
          <Typography variant="body2">Frees memory now; removed from the SD card on next reboot.</Typography>
        </DialogContent>
        <DialogActions>
          <Button onClick={() => setPendingDelete(null)}>Cancel</Button>
          <Button color="error" onClick={() => handleDelete(pendingDelete)}>
            Delete
          </Button>
        </DialogActions>
      </Dialog>
    </Stack>
  );
}
