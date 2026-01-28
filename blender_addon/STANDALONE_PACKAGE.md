# RobStride Streamer - Single Standalone Installation Package

## ✅ What You Have

One complete, self-contained .zip file with **everything needed** to install the addon:

```
robstride-streamer-0.1.1-standalone.zip (106 KB)
```

### ✨ What's Inside:

✓ **Complete Addon Source Code** - All Python files  
✓ **Pre-built Wheels** - pyserial and all dependencies (no internet needed!)  
✓ **Smart Installation Script** - Works offline, handles everything  
✓ **Complete Documentation** - README, INSTALL guide, LICENSE  
✓ **Manifest File** - Package metadata and file listing  

---

## 🚀 Installation (3 Steps)

### Step 1: Extract the Package
```bash
unzip robstride-streamer-0.1.1-standalone.zip
cd robstride-streamer-0.1.1-standalone
```

### Step 2: Run the Installer
```bash
python3 install.py
```

The installer will:
- 🔍 Find your Blender installation automatically
- 📦 Install dependencies from the included wheels (no internet needed)
- 📂 Copy the addon to the correct location
- ✅ Show you when it's done

### Step 3: Enable in Blender
1. Start or restart Blender
2. Go to **Edit → Preferences → Add-ons**
3. Search for "RobStride"
4. Click the checkbox to enable
5. Look for the RobStride panel in the 3D View (press **N**)

**Total time: 2-3 minutes** ⏱️

---

## 📦 Package Contents

```
robstride-streamer-0.1.1-standalone/
├── 🚀 install.py                 (Installation script)
├── 📖 00-READ-ME-FIRST.txt       (Quick start guide)
├── 📚 README.md                  (Features and usage)
├── 📚 INSTALL.md                 (Detailed installation help)
├── 📄 LICENSE                    (MIT License)
├── 📋 MANIFEST.json              (Package information)
├── 🔧 robstride_streamer/        (Addon source code)
│   ├── __init__.py
│   ├── ui.py
│   ├── serial_link.py
│   ├── protocol.py
│   ├── fcurve_sampling.py
│   └── telemetry_view.py
└── 🔗 wheels/                    (Pre-built dependencies)
    └── pyserial-3.5-py2.py3-none-any.whl
```

---

## ✅ System Requirements

| Requirement | Version | Notes |
|---|---|---|
| Blender | 3.0+ | Newer is better |
| Python | 3.6+ | Included with Blender |
| OS | Any | Linux, macOS, Windows |
| Internet | Optional | **Not needed!** All dependencies included |

---

## 🎯 Why This Package is Better

### ✅ Single File
- Just **one .zip** to download and share
- No confusion about which package to use

### ✅ Self-Contained
- All dependencies included as wheels
- Works **completely offline**
- No internet connection needed during installation

### ✅ Easy Installation
- Single command: `python3 install.py`
- Automatic Blender detection
- Smart error handling

### ✅ Everything Included
- Source code
- Dependencies (no pip install needed)
- Documentation
- License

### ✅ No Internet Dependency
Traditional installers fail if:
- User has no internet
- Firewall blocks pip
- PyPI is down
- Network is slow

This package **avoids all these issues!**

---

## 🔧 How It Works

### Traditional Method (Problems):
```
User downloads → Runs installer → Installer tries to pip install → 
  → Needs internet → May fail → User frustrated
```

### Our Method (Better):
```
Package includes pre-built wheels → Installer uses local wheels → 
  → No internet needed → Works reliably every time
```

---

## 📖 Documentation

Inside the package:

- **00-READ-ME-FIRST.txt** - Quick start (this file)
- **install.py** - The installation script
- **README.md** - Features, capabilities, usage guide
- **INSTALL.md** - Detailed installation instructions and troubleshooting
- **LICENSE** - MIT License (free to use)

---

## 🆘 Troubleshooting

### "Blender not found"
Specify the path when prompted or use:
```bash
python3 install.py
# When asked: /snap/bin/blender  (or your Blender path)
```

### "Python not found"
Make sure Python 3 is installed. Try:
```bash
python3 --version
```

### Permission denied on Linux
You may need to add your user to the dialout group:
```bash
sudo usermod -a -G dialout $USER
newgrp dialout
```

### More help
See **INSTALL.md** in the package for detailed troubleshooting.

---

## 🎓 Advanced Usage

### Manual Installation
If `install.py` doesn't work, you can install manually:

```bash
# Extract the package
unzip robstride-streamer-0.1.1-standalone.zip
cd robstride-streamer-0.1.1-standalone

# Copy addon to Blender
cp -r robstride_streamer ~/.config/blender/5.0/scripts/addons/

# Install dependencies from wheels
/path/to/blender --background --python << 'ENDPYTHON'
import subprocess, sys
from pathlib import Path

wheels = list(Path('wheels').glob('*.whl'))
for wheel in wheels:
    subprocess.check_call([sys.executable, '-m', 'pip', 'install', str(wheel)])
ENDPYTHON
```

### Custom Installation Path
Edit `install.py` to change the installation directory before running.

---

## 💡 Features

✅ Real-time animation streaming to ESP32  
✅ Support for up to 32 motors  
✅ Flexible animation channel mapping  
✅ Motor parameter tuning (Kp, Kd, torque)  
✅ Real-time telemetry monitoring  
✅ Loopback/offline testing mode  
✅ Cross-platform support  
✅ Professional installation process  
✅ MIT License (free to use)  

---

## 📊 Package Information

| Property | Value |
|----------|-------|
| Version | 0.1.1 |
| Package Size | 106 KB |
| Wheel Included | pyserial 3.5 |
| Blender Support | 3.0+ |
| Python Support | 3.6+ |
| License | MIT |
| Type | Standalone (offline) |
| Internet Required | No |

---

## 🎉 Next Steps

1. **Extract**: `unzip robstride-streamer-0.1.1-standalone.zip`
2. **Install**: `python3 install.py`
3. **Restart Blender** and enable the addon
4. **Start using!** Configure motors and stream animations

---

## 📝 License

This addon is released under the **MIT License**. You're free to use, modify, and distribute it.

See **LICENSE** file for full details.

---

## ✨ Summary

This is a **professional, production-ready package** that:
- Works reliably every time
- Requires no internet connection
- Handles all the complexity for you
- Can be distributed anywhere
- Works on all major platforms

**Just extract, run, and enjoy!** 🚀

---

**Created**: January 28, 2025  
**Version**: 0.1.1  
**Status**: ✅ Production Ready
