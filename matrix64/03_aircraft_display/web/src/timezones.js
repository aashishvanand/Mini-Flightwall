// IANA zone names, straight from the browser's own tz database via
// Intl.supportedValuesOf('timeZone') -- no bundled country/offset dataset
// to keep in sync, and it's always as current as the browser itself
// (DST rules, newly split zones, etc.). Falls back to a short curated list
// for older browsers without Intl.supportedValuesOf (Safari < 17, notably).
const FALLBACK_ZONES = [
  'Pacific/Midway', 'Pacific/Honolulu', 'America/Anchorage', 'America/Los_Angeles',
  'America/Denver', 'America/Chicago', 'America/New_York', 'America/Sao_Paulo',
  'Atlantic/Azores', 'Europe/London', 'Europe/Paris', 'Europe/Athens',
  'Europe/Moscow', 'Asia/Dubai', 'Asia/Karachi', 'Asia/Kolkata',
  'Asia/Dhaka', 'Asia/Bangkok', 'Asia/Singapore', 'Asia/Shanghai',
  'Asia/Tokyo', 'Australia/Sydney', 'Pacific/Auckland',
];

export function listTimeZones() {
  if (typeof Intl.supportedValuesOf === 'function') {
    try {
      return Intl.supportedValuesOf('timeZone');
    } catch {
      // fall through
    }
  }
  return FALLBACK_ZONES;
}

// "+8:00" / "-5:30" -- the exact format 03_aircraft_display.ino's
// parseUtcOffset() expects (POSTed as the "tzOffset" config field).
export function offsetStringForZone(zone, date = new Date()) {
  try {
    const parts = new Intl.DateTimeFormat('en-US', { timeZone: zone, timeZoneName: 'longOffset' }).formatToParts(date);
    const tzName = parts.find((p) => p.type === 'timeZoneName')?.value; // e.g. "GMT+05:30" or "GMT"
    if (!tzName) return null;
    if (tzName === 'GMT') return '+0:00';
    const m = tzName.match(/^GMT([+-])(\d{1,2})(?::(\d{2}))?$/);
    if (!m) return null;
    const [, sign, h, min] = m;
    return `${sign}${parseInt(h, 10)}:${min || '00'}`;
  } catch {
    return null;
  }
}

// e.g. "Asia/Singapore" -> "Asia / Singapore (UTC+8:00)" -- readable and
// searchable by city/region without needing a separate country lookup.
export function labelForZone(zone, date = new Date()) {
  const offset = offsetStringForZone(zone, date);
  const pretty = zone.replace(/_/g, ' ').replace('/', ' / ');
  return offset ? `${pretty} (UTC${offset})` : pretty;
}
