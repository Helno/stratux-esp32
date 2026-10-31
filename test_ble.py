#!/usr/bin/env python3
import subprocess
import time

# Connect to the device
addr = "28:84:85:A6:CF:AE"

# Get primary services
print("Getting primary services...")
result = subprocess.run(['gatttool', '-b', addr, '--primary'], 
                       capture_output=True, text=True, timeout=5)
print(result.stdout)
if result.returncode != 0:
    print("Error:", result.stderr)
    
# Try to get characteristics
print("\nGetting characteristics...")
result = subprocess.run(['gatttool', '-b', addr, '--characteristics'], 
                       capture_output=True, text=True, timeout=5)
print(result.stdout)
if result.returncode != 0:
    print("Error:", result.stderr)
