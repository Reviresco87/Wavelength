# Living Wave Artwork

A physical artwork: an ESP32-S3 driving a 64×64 HUB75 LED matrix, rendering a top-down, continuously-alive sea-state
animation from real UK buoy data. The buoy updates every so often; the display never stops moving — a small engine
extrapolates and animates between readings so it always looks like a living sea, not a slideshow of stale snapshots.

## Hardware

- diymore ESP32-S3 DevKitC-1 N16R8 (16MB flash / 8MB octal PSRAM)
- seengreat 64×64 RGB HUB75 LED matrix panel
- (later) Electrosmith Daisy Seed, for audio/sonification — deferred until the silent build is working

## Data source

[Channel Coastal Observatory](https://coastalmonitoring.org/ccoresources/api/) API, buoy: **Looe Bay** (public viewer
`chart=98`) — the nearest active CCO wave buoy to Mevagissey, on the same south Cornwall/Channel coast. Reports
significant wave height, period, direction, and sea temperature from a Datawell Directional Waverider MkIII.

The exact API `sensor=` identifier and property field names in the GeoJSON response are **not yet verified** — CCO's
public docs don't spell out the response schema in enough detail to hardcode confidently. `data/cco_client` parses a
GeoJSON wave observation into a `BuoyReading` using the vendored [ArduinoJson](https://arduinojson.org) (v7.4.3,
`vendor/ArduinoJson.h`, works standalone on native and later on the ESP32 unchanged) — the GeoJSON *traversal* logic
is written and tested against synthetic payloads, but the property key names it looks for (`Hs`, `Tp`,
`MeanDirection`, `SpreadDirection`, `SeaTemperature`) are best-guesses, clearly flagged in `cco_client.h`. Requesting
a free API key via CCO's developer form is a manual step only you can do; once you have one, sanity-check those field
names against a real response before wiring `cco_client` into `main_native` (it isn't yet — the mock feed is still
the default).

## Project phases

- **Phase A (current)** — hardware-agnostic engine + terminal preview. No hardware required. Everything in `core/`
  ships into the firmware unchanged later; only the transport and render targets change.
- **Phase B** — once hardware arrives: PlatformIO ESP32-S3 target, HUB75 render via
  [ESP32-HUB75-MatrixPanel-DMA](https://github.com/mrcodetastic/ESP32-HUB75-MatrixPanel-DMA), real CCO fetch over
  WiFi/HTTPS, pin mapping against the actual board.
- **Phase C (later)** — Daisy Seed audio, fed from the same `WaveEngine` state over a serial link.

## Layout

```
core/      hardware-agnostic wave engine (shared by preview today and firmware later)
data/      buoy data sources: mock feed (default, used by preview) and cco_client (pending field-name verification)
preview/   native desktop target: terminal ANSI true-colour renderer + main loop
vendor/    ArduinoJson.h, vendored single header (MIT), used by cco_client
firmware/  empty until Phase B
```

## Building the preview

No installs required beyond Xcode Command Line Tools (`clang++`).

```
make
./preview_native
```

Widen your terminal to ~130 columns for a clean square-ish render. Runs against the mock buoy feed by default —
a short, illustrative (not verified-historical) sequence of Looe-Bay-plausible readings on a compressed ~45s
cadence, deliberately swinging direction across the 350°→10° wrap and eventually running dry so you can watch
the engine lose confidence and drift. Ctrl+C to quit.

```
./preview_native --stats                # headless: prints eased state + confidence each tick instead of rendering
./preview_native --interval 20          # change the mock feed's cadence (seconds)
```

## Engine design (the "living" part)

`WaveEngine` (`core/wave_engine.h`) holds a `prevState`/`targetState` pair. Each new reading starts a ~20s eased
transition (direction eased via shortest-arc circular interpolation, not linear, to avoid spinning the wrong way
across 0°/360°). Between readings, a small fixed set of directional sinusoidal wave components (one primary from
the reported height/period/direction, plus secondaries jittered across the reported directional spread) keep
advancing phase continuously in real time, layered with two octaves of Perlin noise for organic texture. If updates
stop arriving, confidence decays and noise/desaturation grows — the piece reads as "losing certainty," not frozen.

Wavelength on screen is an artistic rescale of the real deep-water dispersion relation (`λ ≈ 1.56·T²`), not a literal
metres-per-pixel mapping — a real 100m+ swell wavelength can't fit a 64px panel, so period maps onto a chosen
6–40px range instead.
