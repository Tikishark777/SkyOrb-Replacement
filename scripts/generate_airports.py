#!/usr/bin/env python3
"""Generate SkyOrb's offline airport catalogue from OurAirports (public domain).

Run on the GitHub Actions runner BEFORE `pio run`, not on the ESP32.
The complete dataset is not fetched at runtime by the device.

Source: https://github.com/davidmegginson/ourairports-data
"""
from __future__ import annotations
import csv
import io
import math
import os
import re
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "include" / "airport_catalog.h"
SOURCE = ("https://raw.githubusercontent.com/davidmegginson/"
          "ourairports-data/main/airports.csv")
CODE = re.compile(r"^[A-Z0-9]{3,4}$")


def read_data() -> list[dict[str, str]]:
    fixture = os.environ.get("SKYORB_AIRPORT_CSV")
    if fixture:
        print("[Airports] Using local CSV fixture", fixture)
        stream = open(fixture, "r", encoding="utf-8-sig", newline="")
        with stream:
            return list(csv.DictReader(stream))
    req = urllib.request.Request(
        SOURCE,
        headers={"User-Agent": "SkyOrb-community-firmware/3.3 (GitHub Actions)"},
    )
    print("[Airports] Fetching public-domain OurAirports CSV for offline firmware catalog")
    with urllib.request.urlopen(req, timeout=90) as response:
        raw = response.read()
    return list(csv.DictReader(io.StringIO(raw.decode("utf-8-sig"))))


def generate(rows: list[dict[str, str]]) -> str:
    airports: dict[str, tuple[str, str, int, int, int]] = {}
    for row in rows:
        kind = (row.get("type") or "").strip()
        scheduled = (row.get("scheduled_service") or "").lower() == "yes"
        # Include regional/GA medium airports (e.g. KOLM) and scheduled small
        # airports that are relevant to Alaska and remote communities.
        if kind not in ("large_airport", "medium_airport") and not (
            kind == "small_airport" and scheduled
        ):
            continue
        icao = (row.get("icao_code") or "").strip().upper()
        ident = (row.get("ident") or "").strip().upper()
        iata = (row.get("iata_code") or "").strip().upper()
        if not CODE.fullmatch(icao):
            icao = ident if CODE.fullmatch(ident) else ""
        if len(iata) != 3 or not iata.isalnum() or not iata.isascii():
            iata = ""
        if not icao and not iata:
            continue
        try:
            lat = float(row["latitude_deg"])
            lon = float(row["longitude_deg"])
        except (KeyError, ValueError, TypeError):
            continue
        if not math.isfinite(lat) or not math.isfinite(lon):
            continue
        if not (-90 <= lat <= 90 and -180 <= lon <= 180):
            continue
        # Stable deduplication; prefer records with scheduled traffic.
        key = icao or iata
        record = (icao, iata, round(lat * 100000), round(lon * 100000), int(scheduled))
        if key not in airports or (record[-1] and not airports[key][-1]):
            airports[key] = record
    items = sorted(airports.values(), key=lambda row: (row[0], row[1]))
    if len(items) < 10:
        raise RuntimeError(f"Airport data unexpectedly small: only {len(items)} records")
    header = [
        "// Generated automatically from OurAirports airport coordinates (public domain).",
        "// https://github.com/davidmegginson/ourairports-data",
        "// DO NOT EDIT. Generated in GitHub Actions before firmware compilation.",
        "#pragma once",
        "#include <stddef.h>",
        "#include <stdint.h>",
        "struct AirportRecord { char icao[5]; char iata[4]; int32_t latE5; int32_t lonE5; uint8_t scheduled; };",
        "static const AirportRecord AIRPORT_CATALOG[] = {",
    ]
    for icao, iata, lat, lon, scheduled in items:
        header.append(f'  {{"{icao}","{iata}",{lat},{lon},{scheduled}}},')
    header += [
        "};",
        "static constexpr size_t AIRPORT_CATALOG_COUNT = sizeof(AIRPORT_CATALOG) / sizeof(AIRPORT_CATALOG[0]);",
    ]
    print(f"[Airports] Catalogue contains {len(items)} medium/large/scheduled airports")
    return "\n".join(header) + "\n"


def main() -> None:
    content = generate(read_data())
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(content, encoding="utf-8")
    print("[Airports] Wrote", OUT, "bytes", len(content.encode('utf-8')))


if __name__ == "__main__":
    main()
