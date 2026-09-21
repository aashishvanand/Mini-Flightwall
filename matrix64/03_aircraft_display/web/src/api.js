// Thin fetch wrappers around the board's JSON API. Kept deliberately
// framework-free (no axios) -- there's nothing here fetch() can't do.

export async function getJson(path) {
  const res = await fetch(path);
  if (!res.ok) throw new Error(`${path}: ${res.status}`);
  return res.json();
}

export async function getText(path) {
  const res = await fetch(path);
  if (!res.ok) throw new Error(`${path}: ${res.status}`);
  return res.text();
}

export function postEmpty(path) {
  return fetch(path, { method: 'POST' });
}

// /api/config is read on the board with server.arg() against a plain
// application/x-www-form-urlencoded POST, and checkbox fields
// (tzOverrideEnabled, nightModeEnabled) are true only if the key is
// present at all -- so unchecked checkboxes must be left out of the body
// entirely, not sent as "false" (see handleConfig() in the firmware).
export function postConfig(fields) {
  const params = new URLSearchParams();
  for (const [key, value] of Object.entries(fields)) {
    if (value === false || value === undefined || value === null) continue;
    params.set(key, value === true ? 'on' : String(value));
  }
  return fetch('/api/config', {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
    body: params.toString(),
  });
}

// extraFields (if given) are appended to the FormData *before* the file
// field -- the firmware's multipart parser processes parts in request
// order, so any text fields (e.g. /api/ota's "version"/"confirmDowngrade")
// need to arrive first to be readable via server.arg() while the file part
// itself is still streaming in (see handleOtaData() in the firmware).
export function postFile(path, fieldName, file, extraFields) {
  const fd = new FormData();
  if (extraFields) {
    for (const [key, value] of Object.entries(extraFields)) fd.append(key, value);
  }
  fd.append(fieldName, file);
  return fetch(path, { method: 'POST', body: fd });
}
