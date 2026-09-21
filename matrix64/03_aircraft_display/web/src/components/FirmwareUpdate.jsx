import { useRef, useState } from 'react';
import Alert from '@mui/material/Alert';
import Button from '@mui/material/Button';
import Dialog from '@mui/material/Dialog';
import DialogActions from '@mui/material/DialogActions';
import DialogContent from '@mui/material/DialogContent';
import DialogTitle from '@mui/material/DialogTitle';
import Stack from '@mui/material/Stack';
import TextField from '@mui/material/TextField';
import Typography from '@mui/material/Typography';
import { postFile } from '../api.js';

// Same major.minor.patch shape as FIRMWARE_VERSION in the .ino -- checked
// client-side just so a typo'd version gets a clear message immediately
// instead of an opaque request; the firmware re-validates independently
// (see isVersionOlder() in handleOtaData()), since this field is only ever
// a convenience the admin fills in, not something the client can be trusted
// to enforce.
const VERSION_RE = /^\d+\.\d+\.\d+$/;

export default function FirmwareUpdate({ status }) {
  const fileInput = useRef(null);
  const [version, setVersion] = useState('');
  const [uploadStatus, setUploadStatus] = useState('');
  const [confirmOpen, setConfirmOpen] = useState(false);
  const [downgradeOpen, setDowngradeOpen] = useState(false);
  const [downgradeMessage, setDowngradeMessage] = useState('');

  const runUpload = (confirmDowngrade) => {
    const file = fileInput.current?.files?.[0];
    if (!file) return;
    setUploadStatus('flashing... (do not close this page or power off the board)');
    const fields = {};
    if (version) fields.version = version;
    if (confirmDowngrade) fields.confirmDowngrade = '1';
    postFile('/api/ota', 'firmware', file, fields)
      .then(async (r) => {
        const j = await r.json();
        if (r.status === 409 && j.downgrade) {
          setDowngradeMessage(j.error);
          setDowngradeOpen(true);
          setUploadStatus('');
          return;
        }
        setUploadStatus(j.ok ? 'flashed -- rebooting, this page will go dark briefly' : `error: ${j.error}`);
      })
      .catch(() => setUploadStatus('board rebooting (connection dropped, which is expected)'));
  };

  const flash = () => {
    setConfirmOpen(false);
    runUpload(false);
  };

  const flashAnywayDowngrade = () => {
    setDowngradeOpen(false);
    runUpload(true);
  };

  const requestFlash = () => {
    if (!fileInput.current?.files?.length) {
      setUploadStatus('choose a file first');
      return;
    }
    if (version && !VERSION_RE.test(version)) {
      setUploadStatus('version must look like 1.2.3 (or leave it blank to skip the downgrade check)');
      return;
    }
    setConfirmOpen(true);
  };

  return (
    <Stack spacing={2} sx={{ maxWidth: 560 }}>
      <Typography variant="body2" color="text.secondary">
        Upload a compiled .bin (Arduino IDE: Sketch &gt; Export Compiled Binary). Flashes and
        reboots automatically -- no confirmation step once you click Flash, so double-check the
        file first. <strong>Requires the admin credentials</strong> (your browser will prompt) --
        the one endpoint on this board gated beyond the usual LAN-only trust model, because the
        stakes here are higher than a wrong flight number on the panel. If the firmware fails to
        boot properly 3 times in a row, it automatically reverts to the previous working version.
      </Typography>
      {status?.firmwareVersion && (
        <Typography variant="caption" color="text.secondary">
          Board is currently running {status.firmwareVersion}.
        </Typography>
      )}
      <Stack direction="row" spacing={1} alignItems="center">
        <input ref={fileInput} type="file" accept=".bin" />
        <TextField
          label="Version (optional)"
          placeholder="1.2.0"
          size="small"
          value={version}
          onChange={(e) => setVersion(e.target.value)}
          sx={{ width: 140 }}
          helperText="matches FIRMWARE_VERSION in the .ino"
        />
        <Button variant="outlined" size="small" onClick={requestFlash}>
          Flash
        </Button>
      </Stack>
      {uploadStatus && <Typography variant="caption">{uploadStatus}</Typography>}

      <Dialog open={confirmOpen} onClose={() => setConfirmOpen(false)}>
        <DialogTitle>Flash and reboot?</DialogTitle>
        <DialogContent>
          <Typography variant="body2">This cannot be undone from here.</Typography>
        </DialogContent>
        <DialogActions>
          <Button onClick={() => setConfirmOpen(false)}>Cancel</Button>
          <Button color="warning" onClick={flash}>
            Flash
          </Button>
        </DialogActions>
      </Dialog>

      <Dialog open={downgradeOpen} onClose={() => setDowngradeOpen(false)}>
        <DialogTitle>This looks like a downgrade</DialogTitle>
        <DialogContent>
          <Alert severity="warning" sx={{ mb: 1 }}>
            {downgradeMessage}
          </Alert>
          <Typography variant="body2">
            Flashing an older version can reintroduce bugs (or security fixes) the current build
            already has. Only continue if that's actually what you want.
          </Typography>
        </DialogContent>
        <DialogActions>
          <Button onClick={() => setDowngradeOpen(false)}>Cancel</Button>
          <Button color="error" onClick={flashAnywayDowngrade}>
            Downgrade anyway
          </Button>
        </DialogActions>
      </Dialog>
    </Stack>
  );
}
