# Distribution and Packaging Guide

This document describes the different ways to distribute and install the RobStride Streamer Blender addon.

## Overview

The RobStride Streamer addon is packaged and distributed in multiple formats to support different use cases:

1. **Full Distribution Package** - Recommended for end users
2. **Addon-Only Package** - For experienced users who can manage dependencies
3. **Wheel Distribution** - For Python developers and CI/CD integration
4. **Source Distribution** - For development and modification

---

## Building Packages

### Prerequisites

```bash
# Install build tools
pip install setuptools wheel build twine
```

### Build All Distributions

```bash
cd /path/to/blender_addon
python3 build_dist.py
```

This creates:
- `dist/robstride-streamer-X.X.X.zip` - Full distribution
- `dist/robstride-streamer-addon-X.X.X.zip` - Addon only
- `dist/robstride_streamer-X.X.X.whl` - Wheel package
- `dist/robstride_streamer-X.X.X.tar.gz` - Source distribution

### Build Specific Packages

```bash
# Wheel only
python3 setup.py bdist_wheel

# Source distribution
python3 setup.py sdist

# Both
python3 setup.py sdist bdist_wheel
```

---

## Distribution Package Contents

### Full Distribution (`robstride-streamer-X.X.X.zip`)

**Best for**: End users without prior Blender addon experience

**Contents:**
```
robstride-streamer-0.1.1/
├── robstride_streamer/          # The addon package
│   ├── __init__.py
│   ├── ui.py
│   ├── serial_link.py
│   ├── protocol.py
│   ├── fcurve_sampling.py
│   ├── telemetry_view.py
│   └── requirements.txt
├── install_addon.py             # Automated installer
├── README.md                     # Full documentation
├── INSTALL.md                    # Installation guide
├── LICENSE                       # MIT License
├── MANIFEST.json                 # Package manifest
└── requirements.txt              # Python dependencies
```

**Installation:**
```bash
unzip robstride-streamer-0.1.1.zip
cd robstride-streamer-0.1.1
python3 install_addon.py
```

The `install_addon.py` script will:
- Auto-detect Blender installation
- Install pyserial dependency
- Copy addon to the correct location
- Provide next steps

### Addon-Only Package (`robstride-streamer-addon-X.X.X.zip`)

**Best for**: Users who prefer manual installation or have dependencies already installed

**Contents:**
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

**Installation:**

1. Extract to your Blender addons directory
2. Install dependencies manually: `blender --python-expr "import subprocess, sys; subprocess.check_call([sys.executable, '-m', 'pip', 'install', 'pyserial'])"`
3. Restart Blender and enable addon

### Wheel Package (`robstride_streamer-X.X.X.whl`)

**Best for**: Python developers, package managers, CI/CD integration

**Installation:**
```bash
pip install robstride_streamer-0.1.1-py3-none-any.whl
```

Note: This installs the Python package, not the Blender addon. Use for development purposes.

### Source Distribution (`robstride_streamer-X.X.X.tar.gz`)

**Best for**: Package maintainers, source modifications, PyPI distribution

**Installation:**
```bash
pip install robstride_streamer-0.1.1.tar.gz
# or for development
pip install -e robstride_streamer-0.1.1.tar.gz
```

---

## Hosting and Distribution

### GitHub Releases

1. Tag a release:
   ```bash
   git tag -a v0.1.1 -m "Release version 0.1.1"
   git push origin v0.1.1
   ```

2. Upload packages to GitHub Releases
   - Full distribution ZIP
   - Addon-only ZIP
   - Wheel and source distributions

### Package Registry (PyPI)

For public Python package distribution:

```bash
# Register account at https://pypi.org/

# Build packages
python3 setup.py sdist bdist_wheel

# Upload
twine upload dist/robstride_streamer-0.1.1*
```

### Blender Extensions Platform

For distribution through official Blender addon repository:

1. Ensure addon meets Blender extension requirements
2. Package as `.zip` with correct structure
3. Submit to https://extensions.blender.org/

---

## Version Management

### Semantic Versioning

Use semantic versioning: `MAJOR.MINOR.PATCH`

- **MAJOR**: Incompatible API changes or major features
- **MINOR**: New features, backward compatible
- **PATCH**: Bug fixes, small improvements

### Updating Version

Update version in these files:

```python
# robstride_streamer/__init__.py
bl_info = {
    ...
    "version": (0, 1, 1),  # Change here
    ...
}
```

```python
# setup.py
setup(
    name="robstride-streamer",
    version="0.1.1",  # Change here
    ...
)
```

Also update in:
- `README.md` (if version mentioned)
- `INSTALL.md` (if paths include version)
- Any changelog files

---

## Installation Verification

### For End Users

After installation, verify in Blender:

1. **Enable addon**: Edit → Preferences → Add-ons → Search "RobStride"
2. **Check console**:
   ```python
   # In Blender's Python console
   import bpy
   print("RobStride" in bpy.types.Scene.robstride.__class__.__name__)
   ```
3. **Test loopback mode**:
   - Open RobStride panel (press N in 3D View)
   - Enable Loopback
   - Click Connect (should succeed)

### For Developers

```bash
# Test wheel installation
pip install --force-reinstall dist/robstride_streamer-*.whl

# Test in Python
python3 -c "from robstride_streamer.serial_link import SerialLink; print(SerialLink.list_ports())"
```

---

## Troubleshooting

### Build Issues

**ModuleNotFoundError: No module named 'setuptools'**
```bash
pip install setuptools wheel
```

**Error in setup.py**
- Ensure all files referenced exist
- Check `robstride_streamer/__init__.py` for valid Python
- Verify all required metadata is present

### Installation Issues

**Addon doesn't appear after installation**
- Verify folder is in correct addons directory
- Check folder is named `robstride_streamer`
- Restart Blender completely
- Look in Edit → Preferences → Add-ons (search "RobStride")

**pyserial not found**
- Run: `blender --python-expr "import subprocess, sys; subprocess.check_call([sys.executable, '-m', 'pip', 'install', 'pyserial'])"`
- Restart Blender

---

## CI/CD Integration

### GitHub Actions Example

```yaml
name: Build Distribution

on:
  push:
    tags:
      - 'v*'

jobs:
  build:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v2
      - uses: actions/setup-python@v2
        with:
          python-version: '3.9'
      - run: pip install setuptools wheel build
      - run: python build_dist.py
      - uses: actions/upload-artifact@v2
        with:
          name: distributions
          path: dist/
```

---

## Release Checklist

- [ ] Update version numbers in all files
- [ ] Update CHANGELOG
- [ ] Test addon functionality in Blender
- [ ] Test with Loopback mode
- [ ] Test with real hardware (if available)
- [ ] Run linting and type checking
- [ ] Build all distribution packages
- [ ] Test installations from each package type
- [ ] Create GitHub release with packages
- [ ] Update documentation/website
- [ ] Announce on relevant forums/channels

---

## Support Resources

- Blender Addon Development: https://docs.blender.org/manual/en/latest/advanced/scripting/addon_development.html
- Python Packaging: https://packaging.python.org/
- setuptools Documentation: https://setuptools.pypa.io/
- PyPI: https://pypi.org/

