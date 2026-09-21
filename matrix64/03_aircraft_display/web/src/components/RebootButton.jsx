import { useState } from 'react';
import Button from '@mui/material/Button';
import Dialog from '@mui/material/Dialog';
import DialogActions from '@mui/material/DialogActions';
import DialogTitle from '@mui/material/DialogTitle';
import { postEmpty } from '../api.js';

export default function RebootButton() {
  const [open, setOpen] = useState(false);

  const reboot = () => {
    setOpen(false);
    postEmpty('/api/reboot');
  };

  return (
    <>
      <Button variant="outlined" color="warning" onClick={() => setOpen(true)} sx={{ alignSelf: 'flex-start' }}>
        Reboot board
      </Button>
      <Dialog open={open} onClose={() => setOpen(false)}>
        <DialogTitle>Reboot now?</DialogTitle>
        <DialogActions>
          <Button onClick={() => setOpen(false)}>Cancel</Button>
          <Button color="warning" onClick={reboot}>
            Reboot
          </Button>
        </DialogActions>
      </Dialog>
    </>
  );
}
