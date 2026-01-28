# RobStride Streamer - Installation & Distribution Guide

## 📦 Distribution Packages Available

Your addon is now ready for distribution in multiple formats:

### 1. **robstride-streamer-0.1.1.zip** (18.3 KB) ⭐ RECOMMENDED
**Full distribution package with everything included**

Contains:
- ✅ Complete addon source code
- ✅ Automated installation script (`install_addon.py`)
- ✅ Full documentation (README, INSTALL guide)
- ✅ License and manifest
- ✅ All dependencies listed

**Who should use**: End users, most people

**Installation**:
```bash
unzip robstride-streamer-0.1.1.zip
cd robstride-streamer-0.1.1
python3 install_addon.py
```

That's it! The script handles everything.

---

### 2. **robstride-streamer-addon-0.1.1.zip** (12.5 KB)
**Addon-only package for manual installation**

Contains:
- ✅ Addon source code only
- ✅ Brief README
- ✅ Minimal documentation

**Who should use**: Experienced Blender users who prefer manual control

**Installation**:
```bash
unzip robstride-streamer-addon-0.1.1.zip
cp -r robstride_streamer ~/.config/blender/4.0/scripts/addons/
# Then install dependencies manually:
blender --python-expr "import subprocess, sys; subprocess.check_call([sys.executable, '-m', 'pip', 'install', 'pyserial'])"
```

**Note**: Requires manual dependency installation

---

### 3. **robstride-streamer-0.1.1.tar.gz** (13.9 KB)
**Source distribution for developers**

Contains:
- ✅ Complete source code
- ✅ Setup and configuration files
- ✅ All documentation

**Who should use**: Developers, package maintainers, CI/CD pipelines

**Installation**:
```bash
tar -xzf robstride-streamer-0.1.1.tar.gz
cd robstride-streamer-0.1.1
pip install -e .
```

---

## 🎯 Quick Installation Guide

### The Easiest Way (One Command)

```bash
unzip robstride-streamer-0.1.1.zip && cd robstride-streamer-0.1.1 && python3 install_addon.py
```

This will:
1. ✅ Find your Blender installation
2. ✅ Install pyserial dependency automatically
3. ✅ Copy addon to the right location
4. ✅ Tell you what to do next

---

## 📋 System Requirements

- **Blender**: 3.0 or higher
- **Python**: 3.6+ (Blender's embedded Python)
- **OS**: Linux, macOS, or Windows
- **USB Device**: ESP32 with RobStride firmware

---

## ✅ Verification

After installation, verify it worked:

### In Blender:
1. **Edit → Preferences → Add-ons**
2. Search for "RobStride"
3. You should see "RobStride Live Streamer" listed
4. Check the box to enable it
5. Look for the RobStride panel in the 3D View (press **N**)

### From Command Line:
```bash
blender --python-expr "import bpy; bpy.context.scene.robstride" 2>&1 | grep -q "AttributeError" || echo "✅ Addon is working!"
```

---

## 🛠️ Troubleshooting

### Port Still Shows Only "Loopback"

**Problem**: ESP32 not appearing in port list

**Solution**:
```bash
# Force reinstall pyserial
/snap/bin/blender --python-expr "import subprocess, sys; subprocess.check_call([sys.executable, '-m', 'pip', 'install', '--force-reinstall', 'pyserial'])"
```

Then restart Blender completely.

### Installation Script Fails

**Try manual installation instead**:
```bash
unzip robstride-streamer-addon-0.1.1.zip
cp -r robstride_streamer ~/.config/blender/4.0/scripts/addons/
```

### Permission Denied on Linux

```bash
# Add your user to dialout group for serial port access
sudo usermod -a -G dialout $USER
newgrp dialout
```

---

## 📂 File Locations

### Where Blender Addons Go

**Linux:**
```
~/.config/blender/4.0/scripts/addons/robstride_streamer/
```

**macOS:**
```
~/Library/Application Support/blender/4.0/scripts/addons/robstride_streamer/
```

**Windows:**
```
%APPDATA%\Blender Foundation\Blender\4.0\scripts\addons\robstride_streamer\
```

(Replace 4.0 with your Blender version)

---

## 🚀 Next Steps

### For Users:
1. Install the addon using `robstride-streamer-0.1.1.zip`
2. Read [README.md](robstride-streamer-0.1.1/README.md) for features and usage
3. Read [INSTALL.md](robstride-streamer-0.1.1/INSTALL.md) for detailed setup

### For Developers:
1. Extract the source distribution
2. Modify as needed
3. Test with: `python3 -c "from robstride_streamer.serial_link import SerialLink; SerialLink.list_ports()"`
4. Rebuild packages with: `python3 build_dist.py`

### For Maintainers:
See [DISTRIBUTION.md](../DISTRIBUTION.md) for hosting, versioning, and release procedures.

---

## 📚 Documentation Files

| File | Purpose |
|------|---------|
| [README.md](robstride-streamer-0.1.1/README.md) | Features, usage guide, troubleshooting |
| [INSTALL.md](robstride-streamer-0.1.1/INSTALL.md) | Detailed installation instructions |
| [LICENSE](robstride-streamer-0.1.1/LICENSE) | MIT License |
| [MANIFEST.json](robstride-streamer-0.1.1/MANIFEST.json) | Package metadata |
| [DISTRIBUTION.md](../DISTRIBUTION.md) | For package maintainers |
| [PACKAGING_SUMMARY.md](../PACKAGING_SUMMARY.md) | Overview of all packages |

---

## 📊 Package Comparison

| Feature | Full ZIP | Addon ZIP | Tar.gz |
|---------|----------|-----------|--------|
| Installation Script | ✅ Yes | ❌ No | ❌ No |
| Documentation | ✅ Full | ⚠️ Minimal | ✅ Full |
| Size | 18.3 KB | 12.5 KB | 13.9 KB |
| Dependency Install | ✅ Auto | ❌ Manual | ⚠️ pip |
| Best For | Most Users | Advanced Users | Developers |
| Single Command Install | ✅ Yes | ❌ No | ⚠️ With pip |

---

## 💡 Usage Tips

### Enable the Addon:
```
Blender → Edit → Preferences → Add-ons → Search "RobStride" → Check box
```

### Access the UI:
```
3D View → Press N → Look for RobStride tab
```

### Test Without Hardware:
```
RobStride Panel → Enable "Loopback" → Click Connect
```

### Configure Motors:
```
RobStride Panel → Motors → Add slots → Assign objects and channels
```

---

## 🔄 Distribution Methods

You can now distribute your addon via:

### 1. **Direct Download**
- Upload `robstride-streamer-0.1.1.zip` to your website
- Users download and run install script
- ✅ Simplest for end users

### 2. **GitHub Releases**
- Push repository to GitHub
- Create releases with all package formats
- ✅ Version tracking, easy updates

### 3. **PyPI Package Registry**
- Publish wheel/source to PyPI
- Users can: `pip install robstride-streamer`
- ✅ For Python developers

### 4. **Blender Extensions**
- Submit `robstride-streamer-addon-0.1.1.zip`
- Official Blender addon marketplace
- ✅ Built-in Blender addon browser

---

## 📞 Support

### Common Issues:

1. **"pyserial not found"**
   - Run: `/snap/bin/blender --python-expr "import subprocess, sys; subprocess.check_call([sys.executable, '-m', 'pip', 'install', 'pyserial'])"`
   
2. **"Addon doesn't appear"**
   - Verify folder is in `~/.config/blender/X.X/scripts/addons/`
   - Folder name must be exactly `robstride_streamer`
   - Restart Blender completely
   
3. **"Can't open serial port"**
   - Check USB connection: `ls /dev/tty*` (Linux)
   - Check port permissions: `sudo usermod -a -G dialout $USER` (Linux)
   - Try baud rate adjustment in UI

See full troubleshooting in [INSTALL.md](robstride-streamer-0.1.1/INSTALL.md).

---

## 🎉 You're Ready!

Your addon is packaged and ready for distribution. 

Choose the package format that best fits your distribution method:
- **Most people**: Use `robstride-streamer-0.1.1.zip` with the install script
- **Tech-savvy users**: Use `robstride-streamer-addon-0.1.1.zip` 
- **Developers**: Use `robstride-streamer-0.1.1.tar.gz`

---

**Package Version**: 0.1.1  
**Created**: January 28, 2025  
**Status**: ✅ Ready for Distribution
