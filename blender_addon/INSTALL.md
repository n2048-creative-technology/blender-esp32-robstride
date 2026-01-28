# Installation Guide - RobStride Streamer Blender Addon

## Quick Start (Recommended)

### 1. Run the Installation Script

```bash
cd /path/to/blender_addon
python3 install_addon.py
```

The script will:
- ✅ Auto-detect your Blender installation
- ✅ Install the addon in the correct location
- ✅ Install Python dependencies (pyserial)
- ✅ Provide next steps

### 2. Enable in Blender

1. Open Blender
2. Go to **Edit → Preferences → Add-ons**
3. Search for "RobStride"
4. Click the checkbox to enable
5. Look for the RobStride panel in the 3D View (press **N**)

---

## Manual Installation

If the automatic script doesn't work for you:

### Step 1: Install Dependencies

#### For Snap-Installed Blender:
```bash
/snap/bin/blender --python-expr "import subprocess, sys; subprocess.check_call([sys.executable, '-m', 'pip', 'install', 'pyserial'])"
```

#### For Standard Blender Installation:
Find your Blender Python executable:
```bash
# Linux/macOS
/path/to/blender/python/bin/python -m pip install pyserial

# Or if blender is in PATH
blender --python-expr "import subprocess, sys; subprocess.check_call([sys.executable, '-m', 'pip', 'install', 'pyserial'])"
```

#### For Windows:
```powershell
# Find blender.exe installation path
& "C:\Program Files\Blender Foundation\Blender\blender.exe" --python-expr "import subprocess, sys; subprocess.check_call([sys.executable, '-m', 'pip', 'install', 'pyserial'])"
```

### Step 2: Locate Addon Directory

The addon directory varies by OS:

**Linux:**
```bash
~/.config/blender/{VERSION}/scripts/addons/
```

**macOS:**
```bash
~/Library/Application Support/blender/{VERSION}/scripts/addons/
```

**Windows:**
```
%APPDATA%\Blender Foundation\Blender\{VERSION}\scripts\addons\
```

Replace `{VERSION}` with your Blender version (e.g., 4.0, 4.1, etc.)

### Step 3: Copy Addon Files

Copy the entire `robstride_streamer` folder to your addons directory:

**Linux/macOS:**
```bash
cp -r robstride_streamer ~/.config/blender/4.0/scripts/addons/
# or
cp -r robstride_streamer ~/Library/Application\ Support/blender/4.0/scripts/addons/
```

**Windows (PowerShell):**
```powershell
Copy-Item -Recurse robstride_streamer "$env:APPDATA\Blender Foundation\Blender\4.0\scripts\addons\"
```

### Step 4: Enable Addon

1. Launch Blender
2. Open **Preferences** (Edit → Preferences)
3. Go to the **Add-ons** tab
4. Search for "RobStride"
5. Check the box next to "RobStride Live Streamer"

---

## Verifying Installation

### Check Dependencies

In Blender's Python console:
```python
import serial
print("pyserial version:", serial.VERSION)
```

### Check Addon Registration

In Blender's console or Info panel, you should see:
```
registering class: ROBSTRIDE_OT_connect
registering class: ROBSTRIDE_OT_disconnect
... (other classes)
```

### Test Loopback Mode

1. Open the RobStride panel in the 3D View (press **N**)
2. Enable **Loopback** checkbox
3. Click **Connect** - should see "Loopback mode enabled" in console
4. Port dropdown should show "Loopback" option

---

## Troubleshooting

### Port Not Detected

If you only see "Loopback" in the port dropdown:

1. **Verify pyserial is installed:**
   ```bash
   /snap/bin/blender --python-expr "import serial; print(serial.__file__)"
   ```

2. **Reinstall pyserial:**
   ```bash
   /snap/bin/blender --python-expr "import subprocess, sys; subprocess.check_call([sys.executable, '-m', 'pip', 'install', '--force-reinstall', 'pyserial'])"
   ```

3. **Restart Blender** completely (close and reopen)

### Permission Denied on Linux

If you get permission errors accessing serial ports:

```bash
# Add your user to the dialout group
sudo usermod -a -G dialout $USER

# Apply new group permissions (log out and in, or use)
newgrp dialout
```

### Addon Doesn't Show Up

1. Check the addon was copied to the correct directory
2. Verify the folder is named exactly `robstride_streamer`
3. Ensure `__init__.py` exists in the addon folder
4. Restart Blender completely
5. Check **Preferences → Add-ons** tab (sometimes needs refresh)

### Import Errors

If you see import errors in the console:

1. Check all files are present:
   ```
   robstride_streamer/
   ├── __init__.py
   ├── ui.py
   ├── serial_link.py
   ├── protocol.py
   ├── fcurve_sampling.py
   ├── telemetry_view.py
   └── requirements.txt
   ```

2. Verify Blender's Python has all dependencies installed

---

## Next Steps

After successful installation, see **README.md** for:
- Usage instructions
- Motor configuration
- Streaming animation data
- Troubleshooting tips

---

## Support

For installation issues:

1. Check the error message in Blender's **Windows → Toggle System Console**
2. Verify system requirements (Blender 3.0+, Python 3.6+)
3. Ensure USB connection is properly established
4. Try the manual installation if the script fails
