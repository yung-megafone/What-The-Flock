# WTF v0.1.4.1 (FT PENDING, LAB TEST PASS)

**Experimental source preview.** The user has chosen to skip a complete ROM backup. Flashing changes the app and bootloader/partition table; keep the source and prior binaries for rollback. This is not hardware-tested.

- First-run SPIFFS creation is allowed **only if every byte** in the `storage` partition is `0xFF`. Never silently format nonblank/corrupt SPIFFS. Validate mount with a write probe.
- Existing new-device detection log lines remain. Additional `[ENCOUNTER WIFI/BLE]` lines on transmitter silence (3 s) summarize `first`, `peak`, `last` measured dBm, sample count, MAC, and start/end **uptime** (not wall clock). These are matching-frame observation summaries, **not captured raw packets or PCAP**.
- Config `WTF_LED_PEAK_RSSI_DBM` controls only LED signal-speed/solid threshold. This **lab build defaults to -70 dBm** for faster/stronger visual indications during passing, pending observed on-road peak values. Restore `(-45)` for legacy LED display behavior. Raw RSSI and detection matching are unmodified.
- The existing flashing/e-stop issue on application USB debugging is not investigated or fixed here. Use ROM download mode to read backups.
- Host-only RSSI and channel scheduling tests run via `bash tests/run-host-tests.sh`. ESP-IDF build / actual radio / flash behavior **not tested in this environment**.
- **Caution**: First-run initialization formats the previously blank 1MiB storage partition. It preserves non-blank partitions and never formats NVS.

**Not a packet sniffer/PCAP recorder:** Stored records contain metadata of frames matching configured signatures, not packet payloads. A matched signature near a camera does not by itself prove the camera transmitted it.

## Lab telemetry changes

- `WTF_ACCEPT_LAB_BEACONS=1` and `WTF_LOG_LAB_ENCOUNTERS=1` in this lab package.
- The transmitted `WTF-LAB-01` beacon exercises the real RF callback, channel confidence, RSSI/LED logic and the same RAM encounter aggregator, queued SPIFFS logger, and reboot-persistent storage used for genuine detections.
- On a first lab reception, expect `[LAB WIFI] ... RSSI=...`. After 3 seconds without further lab reception, expect `[ENCOUNTER LAB WIFI] ... first=... peak=... last=... samples=...`. Allow at least 5 seconds after transmitter stops before shutting down the detector.
- RSSI values in records are actual receiver measurements. The configurable **-70 dBm LED full-scale threshold** only changes how measurements are displayed.
- To disable testbench acceptance and writing in a normal field-only build, set both `WTF_ACCEPT_LAB_BEACONS` and `WTF_LOG_LAB_ENCOUNTERS` to 0.

