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
