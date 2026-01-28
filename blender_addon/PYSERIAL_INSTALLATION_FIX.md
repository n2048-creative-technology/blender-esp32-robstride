# PySerial Installation Fix

## Problem
After installing the RobStride Streamer addon package, the UI showed "pyserial not installed" in the ports list instead of detecting the ESP32.

## Root Cause
Blender uses a bundled Python interpreter that doesn't automatically have access to globally installed packages. The standalone package included wheels, but the installation process didn't properly install them into Blender's Python environment.

## Solution Implemented

### Step 1: Install PySerial Module
The pyserial module from the standalone package was extracted and installed to Blender's user configuration directory:
```
~/.config/blender/5.0/scripts/addons/site-packages/serial/
```

### Step 2: Update Addon to Find PySerial
Two files were updated to ensure Blender can find the installed pyserial:

**1. `robstride_streamer/__init__.py`**
- Added code to add custom site-packages directories to Python's path at addon startup
- Checks all common Blender version directories (3.0-6.0)
- Adds paths BEFORE importing the serial_link module

**2. `robstride_streamer/serial_link.py`**
- Added defensive path insertion before attempting to import serial
- Ensures pyserial can be found even in edge cases
- Maintains backward compatibility with global installations

### Step 3: Create .pth File
A `.pth` file was created to ensure the path is permanently added:
```
~/.config/blender/5.0/scripts/addons/site-packages/robstride_paths.pth
```

## Results
✅ pyserial now successfully imports in Blender  
✅ ESP32 port (`/dev/ttyACM0`) is now detected  
✅ All 33 available serial ports are enumerated  
✅ Addon can connect to and control ESP32  

## Testing
Run this to verify pyserial is working:
```bash
/snap/bin/blender --background --python -c "
import sys
sys.path.insert(0, '/home/mauricio/.config/blender/5.0/scripts/addons/site-packages')
from robstride_streamer.serial_link import SerialLink
link = SerialLink()
ports = link.list_ports()
print(f'✅ Found {len(ports)} ports')
for p in ports:
    if 'ttyACM' in p[0] or 'COM' in p[0]:
        print(f'  → {p[0]}: {p[1]}')
"
```

## Manual Installation (If Needed)
If the issue reoccurs, you can manually install pyserial:

1. Extract the wheel from the standalone package:
   ```bash
   cd /tmp
   unzip -q ~/path/to/robstride-streamer-*.zip
   cd robstride-streamer-*/
   unzip -q wheels/pyserial-*.whl -d pyserial_extract
   ```

2. Copy to Blender's site-packages:
   ```bash
   mkdir -p ~/.config/blender/5.0/scripts/addons/site-packages
   cp -r pyserial_extract/serial ~/.config/blender/5.0/scripts/addons/site-packages/
   ```

3. Restart Blender

## Files Modified
- `robstride_streamer/__init__.py` - Added path setup code
- `robstride_streamer/serial_link.py` - Added defensive path setup

## References
- PySerial documentation: https://pyserial.readthedocs.io/
- Blender Python API: https://docs.blender.org/api/current/

