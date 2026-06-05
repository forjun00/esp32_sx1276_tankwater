"""
lora_receiver.py
----------------
Multi-device LoRa packet receiver with downlink support.

Usage:
    from lora_receiver import LoRaReceiver

    lora = LoRaReceiver(host="0.0.0.0", port=1700)
    lora.add_device("tank_1", dev_addr=0x01020304,
                    nwk_skey="1122...", app_skey="AABB...")

    # Receive uplinks
    def on_packet(name, data):
        print(name, data)

    lora.listen(on_packet)

    # Send downlink (queued — delivered on device's next uplink)
    lora.queue_downlink("tank_1", bytes([0x01]))   # relay ON
    lora.queue_downlink("tank_1", bytes([0x00]))   # relay OFF
"""

import json
import socket
import base64
import struct
from datetime import datetime, timezone, timedelta
from Crypto.Cipher import AES
from Crypto.Hash  import CMAC

BKK        = timezone(timedelta(hours=7))   # Bangkok UTC+7
_PUSH_DATA = 0x00
_PULL_DATA = 0x02
_PUSH_ACK  = 0x01
_PULL_ACK  = 0x04
_PULL_RESP = 0x03


class LoRaReceiver:
    def __init__(self, host: str = "0.0.0.0", port: int = 1700):
        self.host              = host
        self.port              = port
        self._devices          = {}   # keyed by "DEVADDR" uppercase hex
        self._sock             = None
        self._gw_addr          = None # gateway UDP address (from PULL_DATA)
        self._gw_token         = 0    # last PULL_DATA token
        self._assigned_offsets = {}   # name → assigned offset_sec (int)

    # ── Register devices ──────────────────────────────────────────────────────

    def add_device(self, name: str, dev_addr: int,
                   nwk_skey: str, app_skey: str):
        """
        Register a device.
        name     : label e.g. "tank_1"
        dev_addr : e.g. 0x01020304
        nwk_skey : hex string e.g. "1122334455667788..."
        app_skey : hex string e.g. "AABBCCDD..."
        """
        key = f"{dev_addr:08X}".upper()
        self._devices[key] = {
            "name":             name,
            "dev_addr":         dev_addr,
            "nwk_skey":         nwk_skey.replace(" ", ""),
            "app_skey":         app_skey.replace(" ", ""),
            "fcnt_down":        0,
            "pending_downlink": None,   # (payload_bytes, fport) or None
            "last_tmst":        None,   # last uplink timestamp (us)
            "last_freq":        923.2,  # last uplink frequency
            "last_datr":        "SF7BW125",
        }
        print(f"[LoRaReceiver] Registered '{name}' → DevAddr {dev_addr:08X}")

    # ── Downlink ──────────────────────────────────────────────────────────────

    # ── TX offset assignment ──────────────────────────────────────────────────

    def assign_tx_offset(self, name: str, offset_sec: int):
        """
        Assign a TX time-slot offset to a node via downlink command 0x02.
        The node will start transmitting at offset_sec seconds into each
        lora_interval cycle. Automatically retried until node confirms.

        offset_sec : 0–255 seconds
        """
        offset_sec = max(0, min(255, int(offset_sec)))
        self._assigned_offsets[name] = offset_sec
        self.queue_downlink(name, bytes([0x02, offset_sec]), fport=2)
        print(f"[Offset] Assigned '{name}' → {offset_sec}s slot")

    def assign_offsets_evenly(self, interval_sec: int = 120):
        """
        Spread all registered nodes evenly across the TX interval.
        Call once after all devices are registered.

        Example with 3 nodes at 120s interval:
            Node 0 → 0s, Node 1 → 40s, Node 2 → 80s
        """
        names = list({dev['name']: dev for dev in self._devices.values()}.keys())
        count = len(names)
        if count == 0:
            print("[Offset] No devices registered")
            return
        print(f"[Offset] Assigning slots for {count} nodes @ {interval_sec}s interval:")
        for i, name in enumerate(names):
            offset = int(i * interval_sec / count)
            self.assign_tx_offset(name, offset)

    def queue_downlink(self, name: str, payload: bytes, fport: int = 2):
        """
        Queue a downlink command for a device.
        Delivered automatically on the device's next uplink (Class A RX window).

        name    : device name used in add_device()
        payload : bytes to send  e.g. bytes([0x01])
        fport   : LoRaWAN port 1-223 (default 2)

        Command bytes (match firmware handleDownlink):
            0x01 = Relay ON
            0x00 = Relay OFF
        """
        for key, dev in self._devices.items():
            if dev["name"] == name:
                dev["pending_downlink"] = (payload, fport)
                print(f"[Downlink] Queued for '{name}' — will send on next uplink  "
                      f"payload={payload.hex().upper()}")
                return
        print(f"[Downlink] Device '{name}' not found")

    def send_now(self, name: str, payload: bytes, fport: int = 2):
        """
        Send downlink immediately using last known timing.
        Device must have sent at least one uplink first.
        """
        for key, dev in self._devices.items():
            if dev["name"] == name:
                if dev["last_tmst"] is None:
                    print(f"[Downlink] No uplink received yet from '{name}' — use queue_downlink()")
                    return
                self._send_downlink(dev, payload, fport)
                return
        print(f"[Downlink] Device '{name}' not found")

    # ── Listen / Receive ──────────────────────────────────────────────────────

    def receive(self):
        """Block until one valid uplink. Returns (name, data_dict)."""
        self._open_socket()
        while True:
            result = self._wait_for_packet()
            if result is not None:
                return result

    def listen(self, callback):
        """
        Run forever. Call callback(name, data) on every valid uplink.
        Ctrl+C to stop.
        """
        self._open_socket()
        print(f"[LoRaReceiver] Listening on {self.host}:{self.port} ...")
        try:
            while True:
                result = self._wait_for_packet()
                if result is not None:
                    name, data = result
                    callback(name, data)
        except KeyboardInterrupt:
            print("\n[LoRaReceiver] Stopped.")
        finally:
            self._close_socket()

    def listen_json(self, callback, indent=None):
        """Same as listen() but callback receives (name, json_string)."""
        def _wrap(name, data):
            data["device"] = name
            callback(name, json.dumps(data, indent=indent))
        self.listen(_wrap)

    def receive_json(self, indent=None):
        """Block until one packet. Returns JSON string."""
        name, data = self.receive()
        data["device"] = name
        return json.dumps(data, indent=indent)

    def close(self):
        self._close_socket()

    # ── Internal — socket ─────────────────────────────────────────────────────

    def _open_socket(self):
        if self._sock is None:
            self._sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            self._sock.bind((self.host, self.port))

    def _close_socket(self):
        if self._sock:
            self._sock.close()
            self._sock = None

    def _wait_for_packet(self):
        raw, addr = self._sock.recvfrom(4096)
        if len(raw) < 4:
            return None

        pkt_type = raw[3]
        token    = struct.unpack_from('>H', raw, 1)[0]

        # Track gateway downlink address (PULL_DATA)
        if pkt_type == _PULL_DATA:
            self._gw_addr  = addr
            self._gw_token = token
            self._sock.sendto(struct.pack('>BHB', 0x02, token, _PULL_ACK), addr)
            return None

        # ACK push data
        if pkt_type == _PUSH_DATA:
            self._sock.sendto(struct.pack('>BHB', 0x02, token, _PUSH_ACK), addr)
        else:
            return None

        if len(raw) <= 12:
            return None

        try:
            pkt = json.loads(raw[12:].decode('utf-8', errors='ignore'))
        except Exception:
            return None

        if 'rxpk' not in pkt:
            return None

        for rx in pkt['rxpk']:
            result = self._process_rxpk(rx)
            if result is not None:
                return result

        return None

    # ── Internal — uplink processing ──────────────────────────────────────────

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

        # Save radio metadata for downlink timing
        device["last_tmst"] = rx.get('tmst')
        device["last_freq"] = rx.get('freq', 923.2)
        device["last_datr"] = rx.get('datr', 'SF7BW125')

        sensors.update({
            'rssi':     rx.get('rssi', 0),
            'snr':      rx.get('lsnr', 0.0),
            'freq':     rx.get('freq', 0.0),
            'sf':       rx.get('datr', ''),
            'fcnt':     frame['fcnt'],
            'dev_addr': frame['dev_addr'],
            'time':     datetime.now(BKK).strftime('%Y-%m-%d %H:%M:%S'),
        })

        # ── Auto-retry TX offset if not yet confirmed ────────────────────────
        name     = device['name']
        assigned = self._assigned_offsets.get(name)
        if assigned is not None:
            current = sensors.get('tx_offset', 0)
            if current != assigned:
                # Node hasn't applied the offset yet — re-queue downlink
                # (only if no other command is already pending)
                if device["pending_downlink"] is None:
                    device["pending_downlink"] = (bytes([0x02, assigned & 0xFF]), 2)
                    print(f"[Offset] {name}: current={current}s want={assigned}s → retry")
            else:
                print(f"[Offset] {name}: offset {assigned}s confirmed ✅")

        # ── Send queued downlink in RX1 window ───────────────────────────────
        if device["pending_downlink"] is not None:
            payload, fport = device["pending_downlink"]
            self._send_downlink(device, payload, fport)
            device["pending_downlink"] = None

        return (device['name'], sensors)

    # ── Internal — downlink ───────────────────────────────────────────────────

    def _send_downlink(self, device: dict, payload: bytes, fport: int):
        if self._gw_addr is None:
            print("[Downlink] Gateway address unknown — waiting for PULL_DATA")
            return

        # Build LoRaWAN downlink frame
        frame = self._build_downlink_frame(device, payload, fport)
        if frame is None:
            return

        # RX1 window: tmst + 1,000,000 us (1 second after uplink)
        tmst = device["last_tmst"]
        if tmst is not None:
            tx_tmst = (tmst + 1_000_000) & 0xFFFFFFFF
        else:
            tx_tmst = 0

        txpk = {
            "tmst": tx_tmst,
            "freq": device["last_freq"],     # RX1 = same freq as uplink
            "rfch": 0,
            "powe": 14,
            "modu": "LORA",
            "datr": device["last_datr"],     # RX1 = same DR as uplink
            "codr": "4/5",
            "ipol": True,                    # downlink uses inverted polarity
            "size": len(frame),
            "data": base64.b64encode(frame).decode(),
        }

        pull_resp = struct.pack('>BHB', 0x02, self._gw_token, _PULL_RESP)
        pull_resp += json.dumps({"txpk": txpk}).encode()

        self._sock.sendto(pull_resp, self._gw_addr)
        device["fcnt_down"] += 1

        print(f"[Downlink] Sent to '{device['name']}'  "
              f"payload={payload.hex().upper()}  "
              f"fcnt_down={device['fcnt_down']-1}  "
              f"tmst={tx_tmst}")

    def _build_downlink_frame(self, device: dict, payload: bytes,
                               fport: int) -> bytes | None:
        try:
            dev_addr  = device["dev_addr"]
            fcnt_down = device["fcnt_down"]
            nwk_key   = bytes.fromhex(device["nwk_skey"])
            app_key   = bytes.fromhex(device["app_skey"])

            # Encrypt payload with AppSKey (direction=1 for downlink)
            enc_payload = self._decrypt_payload(
                device["app_skey"], dev_addr, fcnt_down, payload, direction=1)

            # Build frame (without MIC)
            mhdr  = bytes([0x60])                           # Unconfirmed Data Down
            fhdr  = struct.pack('<I', dev_addr)             # DevAddr LE
            fhdr += bytes([0x00])                           # FCtrl
            fhdr += struct.pack('<H', fcnt_down)            # FCnt LE
            frame = mhdr + fhdr + bytes([fport]) + enc_payload

            # Calculate MIC
            mic = self._calculate_mic(nwk_key, frame, dev_addr, fcnt_down, direction=1)
            return frame + mic

        except Exception as e:
            print(f"[Downlink] Frame build error: {e}")
            return None

    # ── Internal — crypto ─────────────────────────────────────────────────────

    @staticmethod
    def _decrypt_payload(skey_hex: str, dev_addr: int,
                          fcnt: int, payload: bytes,
                          direction: int = 0) -> bytes | None:
        """
        Encrypt/decrypt LoRaWAN payload.
        direction: 0 = uplink, 1 = downlink
        (Same function works for both — AES CTR is symmetric)
        """
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
        """Calculate LoRaWAN MIC using AES-CMAC."""
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

        cobj = CMAC.new(nwk_key, ciphermod=AES)
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
        #   [4]   tx_offset (sec) (uint8) — node reports current assigned offset
        return {
            'distance_cm': ((data[0] << 8) | data[1]),
            'voltage':     ((data[2] << 8) | data[3]) / 100.0,
            'tx_offset':   data[4] if len(data) >= 5 else 0,
        }
