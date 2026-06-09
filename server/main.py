from lora_receiver import LoRaReceiver

lora = LoRaReceiver(host="172.16.110.115", port=1700)

lora.add_device("tank_1",
    dev_addr = 0x01020304,
    nwk_skey = "112233445566778811223344556677 88",
    app_skey = "AABBCCDDEEFFAABBAABBCCDDEEFFAABB",
)

def on_packet(name, data):
    print(f"\n[{name}] {data['time']}")
    print(f"  Distance : {data['distance_cm']} cm")
    print(f"  Battery  : {data['voltage']} V")
    print(f"  RSSI     : {data['rssi']} dBm")
    print(f"  FCnt     : {data['fcnt']}")

    # ── Auto pump control ─────────────────────────────────────
    # Tank: sensor 30cm above water, depth 200cm
    # Water level = (230 - dist) / 200 × 100 %
    # Relay ON  when level < 75%  → dist > 80cm
    # Relay OFF when level > 97%  → dist < 35cm
    level_pct = round((230 - data['distance_cm']) / 200 * 100, 1)
    print(f"  Level    : {level_pct}%")

    if data['distance_cm'] > 80:
        print("  [Auto] Tank < 75% → Relay ON (pump)")
        lora.queue_downlink(name, bytes([0x01]))   # relay ON

    elif data['distance_cm'] < 35:
        print("  [Auto] Tank full → Relay OFF (stop pump)")
        lora.queue_downlink(name, bytes([0x00]))   # relay OFF

lora.listen(on_packet)
