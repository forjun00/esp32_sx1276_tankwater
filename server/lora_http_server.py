"""
lora_http_server.py
-------------------
HTTP-based LoRa uplink receiver for Dragino LPS8N (HTTP forwarding mode).

Setup on gateway:
    LNS → Packet Forwarder → Server type: HTTP
    Uplink URL   : http://<your-pc-ip>:8080/uplink
    Downlink URL : http://<your-pc-ip>:8080/downlink  (gateway polls this)

Usage:
    from lora_http_server import LoRaHTTPServer

    srv = LoRaHTTPServer(host="0.0.0.0", port=8080)
    srv.add_device("tank_1", dev_addr=0x215C5DA5,
                   nwk_skey="546F31E8...", app_skey="E0A48DBD...")

    def on_packet(name, data):
        print(name, data)

    srv.listen(on_packet)       # blocking — runs Flask server
    # or
    srv.listen_background(on_packet)   # non-blocking thread

    # Queue downlink (delivered when gateway polls /downlink)
    srv.queue_downlink("tank_1", bytes([0x01]))   # relay ON
    srv.queue_downlink("tank_1", bytes([0x00]))   # relay OFF

    # TX offset (server time-slotting for many nodes)
    srv.assign_offsets_evenly(interval_sec=120)

NOTE:
    HTTP mode does not have a guaranteed RX1 window like UDP mode.
    Downlinks are delivered when the gateway polls /downlink endpoint.
    For real-time relay control, UDP mode (lora_receiver.py) is preferred.
"""

import json
import base64
import struct
import threading
from datetime import datetime, timezone, timedelta
from Crypto.Cipher import AES
from Crypto.Hash  import CMAC

try:
    from flask import Flask, request, jsonify
except ImportError:
    raise ImportError("Flask required: pip install flask")

BKK = timezone(timedelta(hours=7))   # Bangkok UTC+7


class LoRaHTTPServer:
    def __init__(self, host: str = "0.0.0.0", port: int = 8080):
        self.host              = host
        self.port              = port
        self._devices          = {}   # keyed by "DEVADDR" uppercase hex
        self._assigned_offsets = {}   # name → assigned offset_sec
        self._callback         = None
        self._app              = self._build_app()

    # ── Register devices ──────────────────────────────────────────────────────

    def add_device(self, name: str, dev_addr: int,
                   nwk_skey: str, app_skey: str):
        """
        Register a LoRa device.
        name     : label e.g. "tank_1"
        dev_addr : e.g. 0x215C5DA5
        nwk_skey : hex string (32 chars)
        app_skey : hex string (32 chars)
        """
        key = f"{dev_addr:08X}".upper()
        self._devices[key] = {
            "name":             name,
            "dev_addr":         dev_addr,
            "nwk_skey":         nwk_skey.replace(" ", ""),
            "app_skey":         app_skey.replace(" ", ""),
            "fcnt_down":        0,
            "pending_downlink": None,   # (payload_bytes, fport) or None
        }
        print(f"[LoRaHTTP] Registered '{name}' → DevAddr {dev_addr:08X}")

    # ── Downlink ──────────────────────────────────────────────────────────────

    def queue_downlink(self, name: str, payload: bytes, fport: int = 2):
        """
        Queue a downlink command.
        Delivered when gateway polls the /downlink endpoint.

        Commands (match firmware handleDownlink):
            0x01        = Relay ON
            0x00        = Relay OFF
            0x02, sec   = Set TX offset (seconds)
        """
        for dev in self._devices.values():
            if dev["name"] == name:
                dev["pending_downlink"] = (payload, fport)
                print(f"[Downlink] Queued '{name}' → {payload.hex().upper()}")
                return
        print(f"[Downlink] Device '{name}' not found")

    # ── TX offset assignment ──────────────────────────────────────────────────

    def assign_tx_offset(self, name: str, offset_sec: int):
        """Assign TX time-slot to a node (command 0x02)."""
        offset_sec = max(0, min(255, int(offset_sec)))
        self._assigned_offsets[name] = offset_sec
        self.queue_downlink(name, bytes([0x02, offset_sec]), fport=2)
        print(f"[Offset] Assigned '{name}' → {offset_sec}s slot")

    def assign_offsets_evenly(self, interval_sec: int = 120):
        """Spread all nodes evenly across the TX interval."""
        names = [dev["name"] for dev in self._devices.values()]
        count = len(names)
        if count == 0:
            print("[Offset] No devices registered"); return
        print(f"[Offset] Spreading {count} nodes @ {interval_sec}s interval:")
        for i, name in enumerate(names):
            offset = int(i * interval_sec / count)
            self.assign_tx_offset(name, offset)

    # ── Listen ────────────────────────────────────────────────────────────────

    def listen(self, callback):
        """
        Start HTTP server (blocking).
        callback(name, data_dict) called on every valid uplink.
        """
        self._callback = callback
        print(f"[LoRaHTTP] Listening on http://{self.host}:{self.port}")
        print(f"[LoRaHTTP] Uplink   → POST http://<this-ip>:{self.port}/uplink")
        print(f"[LoRaHTTP] Downlink ← GET  http://<this-ip>:{self.port}/downlink")
        self._app.run(host=self.host, port=self.port, debug=False)

    def listen_background(self, callback):
        """
        Start HTTP server in background thread (non-blocking).
        """
        self._callback = callback
        t = threading.Thread(
            target=lambda: self._app.run(
                host=self.host, port=self.port, debug=False, use_reloader=False),
            daemon=True)
        t.start()
        print(f"[LoRaHTTP] Server running in background on port {self.port}")

    # ── Flask app ─────────────────────────────────────────────────────────────

    def _build_app(self):
        app = Flask(__name__)
        app.logger.disabled = True

        import logging
        log = logging.getLogger('werkzeug')
        log.setLevel(logging.ERROR)

        @app.route('/uplink', methods=['POST'])
        def uplink():
            """Receive uplink from Dragino gateway."""
            try:
                body = request.get_json(force=True, silent=True) or {}
            except Exception:
                return 'Bad JSON', 400

            # Dragino HTTP format: {"rxpk": [...]}
            for rxpk in body.get('rxpk', []):
                result = self._process_rxpk(rxpk)
                if result and self._callback:
                    name, data = result
                    try:
                        self._callback(name, data)
                    except Exception as e:
                        print(f"[LoRaHTTP] Callback error: {e}")

            return 'OK', 200

        @app.route('/downlink', methods=['GET'])
        def downlink():
            """
            Gateway polls this to get pending downlinks.
            Returns JSON with txpk or empty if nothing pending.
            """
            for dev in self._devices.values():
                if dev["pending_downlink"] is not None:
                    payload, fport = dev["pending_downlink"]
                    frame = self._build_downlink_frame(dev, payload, fport)
                    if frame:
                        dev["pending_downlink"] = None
                        dev["fcnt_down"] += 1
                        resp = {
                            "txpk": {
                                "imme": True,
                                "freq": 923.2,
                                "rfch": 0,
                                "powe": 14,
                                "modu": "LORA",
                                "datr": "SF9BW125",
                                "codr": "4/5",
                                "ipol": True,
                                "size": len(frame),
                                "data": base64.b64encode(frame).decode(),
                            }
                        }
                        print(f"[Downlink] Sending to '{dev['name']}' → "
                              f"{payload.hex().upper()}")
                        return jsonify(resp), 200
            return jsonify({}), 200

        @app.route('/status', methods=['GET'])
        def status():
            """Health check + device list."""
            devices = []
            for dev in self._devices.values():
                devices.append({
                    "name":     dev["name"],
                    "dev_addr": f"{dev['dev_addr']:08X}",
                    "fcnt_down": dev["fcnt_down"],
                    "pending":  dev["pending_downlink"] is not None,
                })
            return jsonify({"status": "ok", "devices": devices}), 200

        return app

    # ── Packet processing ─────────────────────────────────────────────────────

    def _process_rxpk(self, rx: dict):
        data_b64 = rx.get('data', '')
        if not data_b64:
            return None

        frame = self._parse_frame(data_b64)
        if frame is None:
            return None

        device = self._devices.get(frame['dev_addr'])
        if device is None:
            return None

        if not frame['enc_payload']:
            return None

        decrypted = self._decrypt_payload(
            device['app_skey'], device['dev_addr'],
            frame['fcnt'], frame['enc_payload'], direction=0)
        if decrypted is None:
            return None

        sensors = self._decode_sensors(decrypted)
        if sensors is None:
            return None

        sensors.update({
            'rssi':     rx.get('rssi', 0),
            'snr':      rx.get('lsnr', 0.0),
            'freq':     rx.get('freq', 0.0),
            'sf':       rx.get('datr', ''),
            'fcnt':     frame['fcnt'],
            'dev_addr': frame['dev_addr'],
            'time':     datetime.now(BKK).strftime('%Y-%m-%d %H:%M:%S'),
        })

        # ── Auto-retry TX offset if not confirmed ─────────────────────────────
        name     = device['name']
        assigned = self._assigned_offsets.get(name)
        if assigned is not None:
            current = sensors.get('tx_offset', 0)
            if current != assigned:
                if device["pending_downlink"] is None:
                    device["pending_downlink"] = (bytes([0x02, assigned & 0xFF]), 2)
                    print(f"[Offset] {name}: current={current}s want={assigned}s → retry")
            else:
                print(f"[Offset] {name}: offset {assigned}s confirmed ✅")

        return (name, sensors)

    # ── Downlink frame builder ────────────────────────────────────────────────

    def _build_downlink_frame(self, device: dict, payload: bytes,
                               fport: int) -> bytes | None:
        try:
            dev_addr  = device["dev_addr"]
            fcnt_down = device["fcnt_down"]
            nwk_key   = bytes.fromhex(device["nwk_skey"])

            enc_payload = self._decrypt_payload(
                device["app_skey"], dev_addr, fcnt_down, payload, direction=1)

            mhdr  = bytes([0x60])
            fhdr  = struct.pack('<I', dev_addr)
            fhdr += bytes([0x00])
            fhdr += struct.pack('<H', fcnt_down)
            frame = mhdr + fhdr + bytes([fport]) + enc_payload

            mic = self._calculate_mic(nwk_key, frame, dev_addr, fcnt_down, direction=1)
            return frame + mic

        except Exception as e:
            print(f"[Downlink] Frame build error: {e}")
            return None

    # ── Crypto ────────────────────────────────────────────────────────────────

    @staticmethod
    def _decrypt_payload(skey_hex: str, dev_addr: int,
                          fcnt: int, payload: bytes,
                          direction: int = 0) -> bytes | None:
        try:
            key = bytes.fromhex(skey_hex.replace(" ", ""))
            ks  = bytearray()
            for i in range(1, (len(payload) // 16) + 2):
                a = bytearray(16)
                a[0]  = 0x01
                a[5]  = direction
                a[6]  = dev_addr & 0xFF
                a[7]  = (dev_addr >> 8)  & 0xFF
                a[8]  = (dev_addr >> 16) & 0xFF
                a[9]  = (dev_addr >> 24) & 0xFF
                a[10] = fcnt & 0xFF
                a[11] = (fcnt >> 8) & 0xFF
                a[15] = i
                ks.extend(AES.new(key, AES.MODE_ECB).encrypt(bytes(a)))
            return bytes(payload[j] ^ ks[j] for j in range(len(payload)))
        except Exception:
            return None

    @staticmethod
    def _calculate_mic(nwk_key: bytes, frame: bytes,
                        dev_addr: int, fcnt: int,
                        direction: int = 0) -> bytes:
        b0 = bytearray(16)
        b0[0]  = 0x49
        b0[5]  = direction
        b0[6]  = dev_addr & 0xFF
        b0[7]  = (dev_addr >> 8)  & 0xFF
        b0[8]  = (dev_addr >> 16) & 0xFF
        b0[9]  = (dev_addr >> 24) & 0xFF
        b0[10] = fcnt & 0xFF
        b0[11] = (fcnt >> 8) & 0xFF
        b0[15] = len(frame)
        cobj   = CMAC.new(nwk_key, ciphermod=AES)
        cobj.update(bytes(b0) + frame)
        return cobj.digest()[:4]

    @staticmethod
    def _parse_frame(data_b64: str) -> dict | None:
        try:
            frame = base64.b64decode(data_b64)
            if len(frame) < 12:
                return None
            return {
                'dev_addr':    f"{struct.unpack_from('<I', frame, 1)[0]:08X}",
                'fcnt':        struct.unpack_from('<H', frame, 6)[0],
                'fport':       frame[8] if len(frame) > 8 else None,
                'enc_payload': frame[9:-4] if len(frame) > 12 else None,
                'mic':         frame[-4:].hex().upper(),
            }
        except Exception:
            return None

    @staticmethod
    def _decode_sensors(data: bytes) -> dict | None:
        if not data or len(data) < 4:
            return None
        # Tank water level payload (5 bytes):
        #   [0-1] distance cm     (uint16 big-endian)
        #   [2-3] voltage×100     (uint16 big-endian)
        #   [4]   tx_offset (sec) (uint8)
        return {
            'distance_cm': ((data[0] << 8) | data[1]),
            'voltage':     ((data[2] << 8) | data[3]) / 100.0,
            'tx_offset':   data[4] if len(data) >= 5 else 0,
        }


# ── Example usage ─────────────────────────────────────────────────────────────

if __name__ == "__main__":
    srv = LoRaHTTPServer(host="0.0.0.0", port=8080)

    srv.add_device("tank_1",
        dev_addr = 0x215C5DA5,
        nwk_skey = "546F31E8BEFDB9EBD8F3B0FE34090755",
        app_skey = "E0A48DBD837993F70B2B3886BBC2922E",
    )

    def on_packet(name, data):
        print(f"\n[{name}] {data['time']}")
        print(f"  Distance : {data['distance_cm']} cm")
        print(f"  Battery  : {data['voltage']} V")
        print(f"  RSSI     : {data['rssi']} dBm")
        print(f"  FCnt     : {data['fcnt']}")
        print(f"  TX Offset: {data['tx_offset']} s")

        # Auto relay control
        if data['distance_cm'] > 200:
            print("  [Auto] Tank low → Relay ON")
            srv.queue_downlink(name, bytes([0x01]))
        elif data['distance_cm'] < 35:
            print("  [Auto] Tank full → Relay OFF")
            srv.queue_downlink(name, bytes([0x00]))

    srv.listen(on_packet)
