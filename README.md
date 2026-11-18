> **v0.1.4.1 LAB TELEMETRY BUILD:** Both lab-beacon acceptance and lab SPIFFS encounter logging are enabled. The LED full-scale threshold is provisionally −70 dBm; all recorded RSSI readings remain raw. Read [`RELEASE-NOTES-v0.1.4.1-preview.md`](RELEASE-NOTES-v0.1.4.1-preview.md) for details and field-only settings.
>
> Adds first-boot SPIFFS creation (only if fully erased), writable-storage check, matching-transmitter encounter summaries with first/peak/last RSSI, and configurable LED full-scale via `WTF_LED_PEAK_RSSI_DBM`. This records matching-frame metadata, **not raw packets or PCAP**.

# What The Flock (WTF)

**Headless Wi-Fi/Bluetooth LE signature monitoring for the Seeed Studio XIAO ESP32-C5.**

What The Flock (WTF) is an experimental ESP-IDF firmware project that listens for selected wireless identifiers associated with Flock-style surveillance equipment and provides a visual, RSSI-based indication when a configured signature is observed. Potential detections can be recorded to on-board SPIFFS storage.

This repository is a fork of [spuder/What-The-Flock](https://github.com/spuder/What-The-Flock), itself described as an ESP-IDF port of the original *Flock You* project. **Credit for the original project and signatures belongs to their respective authors.** This fork's current development target is the **Seeed Studio XIAO ESP32-C5**, with a focus on autonomous operation, offline diagnostics, and improved signal-strength feedback.

> **Prototype status — v0.1.4 (unverified build):** The ESP32-C5 build, boot-time jumper diagnostics, LED patterns, SPIFFS initialization, and synthetic RSSI sweep have been exercised on hardware. Detection of actual surveillance-camera transmissions, false-positive rates, and usable range **have not yet been independently validated**. A matching wireless signature is an indicator, not proof of device identity or physical proximity.

## v0.1.4 development update

This revision is built directly on the supplied working v0.1.3 archive. **The v0.1.4 firmware has passed host-only scheduler and RSSI tests but has not yet been compiled with ESP-IDF or exercised on physical C5 hardware.** Keep a working v0.1.3 binary as your rollback until the first hardware tests pass.

- **Adaptive scanning:** P1 prioritizes only 1/6/11; P2 spends 6 of every 7 slots on 1/6/11, with the seventh rotating through remaining 2.4 GHz channels; P3 scans channels 1–13 equally. Dwell is approximately 125 ms for P1/P2, 110 ms for P3. This is one radio: no simultaneous off-channel monitoring.
- **Conservative temporary lock:** A recognized Wi-Fi SSID signature can lock immediately, while an OUI-only match requires two observations of the same source on the same channel within 1.2 seconds. Locks expire after 1.8 seconds of silence or 4.5 seconds maximum, followed by a 500 ms reacquisition cooldown. These are initial experimental settings.
- **Persistent profiles:** Press a normally-open button connecting D5/GPIO24 to GND to cycle P1 → P2 → P3 → P1. The selected profile is written to NVS and restored on reboot; default is P2. Press and release for each change; do not leave the jumper permanently grounded.
- **LED acknowledgment:** At normal boot and each profile change the LED emits 10 short flashes, pauses 300 ms, then flashes the profile number (1–3). SOS retains highest priority. Acknowledgment temporarily supersedes other LED indications but does not stop RF scanning.
- **Lab mode (this package):** `WTF_ACCEPT_LAB_BEACONS=1` and `WTF_LOG_LAB_ENCOUNTERS=1`. The `WTF-RF-Testbench` beacons are logged under distinct `[LAB WIFI]` and `[ENCOUNTER LAB WIFI]` labels, never as genuine camera detections.

## Features

- **Concurrent Wi-Fi and BLE monitoring:** Wi-Fi promiscuous reception with channel hopping and NimBLE advertisement scanning.
- **Signature matching:** Configured Wi-Fi MAC prefixes and SSID patterns, BLE address/name patterns, and selected manufacturer data. See [`main/main.c`](main/main.c) for the actual signatures in this revision.
- **Continuous RSSI indicator:** Received RSSI values are processed into a continuously varying LED pulse interval, rather than only a handful of discrete strength bands.
- **Trend-aware display:** A short peak hold, gradual weakening indication, and double-tick pattern for repeated decreases from the *same tracked source*. RSSI is not a calibrated distance measurement.
- **Persistent detections:** A dedicated 1 MiB SPIFFS partition for detection records at `/spiffs/detections.txt`, when the filesystem is available.
- **Centralized error handling:** Fatal errors latch a repeating Morse SOS pattern; certain nonfatal errors allow scanning to continue in a degraded state.
- **Offline diagnostic mode:** A boot-time jumper runs subsystem checks and a synthetic RSSI approach/recession sweep, ending in an intentional SOS test. No serial monitor required.

## Hardware

| Item | Current configuration |
| --- | --- |
| Development board | Seeed Studio XIAO ESP32-C5 |
| Status indicator | Onboard user LED, GPIO27 (active-low) |
| Diagnostic jumper | **D10 / GPIO10 → GND** before reset |
| Profile button | **D5 / GPIO24 → GND** momentary switch; internal pull-up |
| Diagnostic input when open | Internal pull-up selects normal operation |
| Buzzer | Disabled by default (`WTF_ENABLE_BUZZER=0`) |
| Radio | Integrated Wi-Fi and Bluetooth LE |
| Host computer | Linux with ESP-IDF 5.5.x; tested development environment: Fedora |

**Never connect D10 to 5 V.** The XIAO uses 3.3 V GPIO logic. The D10 jumper is read at boot; changing it while running does not change modes until reset.

Earlier upstream hardware targets include ESP32-C6, C3, and S3; **those boards are not verified against this fork's current ESP32-C5-focused configuration**. Do not assume that the GPIO assignments or diagnostics apply unchanged to them.

## LED indications

| Indication | Meaning |
| --- | --- |
| Three brief flashes during boot | Startup animation |
| 10 flashes, pause, 1–3 flashes | Current saved scan profile P1/P2/P3; also shown on profile change |
| Normal periodic heartbeat (5-second interval) | Idle / scanning indication |
| Short alert pattern | New matching transmitter (subject to cooldown) |
| Pulses gradually speeding up | Stronger *relative* RSSI from tracked source |
| Solid LED | Displayed signal is at or above the current strong-signal threshold (approximately −45 dBm) |
| Slowing double-ticks | A measured weakening trend has been established for the selected source |
| Repeating `... --- ...` | Latched fatal fault, or the intentional end of the offline diagnostic test |

A strong RSSI reading does **not** necessarily mean the transmitter is immediately beside the board. Propagation, antenna orientation, transmitter power, obstructions, and reflections can all affect the reading. The current solid-to-pulsing transition can look unusually rapid; this is a **known animation refinement**, not evidence on its own of a self-test failure.

## Build

Install [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c5/get-started/index.html) and its toolchain. The development setup used ESP-IDF **5.5.2**.

```bash
# Adjust this path if your ESP-IDF installation lives elsewhere.
. ~/esp/esp-idf/export.sh

cd ~/What-The-Flock
idf.py set-target esp32c5   # First setup only; may regenerate sdkconfig
./tools/build-flash-headless.sh
```

`build-flash-headless.sh` performs a build only when no port is specified. Unlike the older `build-flash-fedora.sh`, it does **not** automatically open the serial monitor.

## Flashing the XIAO ESP32-C5

### Recommended for the current prototype: manual ROM download mode

This workaround avoids the USB serial/reset instability observed with the running application.

1. Disconnect any serial monitor or OpenOCD session.
2. Hold **BOOT**, tap **RESET**, then release **BOOT**.
3. Identify the board's current serial port (`/dev/ttyACM*` may change after reset).
4. Flash the already-built images:

```bash
cd ~/What-The-Flock
. ~/esp/esp-idf/export.sh
./tools/flash-rom-download.sh /dev/ttyACM1
```

Alternatively, build and flash in one operation:

```bash
./tools/build-flash-headless.sh --rom /dev/ttyACM1
```

Replace `/dev/ttyACM1` with the port actually assigned to your board. The ROM script writes the bootloader, partition table, and application; it **does not intentionally erase or write the SPIFFS storage partition**. After flashing, press physical **RESET** to exit ROM download mode.

For boards whose normal serial reset works, `./tools/build-flash-headless.sh /dev/ttyACM1` uses `idf.py flash`, but this path has been unreliable on the current prototype.

## First-time SPIFFS setup

**Important:** The `storage` partition may be entirely blank on a freshly flashed board. The firmware currently uses `format_if_mount_failed = false`, so flashing the application does **not** automatically initialize SPIFFS. In normal mode, an unavailable SPIFFS filesystem is reported as a degraded logging fault; the diagnostic mode requires a working storage read/write test.

For a **new, confirmed-empty** storage partition, create and flash a filesystem image:

```bash
cd ~/What-The-Flock
. ~/esp/esp-idf/export.sh

mkdir -p /tmp/wtf-spiffs-init
printf 'What-The-Flock SPIFFS initialized\n' > /tmp/wtf-spiffs-init/init.txt

python "$IDF_PATH/components/spiffs/spiffsgen.py" \
  0x100000 /tmp/wtf-spiffs-init /tmp/wtf-spiffs.img

# Put the XIAO into BOOT/download mode first.
python -m esptool --chip esp32c5 -p /dev/ttyACM1 -b 115200 \
  --before no_reset --after no_reset --no-stub \
  write_flash 0x210000 /tmp/wtf-spiffs.img
```

**Warning:** Flashing this image **overwrites the entire 1 MiB SPIFFS partition and destroys stored detections**. Only do this on a known-empty partition or after backing up existing records. The offset and size above match the current project's partition table; check [`partitions.csv`](partitions.csv) if the layout changes. Reset physically after flashing.

## Offline hardware self-test (D10 jumper)

This test does not require a Flock camera, a USB serial client, or any special transmitter.

1. Power off the board or prepare for a reset.
2. Connect **D10 (GPIO10) to GND**.
3. Press **RESET** and leave the serial monitor closed.
4. Observe the status LED:

   - Firmware and storage initialization; actual SPIFFS write/read/compare test, radio startup checks, and RSSI math assertions.
   - Two normal idle heartbeat periods (10 seconds total), while real Wi-Fi packets and BLE advertisements are counted. **No received packets is inconclusive**, not proof the radios are defective.
   - Synthetic RSSI increases from **−95 to −45 dBm** in **5 dBm steps**, held for **2 seconds per step**.
   - An additional **2-second** maximum-strength hold, producing a solid LED.
   - Synthetic RSSI falls from **−50 to −95 dBm** in **5 dBm steps**, held for **1.5 seconds per step**, to exercise weakening-trend double-ticks.
   - A short no-sample interval, then **intentional latched SOS** (fault F11) to prove the fault indicator works.

5. Remove the jumper and press **RESET** to return to normal scanning. A fatal fault/SOS latch is cleared by rebooting.

The synthetic sweep calls the **same RSSI update/filter functions** used for radio detections, but deliberately does **not** add fake detections to the persistent log. SOS at the **end** of this test is expected; SOS immediately upon startup indicates a failure or incomplete initialization.

## Host-side signal-math tests

The RSSI interpolation, filtering, trend logic, and LED timing functions can also be tested on a Linux machine without flashing a board:

```bash
cd ~/What-The-Flock
./tests/run-host-tests.sh
```

The runner compiles RSSI and scheduler tests (`test_signal_math.c` and `test_scan_math.c`) against the production math headers using a host C compiler. Passing host tests does not establish RF reception quality or real-world signature accuracy.

## Logging and diagnostics limitations

- A matching detection is appended to `/spiffs/detections.txt` if SPIFFS and the logging task are available.
- Automatic dumping of the full detection history at boot was **removed**.
- The optional interactive commands (`status`, `logs`, `logs stats`, etc.) exist behind `WTF_ENABLE_DEBUG_CONSOLE`, but the feature is **disabled by default**. **Opening the USB serial device has been observed to interrupt the application**, even with that task disabled. Do not rely on `idf.py monitor` for routine operation or data extraction until this is fixed.
- There is currently **no validated convenient headless log-export workflow**. Back up flash before modifying the storage partition, and treat manual recovery/extraction as an experimental procedure.
- Fatal fault SOS relies on the running application and scheduler; it cannot indicate a CPU hang in ROM or a halted processor.

## Configuration and layout

```text
main/
  main.c                 Wi-Fi/BLE monitoring, detection, storage, indicators, diagnostics
  wtf_config.h           GPIOs, feature toggles, RSSI and timing settings
  wtf_signal_math.h      RSSI filtering, interpolation, trend and pulse math
  wtf_scan_math.h        Profile schedules, confidence and lock expiry
tests/
  test_signal_math.c     Host-side RSSI unit tests
  test_scan_math.c       Host-side scheduler unit tests
  run-host-tests.sh      Test runner
tools/
  build-flash-headless.sh
  flash-rom-download.sh
partitions.csv            Flash partition layout
sdkconfig.defaults        ESP-IDF defaults
```

Main configuration values are in [`main/wtf_config.h`](main/wtf_config.h). In particular, the GPIO10 diagnostic selector, GPIO27 LED, five-second scanning heartbeat, and RSSI behavior can be adjusted there. Avoid enabling the debug console on this prototype until the USB issue is resolved.

## Controlled RF testbench

The standalone companion project is included in `WTF-RF-Testbench/` **alongside** the detector source in the development ZIP, not nested inside the firmware build. It emits a clearly labeled `WTF-LAB-01` AP beacon, defaults to channel 6, and cycles channels 6/11/1 with its D5 button. Change `LAB_TX_QDBM` and rebuild for a controlled transmit-power test. No real camera identity is cloned. See its own README. This is a development aid, not a proof of live Flock detection.

## Roadmap / known issues

- [ ] Hardware-build and validate the v0.1.4 adaptive scheduler and button/NVS functions.
- [ ] Add reliable packet-level capture and timestamped lock metrics for lab benchmarking.
- [ ] Add one-shot and runtime-adjustable TX power to the companion testbench.
- [ ] Investigate USB-Serial/JTAG connection-triggered stalls and make the debug console safe.
- [ ] Refine the aggressive transition between solid LED and very fast pulses at high RSSI.
- [ ] Validate live signature detection against known, authorized test sources and assess false positives.
- [ ] Add a safe, documented method for retrieving stored detections without relying on the unstable console.
- [ ] Improve headless fault-code identification beyond a common SOS indication.
- [ ] Expand support and hardware validation for additional ESP32 boards, where feasible.

## Attribution

This fork builds on [spuder/What-The-Flock](https://github.com/spuder/What-The-Flock), which credits the original *Flock You* project. Existing upstream code and signature lists remain subject to their original authors' rights and any applicable license terms; **verify upstream licensing before redistributing beyond the terms allowed**. This project is intended for legitimate research and educational use. Comply with local laws and respect the privacy of others.
