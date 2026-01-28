# 📦 RobStride Streamer - Complete Installation Package

## 🎯 What You Have

A professional, production-ready Blender addon with **complete packaging and distribution support**. Users can install in **one command**.

---

## ⚡ Quick Start (30 seconds)

### For End Users:
```bash
unzip robstride-streamer-0.1.1.zip
cd robstride-streamer-0.1.1
python3 install_addon.py
```

✅ Done! Addon is installed. Restart Blender and enable it.

### For Developers:
```bash
tar -xzf robstride-streamer-0.1.1.tar.gz
cd robstride-streamer-0.1.1
python3 -c "from robstride_streamer.serial_link import SerialLink; print(SerialLink.list_ports())"
```

---

## 📂 Package Contents

### Distribution Packages in `dist/`

```
dist/
├── robstride-streamer-0.1.1.zip          (19 KB) ⭐ RECOMMENDED
│   └── Complete addon with install script
│
├── robstride-streamer-addon-0.1.1.zip    (13 KB)
│   └── Addon only (manual installation)
│
├── robstride-streamer-0.1.1.tar.gz       (14 KB)
│   └── Source distribution for developers
│
└── robstride-streamer-0.1.1/             (unpacked version)
    ├── robstride_streamer/               (the addon)
    ├── install_addon.py                  (automated installer)
    ├── README.md
    ├── INSTALL.md
    ├── LICENSE
    └── MANIFEST.json
```

### Main Directory Files

```
blender_addon/
├── 📦 DISTRIBUTION PACKAGES
│   ├── robstride_streamer/               (addon source)
│   ├── dist/                             (built packages)
│   ├── install_addon.py                  (user installer - EXECUTABLE)
│   └── build_dist.py                     (package builder)
│
├── 📚 DOCUMENTATION
│   ├── GET_STARTED.md                    👈 START HERE
│   ├── README.md                         (features & usage)
│   ├── INSTALL.md                        (installation guide)
│   ├── PACKAGING_SUMMARY.md              (package overview)
│   └── DISTRIBUTION.md                   (maintainer guide)
│
├── ⚙️  CONFIGURATION
│   ├── setup.py                          (Python package config)
│   ├── setup.cfg                         (package metadata)
│   └── LICENSE                           (MIT)
│
└── 📋 OTHER
    └── requirements.txt                  (Python dependencies)
```

---

## 🚀 Installation Methods

### Method 1: Automated (Easiest) ⭐
```bash
unzip robstride-streamer-0.1.1.zip
cd robstride-streamer-0.1.1
python3 install_addon.py
# Follow the prompts - that's it!
```

**Pros**: One command, auto-detects Blender, handles dependencies  
**Cons**: Requires Python 3 installed

---

### Method 2: Manual Installation
```bash
unzip robstride-streamer-addon-0.1.1.zip
cp -r robstride_streamer ~/.config/blender/4.0/scripts/addons/
blender --python-expr "import subprocess, sys; subprocess.check_call([sys.executable, '-m', 'pip', 'install', 'pyserial'])"
```

**Pros**: More control, understand each step  
**Cons**: More steps, manual dependency installation

---

### Method 3: From Source (Developers)
```bash
tar -xzf robstride-streamer-0.1.1.tar.gz
cd robstride-streamer-0.1.1
pip install -e .
```

**Pros**: Full source access, editable install  
**Cons**: Requires pip, Python development setup

---

## 📖 Documentation Guide

| Document | Read If... | Time |
|----------|-----------|------|
| [GET_STARTED.md](GET_STARTED.md) | You're new to this package | 2 min |
| [README.md](dist/robstride-streamer-0.1.1/README.md) | You want to use the addon | 5 min |
| [INSTALL.md](dist/robstride-streamer-0.1.1/INSTALL.md) | You're installing it | 5 min |
| [PACKAGING_SUMMARY.md](PACKAGING_SUMMARY.md) | You want package overview | 3 min |
| [DISTRIBUTION.md](DISTRIBUTION.md) | You're distributing it | 10 min |

---

## ✅ Verification Checklist

After installation, verify everything works:

```bash
# 1. Check pyserial is installed
blender --python-expr "import serial; print('✅ pyserial installed')" 2>/dev/null || echo "❌ Missing pyserial"

# 2. Check addon can be imported
blender --python-expr "from robstride_streamer import serial_link; print('✅ Addon accessible')" 2>/dev/null || echo "❌ Addon not found"

# 3. Check port detection works
blender --python-expr "from robstride_streamer.serial_link import SerialLink; ports = SerialLink.list_ports(); print(f'✅ Found {len(ports)} port(s)')" 2>/dev/null || echo "❌ Port detection failed"
```

---

## 🎯 Use Cases

### I want to... | Do this
|---|---|
| **Install the addon** | `python3 install_addon.py` (from full ZIP) |
| **Test without ESP32** | Enable "Loopback" in RobStride UI |
| **Configure motors** | RobStride panel → Motors → Add and configure |
| **Stream animation** | Object + animation → Motor assignment → Start stream |
| **Troubleshoot issues** | See INSTALL.md → Troubleshooting section |
| **Distribute the addon** | Share `robstride-streamer-0.1.1.zip` or addon-only ZIP |
| **Modify the code** | Extract tar.gz, edit, rebuild with `build_dist.py` |

---

## 🔧 System Requirements

| Component | Requirement | Check |
|-----------|-------------|-------|
| Blender | 3.0 or higher | `blender --version` |
| Python | 3.6+ (Blender's embedded) | Built-in to Blender |
| USB Device | ESP32 with RobStride firmware | `ls /dev/tty*` (Linux) |
| OS | Linux, macOS, or Windows | Any modern OS works |

---

## 📋 Package Formats Explained

### Full Distribution ZIP (robstride-streamer-0.1.1.zip)
**Best for**: Most users

Includes:
- ✅ Complete addon
- ✅ Install script (`install_addon.py`)
- ✅ All documentation
- ✅ License and manifest

Install:
```bash
unzip robstride-streamer-0.1.1.zip && cd robstride-streamer-0.1.1 && python3 install_addon.py
```

---

### Addon-Only ZIP (robstride-streamer-addon-0.1.1.zip)
**Best for**: Experienced Blender users

Includes:
- ✅ Addon folder only
- ✅ Minimal documentation

Install:
```bash
unzip robstride-streamer-addon-0.1.1.zip
cp -r robstride_streamer ~/.config/blender/4.0/scripts/addons/
```

---

### Source Distribution (robstride-streamer-0.1.1.tar.gz)
**Best for**: Developers and maintainers

Includes:
- ✅ Full source code
- ✅ Build configuration
- ✅ Development documentation

Install:
```bash
tar -xzf robstride-streamer-0.1.1.tar.gz && pip install -e .
```

---

## 🎓 Installation Examples

### Snap Blender (Linux)
```bash
unzip robstride-streamer-0.1.1.zip
cd robstride-streamer-0.1.1
python3 install_addon.py
# Selects /snap/bin/blender automatically
```

### Standard Blender (macOS)
```bash
unzip robstride-streamer-0.1.1.zip
cd robstride-streamer-0.1.1
python3 install_addon.py
# Finds /Applications/Blender.app automatically
```

### Windows
```powershell
Expand-Archive robstride-streamer-0.1.1.zip
cd robstride-streamer-0.1.1
python install_addon.py
# Finds Blender in Program Files
```

---

## 🐛 Troubleshooting

### Problem: Port only shows "Loopback"
**Solution**: Reinstall pyserial
```bash
blender --python-expr "import subprocess, sys; subprocess.check_call([sys.executable, '-m', 'pip', 'install', '--force-reinstall', 'pyserial'])"
```

### Problem: Addon doesn't appear in Blender
**Solution**: Verify installation location
```bash
ls ~/.config/blender/4.0/scripts/addons/robstride_streamer/__init__.py
```

### Problem: Permission denied on serial port (Linux)
**Solution**: Add user to dialout group
```bash
sudo usermod -a -G dialout $USER
newgrp dialout
```

See full troubleshooting in [INSTALL.md](dist/robstride-streamer-0.1.1/INSTALL.md).

---

## 🚀 Next Steps

1. **Choose a distribution package**:
   - Users: `robstride-streamer-0.1.1.zip`
   - Developers: `robstride-streamer-0.1.1.tar.gz`

2. **Install the addon**:
   - Run `python3 install_addon.py` or manual installation

3. **Enable in Blender**:
   - Edit → Preferences → Add-ons → Search "RobStride" → ✓

4. **Start using**:
   - Open 3D View, press N, find RobStride panel
   - Configure motors and start streaming!

---

## 📞 Support Resources

- **Installation Help**: See [INSTALL.md](dist/robstride-streamer-0.1.1/INSTALL.md)
- **Usage Guide**: See [README.md](dist/robstride-streamer-0.1.1/README.md)
- **Distribution Info**: See [DISTRIBUTION.md](DISTRIBUTION.md)
- **Package Overview**: See [PACKAGING_SUMMARY.md](PACKAGING_SUMMARY.md)

---

## 📊 Package Statistics

| Metric | Value |
|--------|-------|
| Addon Version | 0.1.1 |
| Blender Requirement | 3.0+ |
| Python Requirement | 3.6+ |
| Primary Dependency | pyserial ≥3.5 |
| Full Package Size | 19 KB |
| Addon-Only Size | 13 KB |
| Source Package Size | 14 KB |
| Files in Addon | 6 Python files + 1 requirements |
| Total Documentation | 5 markdown files |

---

## ✨ Features Included

✅ Real-time animation streaming  
✅ Multi-motor support (up to 32)  
✅ Flexible channel mapping  
✅ Motor parameter tuning (Kp, Kd, T_ff)  
✅ Telemetry monitoring  
✅ Loopback/offline mode  
✅ Serial port detection  
✅ Robust error handling  
✅ Complete documentation  
✅ Automated installation  

---

## 🎉 You're All Set!

Your addon is packaged, documented, and ready for distribution.

**First-time users**: Start with [GET_STARTED.md](GET_STARTED.md)  
**Experienced users**: Jump to [INSTALL.md](dist/robstride-streamer-0.1.1/INSTALL.md)  
**Developers**: See [DISTRIBUTION.md](DISTRIBUTION.md)

---

**Version**: 0.1.1  
**Created**: January 28, 2025  
**Status**: ✅ Ready for Production Distribution
