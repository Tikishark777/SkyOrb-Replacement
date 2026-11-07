# SkyOrb Community Edition — ESP32 Flight Radar & International Clock

**Stable release: v4.0.0 · Hardware-tested · Open source**

**SkyOrb Community Edition** is an independently rebuilt ESP32 aviation desk instrument created and maintained by **John Christensen** ([@Tikishark777](https://github.com/Tikishark777)). It preserves the two-round-display layout and **original wiring** of [GulfCoastMaker's SkyOrb](https://github.com/GulfCoastMaker/SkyOrb), while introducing a replacement aircraft feed, an offline airport catalog, an international clock, configurable radar animation, and **four selectable aviation information views**.

**[Install SkyOrb Community Edition — free browser installer and illustrated build guide](https://tikishark777.github.io/SkyOrb-Replacement/)**

[Source code](https://github.com/Tikishark777/SkyOrb-Replacement) · [Original SkyOrb](https://gulfcoastmaker.github.io/SkyOrb/) · [Printable enclosure by TopherPrints](https://makerworld.com/en/models/2959706-aviation-desk-clock-project-skyorb#profileId-3317442)

> **Release status:** SkyOrb Community Edition's radar, Wi-Fi setup, aircraft views, screen rotation and international clock have been installed and tested on real dual-GC9A01 ESP32 hardware. Hardware and network environments can vary. This is a hobby information display, **not an aviation navigation or safety instrument**.

## Highlights

- **Live aircraft:** Nearby ADS-B-equipped aircraft from [ADSB.lol](https://adsb.lol/), falling back to [ADSB.fi](https://adsb.fi/) when needed. Aircraft coverage, latency and availability vary.
- **Four right-hand screen views:** Radar, Aircraft Spotlight, Traffic Summary and Airport Weather; choose one or rotate a custom selection.
- **Customizable rotation:** Include any combination of the four views and choose **10, 15, 20, 30, 45 or 60 seconds** per view. **Radar only is the default.**
- **Airport METAR weather:** Latest available observation at a nearby reporting airport, including temperature, wind, visibility, cloud cover and flight category where supplied. Weather updates periodically, when the feature is in use; it is **not a forecast**.
- **Radar range:** 5, 10, 20 or 30 miles around a searched address or coordinates.
- **Radar sweep:** Crisp, Classic and Glow presets, or custom trail (0–100%), rotation speed (5–20 seconds per revolution) and brightness (20–100%).
- **Offline airport discovery:** Search the build-generated [OurAirports](https://ourairports.com/data/) catalog or add airports by ICAO/IATA code or coordinates.
- **International clock:** Automatic location-based time zone, 12/24-hour format, leading zero, optional seconds, flexible labels and four date formats.
- **Easy setup:** Wi-Fi scan/selection, phone dashboard, portal color themes, Wi-Fi reset and password-protected local firmware-update support.
- **Browser installation:** Flash prebuilt firmware from desktop Chrome or Edge without Arduino IDE or PlatformIO.

### Right-hand screen choices

| Mode | What appears on the round display |
| --- | --- |
| **Live Radar** | Animated green sweep, aircraft targets and airport markers, adjustable radius |
| **Aircraft Spotlight** | Nearest reported flight; its identifier, altitude, ground speed, heading and distance |
| **Traffic Summary** | Aircraft count, closest reported flight, highest altitude and fastest aircraft |
| **Airport Weather (METAR)** | Available recent station weather: temperature, wind, visibility, clouds and flight category |
| **Rotate selected views** | Cycles through any checked combination of Radar, Spotlight, Summary and Weather |

No aircraft in range? Spotlight and Summary indicate that instead of inventing data. No recent METAR? The weather view reports it as unavailable rather than showing a made-up reading. The **left clock remains active** in every right-hand mode.

## Parts list

| Part | Qty | Notes |
| --- | :---: | --- |
| **ESP-WROOM-32**, 30-pin development board | 1 | Original-style ESP32 hardware |
| **GC9A01** round SPI TFT, 1.28 in, 240 × 240, seven pins | 2 | Left clock; right aviation display |
| 30-pin ESP32 screw-terminal breakout | Optional | Makes wiring easier |
| Hookup wires | As needed | For both screens |
| USB **data** cable | 1 | Required for USB flashing, not just charging |
| Printed case | Optional | [TopherPrints' SkyOrb enclosure on MakerWorld](https://makerworld.com/en/models/2959706-aviation-desk-clock-project-skyorb#profileId-3317442) |

### Wiring — original SkyOrb pinout, unchanged

Both displays share **SCL/CLK → GPIO 18** and **SDA/MOSI → GPIO 23**. The remaining control lines are separate.

| GC9A01 pin | **Left: clock** | **Right: aviation views** |
| --- | --- | --- |
| GND | GND | GND |
| VCC | **3.3 V** | **3.3 V** |
| SCL / CLK | GPIO 18 | GPIO 18 |
| SDA / MOSI | GPIO 23 | GPIO 23 |
| RES / RST | GPIO 21 | GPIO 4 |
| DC | GPIO 17 | GPIO 2 |
| CS | GPIO 16 | GPIO 5 |

**Important:** These are the original-style seven-pin GC9A01 modules. Use their specified **3.3 V** supply, verify your module's labels and electrical requirements, and do not change the pins simply to install this software. The phone dashboard also has an optional screen-swap setting for clock/radar roles.

## Installation — no coding required

1. Assemble the hardware using the wiring chart above.
2. Open **[the SkyOrb installer](https://tikishark777.github.io/SkyOrb-Replacement/)** on a desktop or laptop using **Chrome or Edge**.
3. Connect your ESP32 using a known-good USB **data** cable, ideally directly to the computer.
4. Select **Install SkyOrb Community Edition**, choose the ESP32 COM/serial port and follow the on-screen instructions.
5. Wait for the browser to report success before disconnecting the device. **Flashing replaces existing firmware** and may clear stored Wi-Fi or location settings.

The installer uses ESP Web Tools; its compiled binaries are generated and published by this repository's GitHub Actions workflow.

## First-time Wi-Fi setup and phone dashboard

1. Connect your phone to Wi-Fi **`SKYORB-Setup`** using password **`SKYORB1234`**.
2. If the captive portal does not open automatically, browse to **`http://192.168.4.1/setup`**. Scan/select your **2.4 GHz** home network and save its password.
3. **Reconnect the phone to your normal home Wi-Fi** afterward.
4. Find the SkyOrb device's numeric IP address on its display (shown briefly after startup, or while configuring), or look it up in your router's connected devices. Open `http://<device-IP>/` in the phone browser. `http://skyorb.local/` may work on some networks but is less reliable.
5. Under **SEARCH CENTER**, search a city, address, airport area or other place; or type coordinates. Select a **5 / 10 / 20 / 30-mile** radius and press **Apply location**.
6. In **AIRPORTS**, find nearby commercial airports from the offline catalog, search by **ICAO/IATA code**, or add a marker by coordinates.
7. Use the **RIGHT SCREEN VIEWS**, **RADAR SWEEP** and **CLOCK DISPLAY** sections described below to customize the physical displays. Changes are stored in the ESP32 and don't require another firmware installation.

**Network note:** Your phone and SkyOrb must be able to reach each other on the same local network. A guest network or separate router/subnet can block the dashboard even while both devices have Internet access.

### RIGHT SCREEN VIEWS — choose or rotate

1. Open the local dashboard and scroll to **RIGHT SCREEN VIEWS**.
2. Set **Display mode** to **Radar only**, **Aircraft Spotlight**, **Traffic Summary**, **Airport Weather (METAR)**, or **Rotate selected views**.
3. If rotating, check the views to include and choose **seconds per view** (10–60 seconds).
4. Press **Apply right-screen mode**. The selected display changes without rebooting. To restore the original behavior, select **Radar only**.

**METAR details:** Airport Weather uses recent reports from nearby METAR-reporting airports, via the [Aviation Weather Center](https://aviationweather.gov/data/api/). The firmware checks for updates at a conservative interval of roughly 15 minutes while the feature is enabled. Not every airport issues METARs, and not every report includes every field. A report describes the station and its observation time, not conditions at every point inside the radar range.

### RADAR SWEEP — Crisp / Classic / Glow

Choose a preset or adjust the three sliders: trail/ghosting from **0–100%**, sweep rotation from **5–20 seconds per turn**, and sweep brightness from **20–100%**. Press **Apply radar sweep**. Settings take effect without resetting the radar center, airport markers or Wi-Fi. **Crisp** removes intentionally programmed trailing rays; some LCD motion/refresh artifacts can still remain.

### CLOCK DISPLAY — international choices

- **Time format:** 12-hour (AM/PM) or 24-hour, with optional leading zero.
- **Seconds:** Optional smaller seconds digits; off by default.
- **Time-zone label:** Recognized regional abbreviation (for example PDT/PST, CET/CEST, JST) where reliably available; or an exact UTC offset; or no label.
- **Date:** Full written date, **MM/DD/YYYY**, **DD/MM/YYYY**, or **YYYY-MM-DD**.

The clock follows the selected radar location. Daylight-saving adjustments are obtained from the time-zone data service. Press **Apply clock settings** to save.

## Understanding the data and its limits

- **Aircraft:** ADS-B coverage is incomplete and some aircraft do not broadcast their positions. The app deliberately ignores stale aircraft positions and depends on external feed availability. Altitude/speed/heading are reported values and may be missing or delayed.
- **Spotlight and summary:** These are computed from the same reported aircraft already in the radar; no separate receiver is required. They do **not** represent all aircraft in the sky.
- **Airport METAR:** This is an **observation**, not a weather prediction. It can be old, missing, or unavailable, and the reporting airport may not be at the radar center. Consult the Aviation Weather Center or official aviation products for operational decisions.
- **Airports:** Discovery is based on an onboard catalog generated from public-domain OurAirports data; the list can omit small fields or change between builds. Manual locations are configurable.
- **Connectivity:** Aircraft and METAR reports require Internet access. The time display needs time synchronization. The portal is intended only for a **trusted local network**.
- **HTTPS:** The current ESP32 firmware makes some read-only public HTTPS requests without certificate validation. Do not treat retrieved data as authenticated, safety-critical information.

**Safety:** This is an informational hobby display. Do **not** use it for navigation, flight operations, collision avoidance, emergency response, or any other safety-critical aviation decision.

## Source layout and maintenance

| Location | Purpose |
| --- | --- |
| `src/main.cpp` | ESP32 firmware, aviation views, radar graphics, clock, connectivity, aircraft and METAR requests |
| `include/web_ui.h` | Local phone setup/settings interface |
| `platformio.ini` | ESP32 PlatformIO board and library configuration |
| `scripts/generate_airports.py` | Builds the offline OurAirports airport catalog |
| `scripts/prepare_installer.py` | Stages browser installer firmware binaries |
| `.github/workflows/build.yml` | Compiles firmware and publishes GitHub Pages |
| `web-installer/index.html` | Public installation website and build guide |
| `web-installer/manifest.json` | Installer version label and flashing offsets |
| `LICENSE` | MIT software license |

The GitHub Pages site, README and manifest version are **documentation/metadata**, not ESP32 firmware code. Updating them will trigger GitHub Actions, but **you do not need to flash a working ESP32 again for documentation-only changes**. Do not modify source files, airport generator or wiring solely to update the website. Keep a backup of the last known-good firmware and confirm Actions turns green for each new release.

### Release notes — v4.0.0 stable

- Added **Aircraft Spotlight** for the nearest reported flight.
- Added **Traffic Summary** with nearby aircraft statistics.
- Added optional **Airport Weather (METAR)** from nearby reporting stations.
- Added **Right Screen Views** selector and custom **10–60 second rotation** of any selected views.
- Preserved the original animated radar as the default; no display, GPIO, clock or Wi-Fi wiring changes.
- Retained v3.5 international clock, v3.4 sweep controls, offline airport discovery and phone dashboard.
- Tested the new aviation screens on the physical dual-display ESP32 build.

## Origins and credits

**Original SkyOrb concept, original firmware and dual-display GPIO arrangement:** [GulfCoastMaker](https://github.com/GulfCoastMaker/SkyOrb), whose [original SkyOrb installation page](https://gulfcoastmaker.github.io/SkyOrb/) remains available as a reference. The original code is MIT-licensed; preserve applicable attribution for any reused code.

**Printed SkyOrb enclosure/model on MakerWorld:** [TopherPrints (@Topherprints_45)](https://makerworld.com/en/@Topherprints_45), publisher of [Aviation Desk Clock — Project SkyOrb](https://makerworld.com/en/models/2959706-aviation-desk-clock-project-skyorb#profileId-3317442). The printed design has its **own license**, separate from this software's MIT license.

**SkyOrb Community Edition — independent replacement firmware, integration, hardware testing and maintenance:** **John Christensen** ([@Tikishark777](https://github.com/Tikishark777)). The project was rebuilt after the original **airplanes.live** connection proved unreliable for this hardware/software combination. This does not mean that airplanes.live has shut down globally. The Community Edition is unofficial and is not endorsed by GulfCoastMaker or TopherPrints.

**Data and services:** [ADSB.lol](https://adsb.lol/), [ADSB.fi](https://adsb.fi/), [OurAirports](https://ourairports.com/data/), [Aviation Weather Center](https://aviationweather.gov/data/api/) (METARs), [OpenStreetMap contributors](https://www.openstreetmap.org/copyright) and Nominatim (address search), [Open-Meteo](https://open-meteo.com/) (time-zone information), [ESP Web Tools](https://esphome.github.io/esp-web-tools/) (USB installer), and [GitHub](https://github.com/) (repository, builds and Pages hosting). Radar-rendering research also referenced [DeskRadar](https://github.com/arvis91/deskradar) and [ESP32-Plane-Radar](https://github.com/MatixYo/ESP32-Plane-Radar). Honor each provider's terms and software licenses.

The firmware is distributed under this repository's [MIT License](LICENSE). The software license does not cover third-party 3D models, APIs or datasets.

---

**Project creator and maintainer:** John Christensen · [@Tikishark777](https://github.com/Tikishark777) · [SkyOrb Community Edition website](https://tikishark777.github.io/SkyOrb-Replacement/)
