# SkyOrb Replacement — two-screen ESP32 flight radar + clock

**Preview source project (not yet compiled or hardware-tested).** This is a new,
independent implementation inspired by the SkyOrb enclosure / original wiring.
It uses the *same physical GPIO wiring*, so no rewiring should be needed on an
otherwise working SkyOrb unit. Original project: <https://github.com/GulfCoastMaker/SkyOrb>.

The original binary cannot simply be patched with a new API URL. This project
reimplements the application and **does not use Airplanes.live**.

## What is included

| Original MakerWorld function | Implementation in this source project |
|---|---|
| Left clock, right radar | Two GC9A01 240×240 screens, software-swappable roles |
| Live, nearby aircraft without an API key | ADSB.lol; on failed request, fallback to ADSB.fi open data |
| Moving radar sweep and aircraft glow | 11-second animated sweep, aircraft arrow shows course and proximity glow |
| Aircraft callsigns and altitudes | Up to 70 recent aircraft on radar; full list in phone portal |
| Airport markers | OSM Overpass auto-find for coded aerodromes; manual add and delete |
| Day, date, and time | Internet NTP time; Open-Meteo coordinate-based time-zone detection |
| Automatic daylight saving | Refreshes regional UTC offset every 6 hours (worst-case DST delay ~6h) |
| 12 / 24-hour option | Yes; saved in device |
| Setup from phone | Wi-Fi setup hotspot, phone web portal for location and preferences |
| Location by address | One-off OpenStreetMap Nominatim lookup, or manual latitude/longitude |
| 5–30 mile radius | Yes, in user settings |
| Accent color | Portal color selector; physical screens also use it |
| Tap flights for Flightradar24 | Aircraft on phone radar/list link to Flightradar24 flight/aircraft pages |
| Browser flashing | Included ESP Web Tools site + binary packaging script; **binary must be built first** |

**Important:** the FlightRadar24 links are *search/flight history pages*, not
guaranteed deep links to the exact live aircraft track; their site controls
whether a given flight can be identified. Free provider API access and
coverage are subject to change. No flight-tracking data should be used for
navigation or safety decisions.

## Original pinout (unchanged)

| GC9A01 connector | Radar/right (original radar circuit) | Clock/left (original clock circuit) |
|---|---|---|
| VCC / GND | 3.3 V / GND | 3.3 V / GND |
| SCL / CLK | GPIO 18 | GPIO 18 |
| SDA / MOSI | GPIO 23 | GPIO 23 |
| RES / RESET | GPIO 4 | GPIO 21 |
| DC | GPIO 2 | GPIO 17 |
| CS | GPIO 5 | GPIO 16 |

If the displays appear reversed in your enclosure, tick **Swap screens** in
the phone portal. No wiring changes are needed.

## Easiest way to build and flash with USB

You need a computer with **VS Code** and the **PlatformIO IDE** extension,
and a USB cable supporting data. The *original* SkyOrb installer is **not**
updated by downloading this package. The author must publish a compiled
replacement, or you can build this independent project.

1. Back up or write down the location and Wi-Fi settings from your current unit.
2. Extract this ZIP. Open the `SkyOrb-Replacement` directory in VS Code with
   the PlatformIO extension installed.
3. Connect your 30-pin ESP32 to the computer by a data-capable USB cable.
4. Click **Build** (PlatformIO checkmark). If it succeeds, click **Upload**
   (PlatformIO arrow), selecting the appropriate USB port if prompted.
5. The new firmware uses its own saved-settings namespace and will require
   setup again. Join `SKYORB-Setup` on your phone; the password is
   **SKYORB1234**. Open `http://192.168.4.1` and enter home Wi-Fi credentials.
6. Switch phone back to **home Wi-Fi**; open `http://skyorb.local` or the IP
   printed over USB Serial Monitor (`115200` baud) to select your location,
   radius, airport markers, clock format and accent color.
7. If the IP/hostname is unreachable, verify the ESP32 is on the same network
   (no client-isolation / guest Wi-Fi).

**If you prefer the exact browser installer workflow:** once the build succeeds,
run from the project directory:

```bash
python scripts/prepare_installer.py
python -m http.server 8000 --directory web-installer
```

Open **http://localhost:8000/** in Chrome or Edge on the *computer with the
ESP32 USB-connected*. The ESP Web Tools installer in `web-installer/` is now
usable because the four compiled `.bin` files are present. This does not
update the original author's Github Pages URL.

## Alternative: cloud build + hosted installer through your own GitHub repository

If you create your own GitHub repository and upload this project's files
(including the hidden `.github` directory), a built-in GitHub Actions workflow
compiles the firmware, packages four `.bin` files, and publishes a hosted
ESP Web Tools installer using GitHub Pages. Enable **Settings → Pages → Build
and deployment → Source: GitHub Actions** on *your* repository. On the Actions
tab, run **Build ESP32 firmware and web installer** if it hasn't run already.
Once successful, your installer is normally at
`https://YOUR-ACCOUNT.github.io/YOUR-REPOSITORY/`.
It can then be used with Chrome or Edge like the original install page.

A GitHub Actions build or deployment failure means there is not yet a usable
binary; do **not** try to flash a missing or incomplete image.

## Web portal and diagnostics

- `http://skyorb.local/`: settings, live radar, aircraft list, airports, Wi-Fi.
- `http://skyorb.local/api/config`: device settings (SSID name only, no password).
- `http://skyorb.local/api/live`: JSON of feed source, HTTP response code,
  aircraft count, update age, current aircraft and airport markers.
- USB serial monitor, **115200 baud**, logs flight-provider errors. `403`
  denotes denied access; `429` is rate limiting. If ADSB.lol fails, firmware
  tries the ADSB.fi v3 API.
- When changing the location, the firmware makes one airport lookup and one
  timezone lookup. You can request more airports from the portal.
- Location is mandatory to fetch planes. Plane updates are requested about
  every 30 seconds while the device has internet access.

## Usage limits and security considerations

- **Personal/non-commercial use only** unless you've arranged commercial data
  rights. Flight feed: ADSB.lol (<https://api.adsb.lol/docs>), backup: ADSB.fi
  (<https://github.com/adsbfi/opendata>). ADSB.lol currently documents free
  public usage but may require feeder keys later; ADSB.fi public endpoints
  have request limits, noncommercial terms, and attribution requirements.
- Airports: OpenStreetMap / ODbL via public Overpass. User-triggered address
  search: OSM Nominatim, caching duplicate searches for one hour and limiting
  to at most one request per second per device. Respect provider policies.
  If Nominatim stops working, enter your latitude/longitude manually.
- Time-zone data: Open-Meteo, free noncommercial API (CC BY 4.0). Name and
  effective UTC offset are refreshed at least every six hours. The offset may
  be temporarily stale during daylight-saving transitions.
- The ESP32's HTTPS requests use encrypted **but non-certificate-verified**
  TLS for compatibility with its limited CA storage. This is a prototype,
  not production-grade network security. Wi-Fi credentials are stored in the
  ESP32's NVS, and the local portal has **no password protection**. Only use
  on a home network you trust. The initial setup AP has a *well-known*
  password and should not be used where strangers can connect.
- No claims of guaranteed API uptime. If both aircraft providers deny access,
  the radar shows an error rather than fabricated flights. If all data
  disappear after operating normally, inspect `/api/live` first.
- The source has been checked for structural syntax, and the portal JavaScript
  was checked with Node.js; **it has not yet been compiled, flashed, or tested
  on a real ESP32 with displays**. Some adjustments may still be required.

## Source layout

- `src/main.cpp` — ESP32 firmware, networking, drawing, settings persistence.
- `include/web_ui.h` — complete embedded phone portal.
- `platformio.ini` — dependencies and target `esp32dev` board.
- `scripts/prepare_installer.py` — copies compiled binaries into installer.
- `.github/workflows/build.yml` — optional cloud build / Pages deployment.
- `web-installer/` — ESP Web Tools website and manifest; bins appear after build.

MIT License for newly created source. Adafruit and other libraries have their
own respective open source licenses.
