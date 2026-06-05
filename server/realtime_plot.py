"""
realtime_plot.py
----------------
Live updating graph — receives LoRa packets and plots in real time.

    python realtime_plot.py

Graph updates every time a new packet arrives.
Press Ctrl+C or close the window to stop.
"""

import csv
import os
import threading
import sys
from datetime import datetime
from collections import deque
import matplotlib.pyplot as plt
import matplotlib.animation as animation
import matplotlib.dates as mdates
from lora_receiver import LoRaReceiver

# ── Config ────────────────────────────────────────────────────────────────────
HOST       = "172.16.110.115"
PORT       = 1700
MAX_POINTS = 100          # how many packets to show on graph at once

DEVICES = [
    {
        "name":     "sensor_1",
        "dev_addr": 0x01020304,
        "nwk_skey": "112233445566778811223344556677 88",
        "app_skey": "AABBCCDDEEFFAABBAABBCCDDEEFFAABB",
    },
    # {
    #     "name":     "sensor_2",
    #     "dev_addr": 0x05060708,
    #     "nwk_skey": "...",
    #     "app_skey": "...",
    # },
]

COLORS   = ["#2196F3", "#F44336", "#4CAF50", "#FF9800", "#9C27B0"]
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
LOG_FILE   = os.path.join(SCRIPT_DIR, "data_log.csv")
FIELDS   = ["time", "device", "temperature", "humidity", "battery", "rssi", "snr", "fcnt"]

def init_csv():
    if not os.path.exists(LOG_FILE):
        with open(LOG_FILE, "w", newline="") as f:
            csv.DictWriter(f, fieldnames=FIELDS).writeheader()
        print(f"[Logger] Created {LOG_FILE}")
    else:
        print(f"[Logger] Appending to {LOG_FILE}")

def save_row(data: dict):
    row = {field: data.get(field, "") for field in FIELDS}
    with open(LOG_FILE, "a", newline="") as f:
        csv.DictWriter(f, fieldnames=FIELDS).writerow(row)
    print(f"[CSV] Saved → {LOG_FILE}")

init_csv()

# ── Data store — one deque per device per field ───────────────────────────────
data_store = {}

def get_device_store(name):
    if name not in data_store:
        data_store[name] = {
            "time":        deque(maxlen=MAX_POINTS),
            "temperature": deque(maxlen=MAX_POINTS),
            "humidity":    deque(maxlen=MAX_POINTS),
            "rssi":        deque(maxlen=MAX_POINTS),
        }
    return data_store[name]

# ── LoRa receiver thread ──────────────────────────────────────────────────────
def start_receiver():
    lora = LoRaReceiver(host=HOST, port=PORT)
    for d in DEVICES:
        lora.add_device(d["name"], d["dev_addr"], d["nwk_skey"], d["app_skey"])

    def on_packet(name, pkt):
        store = get_device_store(name)
        store["time"].append(datetime.strptime(pkt["time"], "%Y-%m-%d %H:%M:%S"))
        store["temperature"].append(pkt.get("temperature"))
        store["humidity"].append(pkt.get("humidity"))
        store["rssi"].append(pkt.get("rssi"))

        # Save to CSV
        pkt["device"] = name
        save_row(pkt)

        print(f"[{pkt['time']}] {name} → "
              f"Temp: {pkt.get('temperature')}°C  "
              f"Humi: {pkt.get('humidity')}%  "
              f"RSSI: {pkt.get('rssi')} dBm")

    lora.listen(on_packet)

# Start receiver in background thread
t = threading.Thread(target=start_receiver, daemon=True)
t.start()

# ── Live graph ────────────────────────────────────────────────────────────────
fig, axes = plt.subplots(3, 1, figsize=(13, 8), sharex=True)
fig.suptitle("LoRa Live Data", fontsize=13, fontweight="bold")

axes[0].set_ylabel("Temperature (°C)")
axes[1].set_ylabel("Humidity (%)")
axes[2].set_ylabel("RSSI (dBm)")
axes[2].set_xlabel("Time")

for ax in axes:
    ax.grid(True, alpha=0.3)

lines = {}   # { "sensor_1_temp": Line2D, ... }

def update(frame):
    for i, (name, store) in enumerate(data_store.items()):
        color = COLORS[i % len(COLORS)]
        times = list(store["time"])

        if not times:
            continue

        # Temperature
        key = f"{name}_temp"
        if key not in lines:
            lines[key], = axes[0].plot([], [], marker="o", markersize=3,
                                       linewidth=1.5, label=name, color=color)
            axes[0].legend(loc="upper right", fontsize=8)
        lines[key].set_data(times, list(store["temperature"]))

        # Humidity
        key = f"{name}_humi"
        if key not in lines:
            lines[key], = axes[1].plot([], [], marker="o", markersize=3,
                                       linewidth=1.5, label=name, color=color)
            axes[1].legend(loc="upper right", fontsize=8)
        lines[key].set_data(times, list(store["humidity"]))

        # RSSI
        key = f"{name}_rssi"
        if key not in lines:
            lines[key], = axes[2].plot([], [], marker="o", markersize=3,
                                       linewidth=1.5, label=name, color=color)
            axes[2].legend(loc="upper right", fontsize=8)
        lines[key].set_data(times, list(store["rssi"]))

    # Rescale all axes
    for ax in axes:
        ax.relim()
        ax.autoscale_view()

    # Format X axis
    axes[2].xaxis.set_major_formatter(mdates.DateFormatter("%H:%M:%S"))
    fig.autofmt_xdate()

    return list(lines.values())

ani = animation.FuncAnimation(fig, update, interval=1000, blit=False)

plt.tight_layout()
print("[Plot] Window open — waiting for packets...")
plt.show()
