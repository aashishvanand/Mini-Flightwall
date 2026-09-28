# TODO

## Multi-panel-size support (matrix64/03_aircraft_display)

Currently hardcoded for the 64×64 2mm-pitch panel. Waveshare sells several
other sizes for the same driver board family (2.5mm/3mm/4mm/5mm pitch,
64×32, 96×48, 80×40, etc.) and none of them work today, for two reasons:

- `PANEL_RES_X`/`PANEL_RES_Y`/`PANEL_CHAIN` are just `#define`s, so the HUB75
  driver init itself *could* take other values -- but the entire layout in
  `drawAircraft()` (row Y-positions, `FULL_WIDTH_AVAIL`, `RIGHT_COL_AVAIL`,
  the 24px icon slot) is hand-tuned for exactly 64 columns and ~57 rows of
  vertical space. A 64×32 panel would just clip half the content, not
  reflow to fit.
- `mxconfig.gpio.e` (the row-address line) is only needed for ≥64-row
  panels; shorter panels typically shouldn't set it at all. So panel
  support isn't purely a resolution constant -- there's a driver-config
  difference per panel family too.

Settled approach: shared library + thin per-panel sketches, not a single
`#ifdef`-branched `.ino` and not N independent full copies.

- All the actual logic (icon sync, HTTP handling, scrolling, JSON payload
  parsing, drawing) moves into an Arduino library, e.g.
  `libraries/aircraft_display_core/` (`.h`/`.cpp`), exposing something like
  `aircraftDisplaySetup()` / `aircraftDisplayLoop()`.
- Each panel size gets its own sketch folder, e.g.
  `matrix64/03_aircraft_display_64x64/`, `_64x32/`, `_96x48/`, etc. Its
  `.ino` is just panel-specific `#define`s (`PANEL_RES_X/Y`, `gpio.e`,
  layout constants) plus calls into the shared library's setup()/loop().
- This avoids both failure modes: no N-way copy-paste drift on every future
  logic change (one shared source of truth), and no dead/inactive-panel
  code cluttering any single sketch (each folder only ever contains what
  that panel needs -- `#if` branches are preprocessor-eliminated per build
  anyway, but this reads cleaner in the repo).

Scope still needs deciding: support the exact panel list the user has in
hand, or the full current Waveshare lineup? Layout also needs a real
redesign for anything shorter than ~57 rows tall (64×32, 80×40) -- probably
a more compact single-line-per-field layout rather than trying to reflow
the existing one.

## Multi-aircraft playlist (n8n + matrix64/03_aircraft_display)

Today n8n picks ONE aircraft per 30s run -- `Nearest Aircraft` round-robins
through everything within `ROTATION_RADIUS_KM` (8km) via
`staticData.lastShownKey` -- and the firmware just shows whatever single
object `handleDisplay()` last received. Aircraft still get skipped:

- With N aircraft in the pool each one gets a 30s slot every N×30s; an
  aircraft crosses the 8km circle in ~2-4 min at approach speeds, so at 4+
  overhead some leave before their turn.
- Anything beyond 8km (but inside the ~16-23km `RADIUS_DEG` box) is ignored
  whenever the 8km pool is non-empty.
- Clocks and matrix share the same winner, so the matrix can't show more
  than the clocks do.

Settled approach: n8n sends every aircraft as an array, the firmware cycles
through them locally.

- **Firmware:** accept `{"aircraft": [ {...}, {...} ]}` (each entry the
  current single-aircraft payload, including `iconData`), hold up to ~6
  entries (~2KB each, fine in PSRAM), rotate every ~8-10s, replace the whole
  list on the next push. Keep accepting a bare single object for backward
  compatibility. The display stays dumb -- it plays whatever list it was
  last sent.
- **n8n:** add a Matrix64 branch that loops over the whole pool. The route
  and aircraft lookups (adsbdb route + aircraft, FR24 route match) currently
  run only for the winner, so this means ~N× adsbdb calls per run --
  check rate limits. `Lookup Icon` needs to query every aircraft's ICAO/IATA
  codes instead of one pair. The clocks can keep the current
  one-at-a-time rotation.
- Order: build + verify the n8n branch in executions first (old firmware
  would ignore an `aircraft` key, so push both shapes during the
  transition), then flash the firmware.

## Delay colours on the route row (n8n + matrix64/03_aircraft_display)

Color the route row's origin/destination codes by schedule status: green
when running early, red/amber when late, plain white when on time or
unknown. Shape stays the same, color only reinforces it -- same rule as the
climb/descend arrows.

Blocked on a data source. None of the current ones has schedule times:

- FR24's `feed.js` (what the workflow uses) has live position + route, no
  scheduled/estimated times.
- FR24's per-flight details endpoint (`data-live.flightradar24.com/clickhandler`)
  does have them, but returns 403 from its bot protection -- not an option.
- adsbdb and OpenSky have no schedule data at all.

So this needs a third-party flight-schedule/status API with an API key and a
request quota. Decide whether that's worth it before building anything:
compare providers' free tiers against ~1 lookup per new flight (cache per
callsign per day in workflow static data -- the schedule doesn't change
every 30s poll).

Proposed display, once a source exists:

- n8n computes `delayMin` (estimated or actual arrival minus scheduled
  arrival; departure delay if arrival isn't available yet) and adds it to
  the Matrix64 payload. Absent = unknown = white.
- Firmware colors both route codes: early by >5 min green, within +-15 min
  white (the usual A15 on-time definition), 15-45 min late amber, >45 min
  red. Thresholds are a starting guess -- tune once there's real data.

## Smaller ideas (from TheFlightWall_OSS / OpenFlightWall)

- **Bearing filter** (TheFlightWall_OSS): only show aircraft between a
  min/max compass bearing from HOME -- i.e. what's actually visible from the
  window the panel faces. Two new Config fields in n8n, filtered in
  `Nearest Aircraft` (bearing from HOME to the aircraft).
- **Cache adsbdb lookups** (OpenFlightWall caches route + aircraft per
  callsign/icao24 for 1h): today the same aircraft is re-queried every 30s
  run. Cache in workflow static data, keyed by callsign (route) and icao24
  (aircraft), with a TTL. Becomes important once the multi-aircraft
  playlist multiplies lookups.
- **ETA** (OpenFlightWall): remaining great-circle distance / ground speed
  -> "lands in ~1h 23m". Both inputs are already in n8n. No obvious room on
  the 64x64 layout -- candidate for the city row (alternating) or just the
  admin console.
- **Emergency squawk**: both feeds carry the squawk (OpenSky state vector
  index 14, FR24 feed index 6). Highlight 7700/7600/7500 (general
  emergency / radio failure / hijack) in red on the flight row. Rare, but
  cheap to add.
- **"Overhead now" cue** (OpenFlightWall alerts within ~3 km): e.g. a brief
  border flash on the panel when the shown aircraft is directly overhead.
