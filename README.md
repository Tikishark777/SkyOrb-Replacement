# SkyOrb Community Edition — ESP32 Flight Radar + Clock

**Community remake by John Christensen ([@Tikishark777](https://github.com/Tikishark777)).** Based on the original [SkyOrb by GulfCoastMaker](https://gulfcoastmaker.github.io/SkyOrb/). This version preserves the original dual-screen wiring, restores aircraft tracking using alternative ADS-B data feeds, and adds a browser-based installer plus a phone configuration dashboard.

### [Install SkyOrb Community Edition and read the illustrated build guide](https://tikishark777.github.io/SkyOrb-Replacement/)

> **Project status:** The firmware has compiled and been installed and tested on an actual dual-display SkyOrb, with working Wi-Fi setup, address search, radius controls, live aircraft, airport discovery, selectable colors, Wi-Fi reset, and 12/24-hour clock options. The radar sweep works but may exhibit a faint trail/ghosting; this remake does not yet exactly reproduce the original animation. It is hobby firmware, not aviation safety equipment.

## What it does

- **Live nearby aircraft** using ADSB.lol with ADSB.fi as a fallback (availability subject to providers).
- **Dual round displays:** green radar on the right and a white/amber time-and-date clock on the left.
- **Radar range:** choose 5, 10, 20, or 30 miles.
- **Nearby airports:** build-generated, offline airport catalogue from [OurAirports](https://ourairports.com/data/) plus manual markers.
- **Location search:** enter an address or use precise latitude/longitude.
- **Automatic time zone**, 12/24-hour clock, dashboard colors, network reset and local settings page.
- **No-code browser installation:** prebuilt firmware flashed from desktop Chrome or Edge using [ESP Web Tools](https://esphome.github.io/esp-web-tools/).

## Parts list

| Part | Quantity | Notes |
| --- | :---: | --- |
| ESP-WROOM-32 dev board | 1 | Original 30-pin ESP32 board |
| GC9A01 TFT display, 1.28 in, 240×240 SPI, 7 pin | 2 | One radar and one clock |
| 30-pin ESP32 GPIO screw-terminal breakout | 1 optional | Simplifies wiring without soldering |
| Hookup wires | As needed | Connect both displays to the ESP32 |
| USB **data** cable | 1 | Needed for browser flashing; USB power for normal use |
| Printed enclosure | optional | [Original SkyOrb MakerWorld model](https://makerworld.com/en/models/2959706-aviation-desk-clock-project-skyorb#profileId-3317442) |

## Wiring — original SkyOrb pinout (unchanged)

Both displays share **SCL/CLK → GPIO18** and **SDA/MOSI → GPIO23**. The other control pins are independent.

| GC9A01 display pin | Radar screen, **right** | Clock screen, **left** |
| --- | --- | --- |
| GND | GND | GND |
| VCC | **3.3V** | **3.3V** |
| SCL / CLK | GPIO 18 | GPIO 18 |
| SDA / MOSI | GPIO 23 | GPIO 23 |
| RES / RST | GPIO 4 | GPIO 21 |
| DC | GPIO 2 | GPIO 17 (TX2) |
| CS | GPIO 5 | GPIO 16 (RX2) |

**Important:** Supply 3.3V to these displays, not 5V. Verify the actual pin labels on the modules you purchase. The displays described here are the original SkyOrb-style **7-pin GC9A01** units. The clock/radar roles can be swapped in device settings if necessary, without physically moving the wiring.

## Install the firmware from your browser

1. Assemble the hardware using the table above.
2. Open the [SkyOrb Community Edition installer](https://tikishark777.github.io/SkyOrb-Replacement/) using **Chrome or Edge on a desktop/laptop**.
3. Connect the ESP32 to the computer using a reliable USB **data** cable (a charging-only cable does not work).
4. Click **Install SkyOrb Community Edition**, choose the ESP32's COM/serial port and follow the prompts.
5. Keep it connected until installation is complete. Flashing overwrites existing firmware and may erase Wi-Fi and location settings.

**Troubleshooting flashing:** If a USB write fails, retry with a known-good cable and a direct USB port. If the failure repeats at the same point, stop and investigate rather than erasing the device repeatedly. The [original installer](https://gulfcoastmaker.github.io/SkyOrb/) can be retained as a recovery reference.

## Set up Wi-Fi and the radar from your phone

1. Join the ESP32's network **`SKYORB-Setup`**, password **`SKYORB1234`**.
2. Open **`http://192.168.4.1/setup`** if the captive portal does not appear automatically. Choose your **2.4 GHz** home Wi-Fi and enter its password.
3. **Switch your phone back to your normal home Wi-Fi.** This is easy to miss.
4. Open the **numeric IP address shown on the SkyOrb display** in your phone's browser. `http://skyorb.local/` may also work, but mDNS is unreliable on some networks.
5. Use the dashboard to search your address, select a radar radius, find nearby airports, choose colors and 12/24-hour time.

**Connection tip:** Your phone/computer must be able to communicate with the ESP32 on the same LAN.

## Firmware build and source code

This repository contains source for the ESP32 application, setup UI and browser installer. GitHub Actions compiles the firmware, generates an offline airport catalogue from public-domain OurAirports data, packages the boot files, and publishes the installer on GitHub Pages.

- `src/main.cpp`: firmware, Wi-Fi/networking, aircraft data and rendering.
- `include/web_ui.h`: local phone/dashboard UI.
- `scripts/generate_airports.py`: build-time OurAirports airport catalogue generator.
- `scripts/prepare_installer.py`: copies compiled binaries into `web-installer/`.
- `.github/workflows/build.yml`: build, package and deploy.
- `web-installer/index.html`: **public installation/build-guide web page**.
- `web-installer/manifest.json`: ESP Web Tools binary locations.

The firmware currently uses the original SkyOrb GPIO assignments; **do not replace `src/main.cpp` or change the wiring just to customize the installation website**. To change website wording or styling, edit `web-installer/index.html` only. To update this GitHub landing page, edit `README.md` only. Changes to those documents trigger a normal GitHub Actions rebuild and redeployment but do not themselves change the firmware source.

## Credits and acknowledgement

**Original project concept, dual-display layout, wiring, and enclosure:** **GulfCoastMaker**.

- [Original SkyOrb installation page](https://gulfcoastmaker.github.io/SkyOrb/)
- [Original SkyOrb GitHub repository](https://github.com/GulfCoastMaker/SkyOrb)
- [SkyOrb 3D-printable housing on MakerWorld](https://makerworld.com/en/models/2959706-aviation-desk-clock-project-skyorb#profileId-3317442)

**Community remake, firmware adaptation, integration and testing:** **John Christensen**, [GitHub @Tikishark777](https://github.com/Tikishark777) — [SkyOrb-Replacement repository](https://github.com/Tikishark777/SkyOrb-Replacement).

Flight data: [ADSB.lol](https://adsb.lol/) and [ADSB.fi](https://adsb.fi/). Airport coordinates: [OurAirports](https://ourairports.com/data/) (public-domain airport database). Address lookup: [OpenStreetMap contributors](https://www.openstreetmap.org/copyright). Time-zone service: [Open-Meteo](https://open-meteo.com/). Browser installer: [ESP Web Tools](https://esphome.github.io/esp-web-tools/). Radar rendering references: [DeskRadar](https://github.com/arvis91/deskradar) and [ESP32-Plane-Radar](https://github.com/MatixYo/ESP32-Plane-Radar). ESP32 and display dependencies retain their own licenses.

The original SkyOrb repository uses the **MIT License**. This independent firmware repository has its own [LICENSE](LICENSE); follow all applicable license notices and service-provider terms. The remake is unofficial and is not endorsed by GulfCoastMaker.

**Safety:** This is a hobby display. Aircraft information can be delayed, incomplete or unavailable. Do not use for navigation, flight operations, search-and-rescue or other safety-critical decisions. Use the local configuration dashboard only on a trusted network.
