# SkyOrb Community Edition

**ESP32 Flight Radar + International Clock**

SkyOrb Community Edition is an independent, ground-up firmware implementation for a dual-display ESP32 desk radar and clock. It retains the original SkyOrb display concept and compatible GPIO wiring, restores aircraft tracking through alternative ADS-B feeds, and adds customizable radar and international-clock settings.

**[Install SkyOrb Community Edition and view the illustrated build guide](https://tikishark777.github.io/SkyOrb-Replacement/)**

**Project status — v3.5 Community Edition:** Tested on a physical dual-screen ESP32 unit. Wi-Fi onboarding, address search, live aircraft, radar range, offline airport lookup, sweep settings, reset, and clock display have been confirmed working on that unit. International-clock settings are included in the current firmware. Behavior may vary with hardware and networks, and minor display-motion artifacts are possible.

> **Note:** SkyOrb is a hobby display, not aviation safety equipment. See [Safety and limitations](#safety-and-limitations).

## Quick start

1. Gather the [required parts](#hardware) and [wire both displays](#wiring).
2. Connect your ESP32 to a desktop or laptop with a **USB data cable**, then use the [browser installer](https://tikishark777.github.io/SkyOrb-Replacement/) in Chrome or Edge.
3. [Connect to Wi-Fi and configure the device](#connect-to-wi-fi) from your phone.

The linked installer page also provides an illustrated build guide.

## Features

**Flight radar**
- Live nearby aircraft from [ADSB.lol](https://adsb.lol/), with [ADSB.fi](https://adsb.fi/) as a fallback, subject to provider availability.
- Green radar on the right-hand round display; selectable range of **5, 10, 20, or 30 miles**.
- **Crisp**, **Classic**, and **Glow** sweep presets, plus adjustable trail strength, speed (**5–20 seconds per revolution**), and brightness (**20–100%**).
- Offline nearby-airport catalogue generated at build time from [OurAirports](https://ourairports.com/data/), plus manual airport markers.
- Address search or direct latitude/longitude entry.

**International clock**
- White/amber time-and-date clock on the left-hand round display.
- Location-based time zone, with a regional abbreviation where recognized, an accurate UTC offset, or no zone label.
- **12/24-hour time**, optional leading zero, and independently refreshed optional seconds (off by default).
- Full written date, **MM/DD/YYYY**, **DD/MM/YYYY**, or **YYYY-MM-DD**.

**Setup and customization**
- Phone-friendly Wi-Fi onboarding, a local dashboard, and selectable portal color themes.
- Radar and clock settings that apply without reinstalling firmware; radar sweep preferences persist across restarts.
- Wi-Fi reset and local OTA firmware upload when configured.
- No-code firmware installation with [ESP Web Tools](https://esphome.github.io/esp-web-tools/).
- An option to swap the screen roles in device settings without rewiring.

## Hardware

| Part | Quantity | Notes |
| --- | :---: | --- |
| ESP-WROOM-32 development board | 1 | Original 30-pin ESP32 board |
| GC9A01 TFT display, 1.28-inch, 240 × 240, SPI, 7-pin | 2 | One radar and one clock |
| 30-pin ESP32 GPIO screw-terminal breakout | Optional | Simplifies wiring without soldering |
| Hookup wires | As needed | Connect both displays to the ESP32 |
| USB **data** cable | 1 | Required for browser flashing; USB power for normal use |
| Printed enclosure | Optional | [SkyOrb enclosure by TopherPrints](https://makerworld.com/en/models/2959706-aviation-desk-clock-project-skyorb#profileId-3317442); see its separate license |

## Wiring

This firmware uses the **original SkyOrb pinout, unchanged**. Both displays share **SCL/CLK → GPIO 18** and **SDA/MOSI → GPIO 23**, while the reset, data/command, and chip-select pins are separate.

| GC9A01 display pin | Radar display (right) | Clock display (left) |
| --- | --- | --- |
| GND | GND | GND |
| VCC | **3.3V** | **3.3V** |
| SCL / CLK | GPIO 18 | GPIO 18 |
| SDA / MOSI | GPIO 23 | GPIO 23 |
| RES / RST | GPIO 4 | GPIO 21 |
| DC | GPIO 2 | GPIO 17 (TX2) |
| CS | GPIO 5 | GPIO 16 (RX2) |

**Important:** Supply **3.3V, not 5V**, to these displays. Verify the pin labels on your modules before connecting power. These instructions are for the original SkyOrb-style **7-pin GC9A01** displays.

## Install and configure

### Flash the firmware

1. Assemble the hardware using the wiring table above.
2. Open the **[SkyOrb Community Edition installer](https://tikishark777.github.io/SkyOrb-Replacement/)** in **Chrome or Edge on a desktop or laptop**.
3. Connect the ESP32 with a reliable USB **data** cable. A charging-only cable will not work.
4. Select **Install SkyOrb Community Edition**, choose the ESP32's serial/COM port, and follow the browser prompts.
5. Keep the board connected until flashing completes.

**Warning:** Flashing overwrites existing firmware and may erase saved Wi-Fi and location settings.

### Connect to Wi-Fi

1. On your phone, join the ESP32 setup hotspot: **`SKYORB-Setup`**. The default password is **`SKYORB1234`**.
2. If the captive portal does not appear, open **`http://192.168.4.1/setup`**.
3. Choose your **2.4 GHz** home Wi-Fi network and enter its password.
4. **Reconnect your phone to your normal home Wi-Fi.**
5. Open the **numeric IP address shown on the SkyOrb display** in your phone browser. You can also try `http://skyorb.local/`, although mDNS may not work on every network.

Your phone or computer must be able to reach the ESP32 on the same local network.

### Customize radar and clock

From the device dashboard:

- **Location and airports:** Search for your address or enter coordinates; select a radar radius and configure nearby airports.
- **Radar sweep:** In **RADAR SWEEP**, choose **Crisp**, **Classic**, or **Glow**, or adjust trail, rotation speed, and brightness. Select **Apply** to update the physical radar display immediately.
- **Clock:** In **CLOCK DISPLAY**, choose your zone label, date format, 12/24-hour mode, leading zero, and optional seconds. Select **Apply clock settings**.
- **Appearance:** Choose a portal color theme and adjust other available device settings.

These changes do not require a fresh firmware installation.

## Troubleshooting

| Problem | What to try |
| --- | --- |
| Browser cannot flash the ESP32 | Use desktop Chrome or Edge, a known-good **USB data cable**, and a direct USB port. Select the correct serial/COM port. |
| USB write fails repeatedly | If it fails again at the same point, stop and investigate rather than repeatedly erasing the board. The [original SkyOrb installer](https://gulfcoastmaker.github.io/SkyOrb/) remains a recovery reference. |
| Phone cannot reach the dashboard | Reconnect the phone to your home Wi-Fi and use the device's **numeric IP address**. Check that both devices are on a LAN where they can communicate. |
| `skyorb.local` does not open | Use the numeric IP address; mDNS may be unavailable or blocked. |
| Radar sweep shows a faint trail | **Crisp** disables the programmed fading trail, but some physical-display refresh artifacts may remain. |
| Aircraft are missing or delayed | Check connectivity and allow for interruptions or incomplete data from the ADS-B providers. |

**Network example:** A computer on `10.0.0.x` and a SkyOrb on `192.168.86.x` may be separated by router configuration.

## Source code and automated builds

The repository contains the firmware, local dashboard UI, and browser installer. GitHub Actions builds the ESP32 firmware, generates the offline airport catalogue from OurAirports data, packages the required binary files, and publishes the installer through GitHub Pages.

| File | Purpose |
| --- | --- |
| [`src/main.cpp`](src/main.cpp) | ESP32 firmware, networking, aircraft data, and screen rendering |
| [`include/web_ui.h`](include/web_ui.h) | Phone setup portal and local dashboard |
| [`scripts/generate_airports.py`](scripts/generate_airports.py) | Build-time airport catalogue generator |
| [`scripts/prepare_installer.py`](scripts/prepare_installer.py) | Copies compiled binaries to `web-installer/` |
| [`.github/workflows/build.yml`](.github/workflows/build.yml) | Build, package, and deploy workflow |
| [`web-installer/index.html`](web-installer/index.html) | Public browser installer and illustrated build guide |
| [`web-installer/manifest.json`](web-installer/manifest.json) | ESP Web Tools binary locations |

**Editing guidance:** To change the public installation page, edit `web-installer/index.html`. To change this GitHub project overview, edit `README.md`. Documentation-only changes may trigger the normal build/deployment workflow but do not alter the firmware source. **Do not replace `src/main.cpp` or change GPIO wiring merely to update website content.**

## Issues, contributions, and maintenance

For firmware problems, [open a GitHub issue](https://github.com/Tikishark777/SkyOrb-Replacement/issues) and include the ESP32 model, display type, browser, relevant Actions run, and any serial-console logs. **Remove private details** before posting logs.

For project maintainers:

- Keep GitHub Actions deployment enabled and check that the workflow succeeds after changes. Commit history and build logs are public, including historical failed builds.
- Never commit Wi-Fi passwords, private locations, API keys, or tokens. The documented setup-hotspot password is a firmware default, not a personal credential. If a real secret is exposed, **rotate it**; removing it from the current file does not erase Git history.
- Use GitHub Releases (for example, `v3.5.0`) to mark tested firmware versions and retain a local copy of the last working installer artifacts.
- Update the README, the Pages installer, and version metadata when firmware features change. Documentation edits alone do not require reflashing a working device.
- In the repository **About** sidebar, add the installation-site URL and useful topics, such as `esp32`, `gc9a01`, `flight-radar`, `ads-b`, `arduino`, `clock`, and `github-pages`.

## Origins and acknowledgements

SkyOrb Community Edition is an **unofficial, independent ground-up firmware rebuild**, not a modified copy of the original compiled firmware. It was developed after the original `airplanes.live` feed proved unreliable for this project, including HTTP 403 responses.

- **Original SkyOrb concept, firmware, and dual-display wiring:** [GulfCoastMaker](https://github.com/GulfCoastMaker/SkyOrb). The [original installation page](https://gulfcoastmaker.github.io/SkyOrb/) and repository remain available. The original repository is MIT-licensed; retain any applicable original notices if copying source code.
- **Printable SkyOrb enclosure:** [TopherPrints (@Topherprints_45)](https://makerworld.com/en/@Topherprints_45), creator of the [Aviation Desk Clock — Project SkyOrb model](https://makerworld.com/en/models/2959706-aviation-desk-clock-project-skyorb#profileId-3317442). The model's MakerWorld license is separate from the software license; review its terms before modifying or redistributing it.
- **SkyOrb Community Edition replacement firmware and maintenance:** John Christensen ([@Tikishark777](https://github.com/Tikishark777)), including firmware development, customizations, hardware testing, browser installation site, and documentation.

Additional services, tools, and references:

- [ADSB.lol](https://adsb.lol/) and [ADSB.fi](https://adsb.fi/) — aircraft reports.
- [OurAirports](https://ourairports.com/data/) — public-domain airport data.
- [OpenStreetMap contributors](https://www.openstreetmap.org/copyright) / Nominatim — address lookup.
- [Open-Meteo](https://open-meteo.com/) — time-zone offset information.
- [ESP Web Tools](https://esphome.github.io/esp-web-tools/) — browser-based firmware flashing.
- [DeskRadar](https://github.com/arvis91/deskradar) and [ESP32-Plane-Radar](https://github.com/MatixYo/ESP32-Plane-Radar) — screen-rendering research references; no claim is made that their code or designs were reused.
- GitHub, GitHub Actions, and GitHub Pages — source hosting, automated builds, and installer hosting.

This Community Edition is **not endorsed by GulfCoastMaker or TopherPrints**.

## License

The software in this repository is provided under the [MIT License](LICENSE). Upstream libraries, aircraft-data services, and the printable enclosure have their **own separate terms**, which must also be respected.

## Safety and limitations

SkyOrb is a hobby aircraft display. Aircraft information may be delayed, incomplete, or unavailable. **Do not use it for navigation, flight operations, search and rescue, or any other safety-critical decisions.** Use the local configuration dashboard only on a trusted network.
