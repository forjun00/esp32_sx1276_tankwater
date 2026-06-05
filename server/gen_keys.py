"""
LoRaWAN ABP Key Generator
Generates DevAddr, NwkSKey, AppSKey and prints them
in both ESP32 (C++ array) and Python (hex string) formats.
"""

import os
import random

def gen_key_16():
    """Generate 16 random bytes (128-bit AES key)"""
    return os.urandom(16)

def gen_dev_addr():
    """Generate 4 random bytes for DevAddr"""
    return os.urandom(4)

def to_cpp_array(key_bytes, name):
    """Format bytes as C++ uint8_t array"""
    hex_vals = [f"0x{b:02X}" for b in key_bytes]
    half = len(hex_vals) // 2
    row1 = ", ".join(hex_vals[:half])
    row2 = ", ".join(hex_vals[half:])
    pad = " " * (len(name) + 14)
    return f"uint8_t {name}[] = {{ {row1},\n{pad}{row2} }};"

def to_python_str(key_bytes):
    """Format bytes as Python hex string"""
    return key_bytes.hex().upper()

def to_dev_addr_cpp(addr_bytes):
    val = int.from_bytes(addr_bytes, 'big')
    return f"uint32_t devAddr = 0x{val:08X};"

def to_dev_addr_py(addr_bytes):
    val = int.from_bytes(addr_bytes, 'big')
    return f"0x{val:08X}"

# ── Generate ──────────────────────────────────────────────────────────────────
dev_addr = gen_dev_addr()
nwk_skey = gen_key_16()
app_skey = gen_key_16()

# ── Print ─────────────────────────────────────────────────────────────────────
print("=" * 60)
print("  LoRaWAN ABP Key Generator")
print("=" * 60)

print("\n-- ESP32 (paste into .ino) ----------------------------------\n")
print(to_dev_addr_cpp(dev_addr))
print(to_cpp_array(nwk_skey, "nwkSKey"))
print(to_cpp_array(app_skey, "appSKey"))

print("\n-- Python (paste into gateway_udp_server.py) ----------------\n")
print(f'DEV_ADDR  = {to_dev_addr_py(dev_addr)}')
print(f'NWK_SKEY  = "{to_python_str(nwk_skey)}"')
print(f'APP_SKEY  = "{to_python_str(app_skey)}"')

print("\n" + "=" * 60)
