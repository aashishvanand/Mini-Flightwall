import { useEffect, useMemo, useRef, useState } from 'react';
import Autocomplete from '@mui/material/Autocomplete';
import Button from '@mui/material/Button';
import Checkbox from '@mui/material/Checkbox';
import Divider from '@mui/material/Divider';
import FormControlLabel from '@mui/material/FormControlLabel';
import Stack from '@mui/material/Stack';
import TextField from '@mui/material/TextField';
import Typography from '@mui/material/Typography';
import { postConfig, postEmpty } from '../api.js';
import { labelForZone, listTimeZones, offsetStringForZone } from '../timezones.js';

function fmtOffset(sec) {
  const sign = sec < 0 ? '-' : '+';
  const a = Math.abs(sec);
  const h = Math.floor(a / 3600);
  const m = Math.floor((a % 3600) / 60);
  return `${sign}${h}:${m < 10 ? '0' : ''}${m}`;
}

const EMPTY_FORM = {
  hostname: '',
  brightness: '',
  tzOverrideEnabled: false,
  tzZone: null, // client-side only, for the Autocomplete -- the board only ever stores/sends tzOffset
  tzOffset: '',
  nightModeEnabled: false,
  nightBrightness: '',
  nightStart: '',
  nightEnd: '',
  iconBaseUrl: '',
  ntpServer: '',
};

export default function ConfigForm({ status }) {
  const [form, setForm] = useState(EMPTY_FORM);
  const [saveStatus, setSaveStatus] = useState('');
  const [syncStatus, setSyncStatus] = useState('');
  const [syncing, setSyncing] = useState(false);
  // Pre-fill the form from /api/status only once -- a periodic status
  // refresh must never stomp on what the user is mid-typing. Mirrors the
  // `formHydrated` flag in the previous plain-JS admin page.
  const hydrated = useRef(false);

  useEffect(() => {
    if (hydrated.current || !status) return;
    hydrated.current = true;
    setForm({
      hostname: status.hostname,
      brightness: status.brightness,
      tzOverrideEnabled: status.tzOverrideEnabled,
      tzZone: null, // the board only stores a raw offset, not which zone it came from -- starts unselected
      tzOffset: fmtOffset(status.tzOffsetSec),
      nightModeEnabled: status.nightModeEnabled,
      nightBrightness: status.nightBrightness,
      nightStart: status.nightStartHour,
      nightEnd: status.nightEndHour,
      iconBaseUrl: status.iconBaseUrl,
      ntpServer: status.ntpServer,
    });
  }, [status]);

  const set = (key) => (e) => {
    const value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
    setForm((f) => ({ ...f, [key]: value }));
  };

  // Computed once (not per render) -- listTimeZones() calls
  // Intl.supportedValuesOf('timeZone'), a few hundred entries, and
  // labelForZone() formats each with its *current* UTC offset (DST-aware
  // at selection time), same as a calendar app's zone picker.
  const zones = useMemo(() => listTimeZones(), []);
  const zoneLabels = useMemo(() => {
    const m = new Map();
    zones.forEach((z) => m.set(z, labelForZone(z)));
    return m;
  }, [zones]);

  const save = (e) => {
    e.preventDefault();
    setSaveStatus('saving...');
    postConfig({
      hostname: form.hostname,
      brightness: form.brightness,
      tzOverrideEnabled: form.tzOverrideEnabled,
      tzOffset: form.tzOffset,
      nightModeEnabled: form.nightModeEnabled,
      nightBrightness: form.nightBrightness,
      nightStart: form.nightStart,
      nightEnd: form.nightEnd,
      iconBaseUrl: form.iconBaseUrl,
      ntpServer: form.ntpServer,
    })
      .then((r) => setSaveStatus(r.ok ? 'saved' : `error (${r.status})`))
      .catch(() => setSaveStatus('save failed'));
  };

  const syncNow = () => {
    setSyncing(true);
    setSyncStatus('saving URL...');
    // syncIcons() on the board reads the saved cfgIconBaseUrl, not
    // whatever's currently typed here -- save just this field first (other
    // fields are left untouched, see handleConfig()'s per-field hasArg()
    // guards) so "Sync now" always acts on what's in the box, unsaved or not.
    postConfig({ iconBaseUrl: form.iconBaseUrl })
      .then(() => {
        setSyncStatus('syncing... (panel scroll will pause briefly)');
        return postEmpty('/api/icons/sync');
      })
      .then((r) => r.json())
      .then((j) => setSyncStatus(j.ok ? `done -- ${j.iconCount} icons loaded` : `error: ${j.error}`))
      .catch(() => setSyncStatus('sync failed'))
      .finally(() => setSyncing(false));
  };

  return (
    <Stack component="form" spacing={3} onSubmit={save} sx={{ maxWidth: 480 }}>
      <TextField
        label="Hostname"
        size="small"
        helperText="reboot to apply"
        value={form.hostname}
        onChange={set('hostname')}
      />
      <TextField
        label="Brightness"
        type="number"
        size="small"
        inputProps={{ min: 1, max: 255 }}
        helperText="1-255, live (also adjustable from the Dashboard tab)"
        value={form.brightness}
        onChange={set('brightness')}
      />

      <Stack spacing={1}>
        <FormControlLabel
          control={<Checkbox checked={form.tzOverrideEnabled} onChange={set('tzOverrideEnabled')} />}
          label="TZ override enabled"
        />
        <Autocomplete
          size="small"
          options={zones}
          value={form.tzZone}
          disabled={!form.tzOverrideEnabled}
          getOptionLabel={(zone) => zoneLabels.get(zone) || zone}
          isOptionEqualToValue={(a, b) => a === b}
          onChange={(_, zone) => {
            setForm((f) => ({
              ...f,
              tzZone: zone,
              // Autofills the raw offset the board actually stores -- still
              // editable below for a custom/non-IANA offset.
              tzOffset: zone ? offsetStringForZone(zone) ?? f.tzOffset : f.tzOffset,
            }));
          }}
          renderInput={(params) => <TextField {...params} label="Timezone" placeholder="Search city or region..." />}
        />
        <TextField
          label="UTC offset (auto-filled from timezone, or set manually)"
          size="small"
          placeholder="+8:00"
          value={form.tzOffset}
          onChange={set('tzOffset')}
          disabled={!form.tzOverrideEnabled}
        />
      </Stack>

      <TextField
        label="NTP server"
        size="small"
        placeholder="pool.ntp.org"
        helperText="Hostname or bare IP, not a URL (e.g. pool.ntp.org or 192.168.1.1) -- time.nist.gov stays as a fixed fallback. Reboot to apply."
        value={form.ntpServer}
        onChange={set('ntpServer')}
      />

      <Stack spacing={1}>
        <FormControlLabel
          control={<Checkbox checked={form.nightModeEnabled} onChange={set('nightModeEnabled')} />}
          label="Night mode enabled"
        />
        <Stack direction="row" spacing={1} flexWrap="wrap">
          <TextField
            label="Dim to"
            type="number"
            size="small"
            inputProps={{ min: 1, max: 255 }}
            value={form.nightBrightness}
            onChange={set('nightBrightness')}
            disabled={!form.nightModeEnabled}
            sx={{ width: 100 }}
          />
          <TextField
            label="From (hour, local)"
            type="number"
            size="small"
            inputProps={{ min: 0, max: 23 }}
            value={form.nightStart}
            onChange={set('nightStart')}
            disabled={!form.nightModeEnabled}
            sx={{ width: 140 }}
          />
          <TextField
            label="To (hour, local)"
            type="number"
            size="small"
            inputProps={{ min: 0, max: 23 }}
            value={form.nightEnd}
            onChange={set('nightEnd')}
            disabled={!form.nightModeEnabled}
            sx={{ width: 140 }}
          />
        </Stack>
      </Stack>

      <Divider />

      <Stack spacing={1}>
        <TextField
          label="Icon sync base URL"
          size="small"
          placeholder="https://data.example.dev/24x24"
          helperText="No trailing slash. <url>/manifest.json and <url>/<name>.bin are fetched from here. Leave empty to disable remote icon sync."
          value={form.iconBaseUrl}
          onChange={set('iconBaseUrl')}
        />
        <Stack direction="row" spacing={2} alignItems="center">
          <Button variant="outlined" size="small" onClick={syncNow} disabled={syncing}>
            Sync now
          </Button>
          <Typography variant="caption">{syncStatus}</Typography>
        </Stack>
      </Stack>

      <Stack direction="row" spacing={2} alignItems="center">
        <Button type="submit" variant="contained">
          Save
        </Button>
        <Typography variant="caption">{saveStatus}</Typography>
      </Stack>
    </Stack>
  );
}
