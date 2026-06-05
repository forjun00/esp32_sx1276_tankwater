from lora_receiver import LoRaReceiver

lora = LoRaReceiver(host="0.0.0.0", port=1700)

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

    # ── Auto control example ──────────────────────────────────
    # Turn relay ON if tank is low (< 30cm from sensor = tank almost full)
    # Turn relay OFF if tank is high (> 100cm from sensor = tank low)
    if data['distance_cm'] > 100:
        print("  [Auto] Tank low → Relay ON (pump)")
        lora.queue_downlink(name, bytes([0x01]))   # relay ON

    elif data['distance_cm'] < 30:
        print("  [Auto] Tank full → Relay OFF (stop pump)")
        lora.queue_downlink(name, bytes([0x00]))   # relay OFF

lora.listen(on_packet)
