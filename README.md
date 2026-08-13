# Living Wave Artwork

A physical artwork: an ESP32-S3 driving a 64×64 HUB75 LED matrix, rendering a top-down, continuously-alive sea-state
animation targeted at Mevagissey, Cornwall. Data updates every so often; the display never stops moving — a small
engine extrapolates and animates between readings so it always looks like a living sea, not a slideshow of stale
snapshots.

## Hardware

- diymore ESP32-S3 DevKitC-1 N16R8 (16MB flash / 8MB octal PSRAM)
- seengreat 64×64 RGB HUB75 LED matrix panel
- (later) Electrosmith Daisy Seed, for audio/sonification — deferred until the silent build is working

## Data sources

Two real sources, blended, rather than one buoy standing in for a location it isn't actually at:

- **[Copernicus Marine Service](https://data.marine.copernicus.eu/product/NWSHELF_ANALYSISFORECAST_WAV_004_014/description)**
  — a coupled hydrodynamic-wave model over the UK shelf at 1.5km resolution, already assimilating satellites/physics
  upstream. We read the single grid cell nearest Mevagissey (50.2705, -4.7827), including real **partitioned** wave
  data (separate wind-sea, primary swell, secondary swell — each with its own height/period/direction), not just a
  bulk number. This is what makes the piece actually targeted at Mevagissey rather than at wherever the nearest buoy
  happens to be. Refreshes once daily (hourly time-steps within that run) — spatially precise, not continuously live.
- **[Channel Coastal Observatory](https://coastalmonitoring.org/ccoresources/api/)**, buoy **Looe Bay** — the nearest
  active CCO wave buoy to Mevagissey (~27km away; a real gap in network coverage, not a mistake — verified against
  CCO's own live feed). Real ~30min-cadence buoy data corrects the *amplitude* of Copernicus's wave-train structure
  in real time, the way a local anchor buoy corrects a model in real data-assimilation pipelines, just sized for a
  hobby project. Significant wave heights combine as `sqrt(sum of squares)` across partitions, not linearly.

Both are fetched and blended by a small scheduled job (`cloud/fetch_and_blend.py`, see below), not by the ESP32
directly — the device only ever polls one small JSON payload.

**CCO field names are confirmed**, not guessed — pulled from a real live response this session (via the same endpoint
`coastalmonitoring.org`'s own public map uses): `hs`, `tp`, `pdir`, `spread`, `sst`, `sensor`. One real quirk: CCO
serializes numbers as JSON *strings* (`"hs":"0.670"`), which `data/cco_client.cpp` and `cloud/fetch_and_blend.py`
both account for. `cco_client` also needs the target sensor name (the feed returns every CCO site nationally in one
FeatureCollection) — confirmed working against live data for both Looe Bay and other sites.

**Copernicus's exact variable codes for partition periods/directions are best-effort**, not individually confirmed
(the bulk/height codes are; the convention-inferred ones are flagged in `cloud/fetch_and_blend.py` — verify against
a real response the first time this runs with credentials).

**Known outstanding issue**: the CCO API key currently returns `403 Referer does not match the key` even when sent
with the exact registered domain (`wavelength-artwork.local`) as a properly-formed Referer header — verified this
isn't a client-side formatting problem (tested via both a browser and Python `requests`, with a correctly-parsed
Referer confirmed in the error response itself). Worth checking the key's actual registered domain in CCO's system
before relying on it.

## Local weather

Not used for wave *physics* — Copernicus's wind-sea partition already correctly derives chop from wind within a
coupled model, so re-deriving it from raw wind data would be redundant and cruder. It *is* used for lighting: live
cloud cover from [Open-Meteo](https://open-meteo.com/) (free, no key, no signup — verified endpoint in
`cloud/fetch_and_blend.py`) dulls the specular glint and flattens brightness on overcast days, on top of the
sun-position lighting described below. Wind speed nudging fine noise/chop intensity was considered too but not
built — a real future option, not forgotten scope.

## Project phases

- **Phase A (current)** — hardware-agnostic engine + terminal preview + live cloud data. No hardware required.
  Everything in `core/` ships into the firmware unchanged later; only the transport and render targets change.
- **Phase B** — once hardware arrives: PlatformIO ESP32-S3 target, HUB75 render via
  [ESP32-HUB75-MatrixPanel-DMA](https://github.com/mrcodetastic/ESP32-HUB75-MatrixPanel-DMA), the device fetches the
  same published JSON payload `main_native --live-url` already consumes, pin mapping against the actual board.
- **Phase C (later)** — Daisy Seed audio, fed from the same `WaveEngine` state over a serial link.

## Layout

```
core/      hardware-agnostic wave engine (shared by preview today and firmware later)
           incl. sun_position.{h,cpp} (solar elevation/azimuth, pure math) and sky_state.{h,cpp} (per-tick lighting)
data/      mock feed, cco_client (raw CCO GeoJSON), live_feed_client (our own blended-JSON schema)
cloud/     fetch_and_blend.py -- scheduled job: Copernicus + Looe Bay -> one blended JSON payload
.github/   workflow that runs cloud/ on a schedule and publishes to a data branch
preview/   native desktop target: terminal ANSI true-colour renderer + main loop, incl. --live-url
vendor/    ArduinoJson.h, vendored single header (MIT), used by cco_client and live_feed_client
firmware/  empty until Phase B
```

## Building the preview

No installs required beyond Xcode Command Line Tools (`clang++`) and system `libcurl` (already present on macOS).

```
make
./preview_native
```

Widen your terminal to ~130 columns for a clean square-ish render. Runs against the mock buoy feed by default —
a short, illustrative (not verified-historical) sequence of readings on a compressed ~45s cadence, deliberately
swinging direction across the 350°→10° wrap and eventually running dry so you can watch the engine lose confidence
and drift. Ctrl+C to quit.

```
./preview_native --stats                             # headless: prints eased state + confidence each tick
./preview_native --interval 20                        # change the mock feed's cadence (seconds)
./preview_native --live-url <url> [--live-poll-interval 60]   # poll a published blended-JSON payload instead of the mock feed
./preview_native --sim-time 2026-08-13T05:30:00Z      # override the wall clock used for sun position (dawn/day/dusk/night, testable on demand)
./preview_native --cloud-cover 100                    # force cloud cover regardless of the feed, to test overcast dulling
```

`--live-url` is how the real Copernicus+Looe Bay blend gets seen and judged before any ESP32 hardware exists —
point it at the raw URL of whatever `cloud/fetch_and_blend.py` publishes.

## Cloud data layer

`cloud/fetch_and_blend.py` needs three secrets (GitHub Actions repo secrets when running via
`.github/workflows/fetch-wave-data.yml`; a gitignored `cloud/.env` for local testing): `CCO_API_KEY`,
`COPERNICUSMARINE_SERVICE_USERNAME`, `COPERNICUSMARINE_SERVICE_PASSWORD` (a free account at marine.copernicus.eu —
self-service, not something that can be created on your behalf). The workflow runs every 30 minutes and commits the
output JSON to a `data` branch; it's inert until this repo is actually pushed to GitHub.

## Engine design (the "living" part)

`WaveEngine` (`core/wave_engine.h`) holds a `prevState`/`targetState` pair. Each new reading starts a ~20s eased
transition (direction eased via shortest-arc circular interpolation, not linear, to avoid spinning the wrong way
across 0°/360°). Between readings, wave-component phases keep advancing continuously in real time regardless of when
the next update arrives — that continuous motion, not the update itself, is what makes it look alive. If updates
stop arriving, confidence decays and noise/desaturation grows — the piece reads as "losing certainty," not frozen.

**Wave components**: when a reading carries real partitions (Copernicus), `deriveComponents` builds components
directly from each real wave train (its own real direction/period/amplitude) rather than faking directionality —
`core/wave_partition.h`. When it doesn't (mock feed, bulk-only CCO fallback), it falls back to synthesizing a primary
plus jittered secondaries from one bulk reading's spread — the original Phase A approach, unchanged and still exact.

**Shading**: colour comes from fake Blinn-Phong lighting off the wave field's *analytical slope* (`core/palette.cpp`),
not a flat height-to-colour ramp — that's what makes it read as glinting water rather than a heightmap. Foam is
thresholded and steepness/noise-roughened, not a smooth gradient to white. A sparse fast-noise sparkle layer adds
sun-glitter, gated to only show on already-lit facets.

**Lighting** (`core/sun_position.{h,cpp}`, `core/sky_state.{h,cpp}`): the light direction isn't fixed — it's the
*real* sun's position for Mevagissey's coordinates right now (NOAA/Spencer solar position algorithm, pure math, no
dependency; verified against known equinox/solstice facts — solar-noon elevation and due-south azimuth, ~12h
equinox day length). Recomputed once per tick (not per pixel) as a small `SkyState`: light sweeps across the sky
through the day and fades out below the horizon, warmth peaks near dawn/dusk and cools toward both midday and deep
night, and — when live cloud cover is available — overcast skies dull the glint and flatten the brightness. All
four states (dawn/day/dusk/night) and both cloud extremes are independently testable today via `--sim-time` and
`--cloud-cover`, without waiting for real time or weather to cooperate.

**Wave shape**: each component's raw cosine is reshaped into a peaked-crest/broad-trough asymmetry (Gerstner-ish,
`sampleField` in `core/wave_engine.cpp`) rather than a symmetric sine — steeper components (`amplitude × wavenumber`)
get more of the effect, matching how real wave steepness governs trochoidal peaking. An approximation of true
Gerstner horizontal displacement, not the real thing (no closed-form per-pixel inverse exists for that) — but the
analytical gradient is exact for whatever shape is actually rendered, so lighting stays correct.

Wavelength on screen is an artistic rescale of the real deep-water dispersion relation (`λ ≈ 1.56·T²`), not a literal
metres-per-pixel mapping — a real 100m+ swell wavelength can't fit a 64px panel, so period maps onto a chosen
6–40px range instead.
