#!/usr/bin/env python3
"""Fetches Copernicus's NW Shelf wave model (Mevagissey's grid cell -- real
partitioned wind-sea/swell data), Looe Bay's live CCO buoy reading, and
Open-Meteo's live cloud cover, reconciles them, and writes a small JSON
payload matching the extended BuoyReading shape from core/buoy_reading.h +
core/wave_partition.h.

Credentials come from environment variables only, never from this file:
  COPERNICUSMARINE_SERVICE_USERNAME / COPERNICUSMARINE_SERVICE_PASSWORD
    (copernicusmarine reads these itself)
  CCO_API_KEY

For local testing: put those three in a gitignored cloud/.env, then
`set -a && source cloud/.env && set +a` before running this script.
"""

import json
import os
import sys
from datetime import datetime, timezone

import requests

MEVAGISSEY_LAT = 50.2705
MEVAGISSEY_LON = -4.7827
LOOE_BAY_SENSOR_NAME = "Looe Bay"

# The hourly-instantaneous, 1.5km NWSHELF_ANALYSISFORECAST_WAV_004_014
# dataset -- confirmed both from the product's own dataset listing and
# a real live fetch against Mevagissey's coordinates this session.
COPERNICUS_DATASET_ID = "cmems_mod_nws_wav_anfc_1.5km_PT1H-i"

# All confirmed against a real live fetch this session (real account,
# real Mevagissey coordinates) -- not just the documented convention.
COPERNICUS_VARIABLES = [
    "VHM0", "VTPK", "VMDR",  # bulk: height, peak period, mean direction
    "VHM0_WW", "VTM01_WW", "VMDR_WW",  # wind sea
    "VHM0_SW1", "VTM01_SW1", "VMDR_SW1",  # primary swell
    "VHM0_SW2", "VTM01_SW2", "VMDR_SW2",  # secondary swell
]

# Partitions with less than this much energy are treated as absent rather
# than as a real (but tiny) wave train -- avoids feeding the engine
# near-zero "real" components that are really just model noise.
PARTITION_HS_FLOOR_METRES = 0.05


def fetch_copernicus_point():
    """Returns a dict of bulk + partition values, or None on any failure --
    callers fall back to bulk-only (CCO) data, same as the engine's own
    bulk-only fallback path, rather than failing the whole job over one
    source being unavailable."""
    try:
        import copernicusmarine  # lazy: keeps this module importable/testable without the heavy dependency installed
        ds = copernicusmarine.open_dataset(dataset_id=COPERNICUS_DATASET_ID, variables=COPERNICUS_VARIABLES)
        point = ds.sel(longitude=MEVAGISSEY_LON, latitude=MEVAGISSEY_LAT, method="nearest")
        # The dataset's time coordinate is naive (datetime64[ns], no tz) --
        # confirmed by a real fetch this session throwing "Cannot compare
        # dtypes datetime64[ns] and datetime64[us, UTC]" against an
        # aware datetime. Values are already UTC, so strip tzinfo rather
        # than convert -- an aware-UTC-now with tzinfo dropped, not a
        # local-time now().
        now_naive_utc = datetime.now(timezone.utc).replace(tzinfo=None)
        at_time = point.sel(time=now_naive_utc, method="nearest")

        def val(name):
            v = float(at_time[name].values)
            return v if v == v else None  # filter NaN without a math import just for this

        return {
            "hs": val("VHM0"), "tp": val("VTPK"), "dir": val("VMDR"),
            "wind_sea": {"hs": val("VHM0_WW"), "tp": val("VTM01_WW"), "dir": val("VMDR_WW")},
            "primary_swell": {"hs": val("VHM0_SW1"), "tp": val("VTM01_SW1"), "dir": val("VMDR_SW1")},
            "secondary_swell": {"hs": val("VHM0_SW2"), "tp": val("VTM01_SW2"), "dir": val("VMDR_SW2")},
        }
    except Exception as exc:  # noqa: BLE001 -- deliberately broad: any failure here should degrade, not crash the job
        print(f"Copernicus fetch failed, continuing with CCO-only bulk data: {exc}", file=sys.stderr)
        return None


def fetch_looe_bay():
    """Returns {hs, tp, dir, spread, sst} or None on any failure (including
    the site having no data this cycle) -- callers fall back to Copernicus
    bulk-only data, symmetric with fetch_copernicus_point()'s own fallback,
    so one source being down doesn't take out the whole job. Field names and
    the JSON-strings-not-numbers quirk are confirmed against a real live
    response (see data/cco_client.cpp). Referer must be a proper URL (not a
    bare hostname) -- confirmed this session that CCO's Referer check parses
    it as a URL and reports back whatever hostname it extracted.
    """
    try:
        resp = requests.get(
            "https://coastalmonitoring.org/observations/waves/latest.geojson",
            # CCO's documented single-location filter (coastalmonitoring.org/ccoresources/api/)
            # -- server-side filtering instead of pulling all ~38 national sites every time.
            params={"key": os.environ["CCO_API_KEY"], "sensor": LOOE_BAY_SENSOR_NAME},
            headers={"Referer": "https://wavelength-artwork.local/"},
            timeout=20,
        )
        resp.raise_for_status()
        data = resp.json()
    except Exception as exc:  # noqa: BLE001 -- deliberately broad: degrade, don't crash the job
        print(f"Looe Bay fetch failed, continuing with Copernicus-only data: {exc}", file=sys.stderr)
        return None

    def as_float(raw):
        if raw is None:
            return None
        try:
            return float(raw)  # CCO serializes numbers as JSON strings
        except (TypeError, ValueError):
            return None

    for feature in data.get("features", []):
        props = feature.get("properties", {})
        if props.get("sensor") != LOOE_BAY_SENSOR_NAME:
            continue
        hs, tp, direction = as_float(props.get("hs")), as_float(props.get("tp")), as_float(props.get("pdir"))
        if hs is None or tp is None or direction is None:
            return None  # Looe Bay reported no data this cycle
        return {"hs": hs, "tp": tp, "dir": direction, "spread": as_float(props.get("spread")), "sst": as_float(props.get("sst"))}
    return None  # sensor not present in this response


def fetch_cloud_cover():
    """Returns cloud cover percent (0-100) or None on any failure. Open-Meteo
    is free, keyless, no signup -- verified this session against the real
    endpoint: current.cloud_cover is a plain 0-100 number, current.time is
    ISO8601, update interval ~900s (well inside our 30min cadence)."""
    try:
        resp = requests.get(
            "https://api.open-meteo.com/v1/forecast",
            params={
                "latitude": MEVAGISSEY_LAT,
                "longitude": MEVAGISSEY_LON,
                "current": "cloud_cover",
                "timezone": "UTC",
            },
            timeout=15,
        )
        resp.raise_for_status()
        return float(resp.json()["current"]["cloud_cover"])
    except Exception as exc:  # noqa: BLE001 -- deliberately broad: degrade, don't crash the job
        print(f"Open-Meteo cloud cover fetch failed, continuing without it: {exc}", file=sys.stderr)
        return None


def build_partition(hs, tp, direction):
    if hs is None or tp is None or direction is None or hs < PARTITION_HS_FLOOR_METRES:
        return {"present": False}
    return {"hsMetres": hs, "tpSeconds": tp, "dirDeg": direction, "present": True}


def reconcile(copernicus, live_hs):
    """Keeps Copernicus's directional/period structure for each partition,
    but rescales their total energy so it matches the live buoy's Hs -- the
    model supplies geometry (correct for Mevagissey's location), the live
    buoy corrects amplitude in real time. Significant wave heights combine
    as sqrt(sum of squares), not linearly (Hs is proportional to sqrt
    spectral energy, and independent wave systems' energy adds)."""
    if copernicus is None:
        return {name: {"present": False} for name in ("windSea", "primarySwell", "secondarySwell")}

    raw = {
        "windSea": copernicus["wind_sea"],
        "primarySwell": copernicus["primary_swell"],
        "secondarySwell": copernicus["secondary_swell"],
    }
    combined = sum((v["hs"] or 0.0) ** 2 for v in raw.values()) ** 0.5
    scale = (live_hs / combined) if (combined > 0.0 and live_hs is not None) else 1.0

    return {
        name: build_partition((v["hs"] or 0.0) * scale, v["tp"], v["dir"])
        for name, v in raw.items()
    }


def main():
    looe_bay = fetch_looe_bay()
    copernicus = fetch_copernicus_point()
    cloud_cover = fetch_cloud_cover()

    if looe_bay is None and copernicus is None:
        print("Both Copernicus and Looe Bay fetches failed -- nothing to publish this cycle.", file=sys.stderr)
        sys.exit(1)

    live_hs = looe_bay["hs"] if looe_bay else (copernicus["hs"] if copernicus else None)
    partitions = reconcile(copernicus, live_hs)

    fallback_hs = (copernicus or {}).get("hs") or 0.0
    fallback_tp = (copernicus or {}).get("tp") or 0.0
    fallback_dir = (copernicus or {}).get("dir") or 0.0

    payload = {
        "hsMetres": live_hs if live_hs is not None else fallback_hs,
        "tpSeconds": (looe_bay or {}).get("tp") or fallback_tp,
        "mwdDeg": (looe_bay or {}).get("dir") or fallback_dir,
        "spreadDeg": (looe_bay or {}).get("spread") or 20.0,
        "seaTempC": (looe_bay or {}).get("sst") or 0.0,
        "windSea": partitions["windSea"],
        "primarySwell": partitions["primarySwell"],
        "secondarySwell": partitions["secondarySwell"],
        "cloudCoverPercent": cloud_cover if cloud_cover is not None else 0.0,
        "cloudCoverPresent": cloud_cover is not None,
        "valid": True,
        "generatedAtUnix": int(datetime.now(timezone.utc).timestamp()),
        "sources": {
            "looeBayOk": looe_bay is not None,
            "copernicusOk": copernicus is not None,
            "cloudCoverOk": cloud_cover is not None,
        },
    }

    out_path = os.environ.get("OUTPUT_PATH", "latest.json")
    with open(out_path, "w") as f:
        json.dump(payload, f, indent=2)

    print(
        f"Wrote {out_path}: Hs={payload['hsMetres']:.2f}m Tp={payload['tpSeconds']:.2f}s "
        f"MWD={payload['mwdDeg']:.0f} looeBayOk={looe_bay is not None} copernicusOk={copernicus is not None} "
        f"cloudCoverOk={cloud_cover is not None}"
    )


if __name__ == "__main__":
    main()
