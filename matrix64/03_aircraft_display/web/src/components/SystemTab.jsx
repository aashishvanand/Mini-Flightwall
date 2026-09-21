import Divider from '@mui/material/Divider';
import Stack from '@mui/material/Stack';
import Typography from '@mui/material/Typography';
import FirmwareUpdate from './FirmwareUpdate.jsx';
import RebootButton from './RebootButton.jsx';

export default function SystemTab({ status }) {
  return (
    <Stack spacing={3}>
      <FirmwareUpdate status={status} />
      <Divider />
      <Stack spacing={1} sx={{ alignItems: 'flex-start' }}>
        <Typography variant="body2" color="text.secondary">
          Restart the board (~10s downtime; config and icons are unaffected).
        </Typography>
        <RebootButton />
      </Stack>
    </Stack>
  );
}
