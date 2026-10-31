#!/usr/bin/env python3
import subprocess
import time
import sys

addr = "28:84:85:A6:CF:AE"

# The CCCD (Client Characteristic Configuration Descriptor) for notifications
# is typically at handle + 1, so 0x002b for our characteristic at 0x002a
cccd_handle = "002b"

print(f"Enabling notifications on {addr}...")
print(f"Writing to CCCD handle {cccd_handle}...")

# Write 0x0100 (enable notifications) to CCCD
result = subprocess.run(['gatttool', '-b', addr, '-t', 'random', '--char-write-req', 
                        '--handle', '0x002b', '--value', '0100'],
                       capture_output=True, text=True, timeout=10)
print("Write result:", result.stdout)
if result.stderr:
    print("Error:", result.stderr)

print("\nListening for notifications for 10 seconds...")
result = subprocess.run(['gatttool', '-b', addr, '-t', 'random', '--listen'],
                       capture_output=True, text=True, timeout=10)
print(result.stdout)
if result.stderr:
    print("Error:", result.stderr)
