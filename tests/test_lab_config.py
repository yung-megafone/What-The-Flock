#!/usr/bin/env python3
"""Fast source contract check for the opt-in bench logging path."""
from pathlib import Path
root = Path(__file__).resolve().parents[1]
config = (root/'main/wtf_config.h').read_text()
main = (root/'main/main.c').read_text()
assert '#define WTF_LED_PEAK_RSSI_DBM (-70)' in config
assert '#define WTF_ACCEPT_LAB_BEACONS 1' in config
assert '#define WTF_LOG_LAB_ENCOUNTERS 1' in config
assert 'wtf_note_encounter(3,src_mac,pkt->rx_ctrl.rssi)' in main
assert '"[LAB WIFI] uptime_ms=' in main
assert '"LAB WIFI"' in main
assert 'if (log_queue && xQueueSend(log_queue,lab_record,0)!=pdTRUE)' in main
assert '// LAB traffic must not fall through to OUI/SSID camera rules.' in main
print('PASS: lab beacon -> RSSI -> encounter -> queued SPIFFS record source linkage')
