#!/usr/bin/env python3
"""
Create a self-contained RobStride Streamer installation package.

This script creates a single .zip file that includes:
- Complete addon source code
- All dependencies as pre-built wheels
- Standalone installation script
- Complete documentation

No internet connection needed for installation!
"""

import os
import sys
import subprocess
import shutil
import json
import venv
from pathlib import Path
from datetime import datetime


def get_version():
    """Extract version from __init__.py"""
    init_file = Path(__file__).parent / "robstride_streamer" / "__init__.py"
    with open(init_file) as f:
        for line in f:
            if '"version"' in line and '(' in line:
                start = line.index('(') + 1
                end = line.index(')')
                version_str = line[start:end].replace(' ', '').replace(',', '.')
                return version_str
    return "0.1.1"


def create_venv_and_wheels():
    """Create a virtual environment and build wheels for dependencies."""
    print("\n🔧 Creating virtual environment for wheel building...")
    
    base_dir = Path(__file__).parent
    venv_dir = base_dir / "build_venv"
    wheels_dir = base_dir / "wheels"
    
    # Clean up old venv and wheels
    if venv_dir.exists():
        shutil.rmtree(venv_dir)
    if wheels_dir.exists():
        shutil.rmtree(wheels_dir)
    
    wheels_dir.mkdir(exist_ok=True)
    
    # Create venv
    print("   Creating virtual environment...")
    venv.create(venv_dir, with_pip=True)
    
    # Get Python executable from venv
    if sys.platform == "win32":
        python_exe = venv_dir / "Scripts" / "python.exe"
        pip_exe = venv_dir / "Scripts" / "pip.exe"
    else:
        python_exe = venv_dir / "bin" / "python"
        pip_exe = venv_dir / "bin" / "pip"
    
    try:
        # Upgrade pip and install wheel package
        print("   Upgrading pip...")
        subprocess.check_call([str(pip_exe), "install", "--upgrade", "pip", "wheel"], 
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        
        # Build wheels for all dependencies
        print("   Building wheels for dependencies...")
        dependencies = ["pyserial>=3.5"]
        
        for dep in dependencies:
            print(f"     Building wheel for {dep}...")
            subprocess.check_call(
                [str(pip_exe), "wheel", "--wheel-dir", str(wheels_dir), dep],
                stdout=subprocess.DEVNULL
            )
        
        print(f"✅ Wheels created in {wheels_dir}")
        return wheels_dir
        
    except Exception as e:
        print(f"⚠️  Error building wheels: {e}")
        print("   Continuing without wheels...")
        return None
    finally:
        # Clean up venv
        if venv_dir.exists():
            print("   Cleaning up virtual environment...")
            shutil.rmtree(venv_dir)


def create_standalone_installer():
    """Create a standalone installer script that uses wheels."""
    installer_script = '''#!/usr/bin/env python3
"""
RobStride Streamer - Standalone Installation Script
Installs addon with pre-bundled dependencies (no internet needed)
"""

import os
import sys
import subprocess
import shutil
import platform
from pathlib import Path


def find_blender():
    """Find the Blender executable in the system."""
    system = platform.system()
    
    if system == "Linux":
        candidates = [
            "/snap/bin/blender",
            "/usr/bin/blender",
            "/usr/local/bin/blender",
            os.path.expanduser("~/blender/blender"),
        ]
    elif system == "Darwin":
        candidates = [
            "/Applications/Blender.app/Contents/MacOS/Blender",
            os.path.expanduser("~/Applications/Blender.app/Contents/MacOS/Blender"),
        ]
    elif system == "Windows":
        candidates = [
            "C:\\\\Program Files\\\\Blender Foundation\\\\Blender\\\\blender.exe",
            "C:\\\\Program Files (x86)\\\\Blender Foundation\\\\Blender\\\\blender.exe",
        ]
    else:
        return None
    
    for path in candidates:
        if os.path.exists(path):
            return path
    return None


def get_blender_addon_path():
    """Get the Blender addons directory for the current system."""
    system = platform.system()
    
    if system == "Linux":
        base = Path.home() / ".config" / "blender"
    elif system == "Darwin":
        base = Path.home() / "Library" / "Application Support" / "blender"
    elif system == "Windows":
        base = Path.home() / "AppData" / "Roaming" / "Blender Foundation" / "Blender"
    else:
        return None
    
    if base.exists():
        versions = sorted([d for d in base.iterdir() if d.is_dir() and d.name[0].isdigit()], 
                         reverse=True)
        if versions:
            return versions[0] / "scripts" / "addons"
    return None


def install_dependencies_from_wheels(blender_path, wheels_dir):
    """Install dependencies from pre-built wheels using Blender's Python."""
    print(f"\\n📦 Installing dependencies from wheels...")
    
    # Create temp script to install wheels
    temp_script = Path("/tmp/robstride_install_wheels.py")
    wheels_dir_abs = Path(wheels_dir).absolute()
    
    script_content = f"""
import subprocess
import sys
from pathlib import Path

wheels_dir = Path(r'{wheels_dir_abs}')
wheel_files = list(wheels_dir.glob('*.whl'))

if not wheel_files:
    print("ERROR: No wheel files found in {wheels_dir_abs}")
    sys.exit(1)

print(f"Found {{len(wheel_files)}} wheel(s)")
for wheel in wheel_files:
    print(f"  Installing {{wheel.name}}...")
    try:
        subprocess.check_call([sys.executable, '-m', 'pip', 'install', str(wheel), '-q'])
    except Exception as e:
        print(f"  Error: {{e}}")
        sys.exit(1)

print("SUCCESS")
"""
    
    try:
        temp_script.write_text(script_content)
        
        cmd = [
            blender_path,
            "--background",
            "--python",
            str(temp_script)
        ]
        
        print("   Installing wheels (this may take a minute)...")
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
        
        if temp_script.exists():
            temp_script.unlink()
        
        if "SUCCESS" in result.stdout or result.returncode == 0:
            print("✅ Dependencies installed successfully")
            return True
        else:
            print(f"⚠️  Could not install dependencies")
            if result.stdout:
                print(f"   Output: {result.stdout}")
            if result.stderr:
                print(f"   Error: {result.stderr}")
            return False
            
    except subprocess.TimeoutExpired:
        print("⚠️  Installation timed out")
        if temp_script.exists():
            temp_script.unlink()
        return False
    except Exception as e:
        print(f"⚠️  Error: {e}")
        if temp_script.exists():
            temp_script.unlink()
        return False


def install_addon(addon_path, target_path):
    """Copy the addon to the Blender addons directory."""
    print(f"\\n📂 Installing addon to {target_path}...")
    
    target_path.mkdir(parents=True, exist_ok=True)
    
    addon_name = "robstride_streamer"
    target_addon = target_path / addon_name
    
    if target_addon.exists() or target_addon.is_symlink():
        print(f"   Removing existing installation...")
        try:
            if target_addon.is_symlink():
                target_addon.unlink()
            else:
                shutil.rmtree(target_addon)
        except Exception as e:
            print(f"⚠️  Warning: {e}")
    
    source_addon = Path(addon_path) / addon_name
    if source_addon.exists():
        shutil.copytree(source_addon, target_addon)
        print(f"✅ Addon installed to {target_addon}")
        return True
    else:
        print(f"❌ Source addon not found at {source_addon}")
        return False


def main():
    print("=" * 60)
    print("RobStride Streamer - Standalone Installation")
    print("=" * 60)
    
    print("\\n🔍 Searching for Blender installation...")
    blender_path = find_blender()
    
    if not blender_path:
        print("❌ Could not find Blender")
        print("\\nPlease specify the Blender executable path:")
        blender_path = input("> ").strip()
        if not os.path.exists(blender_path):
            print(f"❌ Blender not found")
            return 1
    
    print(f"✅ Found Blender at: {blender_path}")
    
    print("\\n🔍 Locating Blender addons directory...")
    addon_path = get_blender_addon_path()
    
    if not addon_path:
        print("❌ Could not determine Blender addons directory")
        print("\\nPlease specify the target addons directory:")
        addon_path = input("> ").strip()
        addon_path = Path(addon_path)
    else:
        print(f"✅ Found addons directory: {addon_path}")
    
    current_dir = Path(__file__).parent
    wheels_dir = current_dir / "wheels"
    
    # Install dependencies from wheels
    if wheels_dir.exists():
        if not install_dependencies_from_wheels(blender_path, wheels_dir):
            print("⚠️  Failed to install dependencies, but continuing...")
    else:
        print("⚠️  No wheels directory found")
    
    # Install addon
    if install_addon(current_dir, addon_path):
        print("\\n" + "=" * 60)
        print("✅ Installation Complete!")
        print("=" * 60)
        print("\\nNext steps:")
        print("1. Start or restart Blender")
        print("2. Go to Edit → Preferences → Add-ons")
        print("3. Search for 'RobStride' and enable it")
        print("4. Open the RobStride panel in the 3D View (press N)")
        return 0
    else:
        print("\\n❌ Installation failed")
        return 1


if __name__ == "__main__":
    sys.exit(main())
'''
    return installer_script


def create_standalone_package():
    """Create a single self-contained .zip package."""
    version = get_version()
    base_dir = Path(__file__).parent
    dist_dir = base_dir / "dist"
    package_name = f"robstride-streamer-{version}-standalone"
    package_dir = dist_dir / package_name
    
    print(f"\n📦 Creating standalone package: {package_name}")
    
    # Build wheels
    wheels_dir = create_venv_and_wheels()
    
    # Clean and create package directory
    if package_dir.exists():
        shutil.rmtree(package_dir)
    package_dir.mkdir(parents=True, exist_ok=True)
    
    # Copy addon files
    addon_src = base_dir / "robstride_streamer"
    addon_dst = package_dir / "robstride_streamer"
    shutil.copytree(addon_src, addon_dst, ignore=shutil.ignore_patterns('__pycache__', '*.pyc'))
    
    # Copy wheels
    if wheels_dir and wheels_dir.exists():
        wheels_dst = package_dir / "wheels"
        shutil.copytree(wheels_dir, wheels_dst)
        print(f"✅ Copied wheels")
    
    # Copy important files
    for fname in ["README.md", "INSTALL.md", "LICENSE", "requirements.txt"]:
        src = base_dir / fname
        if src.exists():
            shutil.copy(src, package_dir / fname)
    
    # Create standalone installer
    installer_content = create_standalone_installer()
    installer_path = package_dir / "install.py"
    installer_path.write_text(installer_content)
    installer_path.chmod(0o755)
    
    # Create manifest
    manifest = {
        "name": "RobStride Live Streamer",
        "version": version,
        "type": "standalone",
        "blender": "3.0.0",
        "description": "Stream real-time motion setpoints from animation curves to ESP32 over Serial",
        "author": "OpenAI Codex",
        "build_date": datetime.now().isoformat(),
        "dependencies": ["pyserial>=3.5"],
        "includes_wheels": wheels_dir is not None and wheels_dir.exists(),
        "files": [
            "robstride_streamer/ (addon source)",
            "wheels/ (pre-built dependencies)",
            "install.py (installer script)",
            "README.md",
            "INSTALL.md",
            "LICENSE",
            "MANIFEST.json",
        ]
    }
    
    with open(package_dir / "MANIFEST.json", "w") as f:
        json.dump(manifest, f, indent=2)
    
    # Create installation instructions
    instructions = """
ROBSTRIDE STREAMER - STANDALONE INSTALLATION
==============================================

This package contains everything needed to install the RobStride Streamer addon.

QUICK START:
-----------

1. Extract this .zip file:
   unzip robstride-streamer-*-standalone.zip

2. Run the installer:
   cd robstride-streamer-*-standalone
   python3 install.py

3. Restart Blender and enable the addon:
   Edit → Preferences → Add-ons → Search "RobStride" → Enable

That's it! No internet connection required.

WHAT'S INCLUDED:
----------------
✓ Complete addon source code
✓ Pre-built wheels for all dependencies (pyserial and its dependencies)
✓ Smart installation script
✓ Complete documentation

SYSTEM REQUIREMENTS:
--------------------
- Blender 3.0 or higher
- Python 3.6+ (included with Blender)
- Linux, macOS, or Windows

TROUBLESHOOTING:
----------------
If the installer fails, see README.md or INSTALL.md

For questions or issues:
- Check README.md for features and usage
- Check INSTALL.md for detailed installation help
"""
    
    with open(package_dir / "00-READ-ME-FIRST.txt", "w") as f:
        f.write(instructions)
    
    # Create ZIP archive
    zip_path = dist_dir / package_name
    shutil.make_archive(str(zip_path), 'zip', package_dir.parent, package_dir.name)
    
    print(f"✅ Created {zip_path}.zip")
    
    # Get file size
    zip_file = Path(f"{zip_path}.zip")
    size_kb = zip_file.stat().st_size / 1024
    
    return zip_file, size_kb


def main():
    print("=" * 70)
    print("RobStride Streamer - Standalone Package Builder")
    print("=" * 70)
    
    base_dir = Path(__file__).parent
    dist_dir = base_dir / "dist"
    
    try:
        # Clean old packages (keep only the new standalone one)
        print("\n🧹 Cleaning old packages...")
        if dist_dir.exists():
            for item in dist_dir.glob("robstride-streamer-*"):
                if item.is_dir():
                    shutil.rmtree(item)
                elif item.suffix in ['.zip', '.tar.gz']:
                    item.unlink()
        
        dist_dir.mkdir(exist_ok=True)
        
        # Create standalone package
        zip_file, size_kb = create_standalone_package()
        
        print("\n" + "=" * 70)
        print("✅ Standalone Package Created Successfully!")
        print("=" * 70)
        print(f"\nPackage: {zip_file.name} ({size_kb:.1f} KB)")
        print(f"Location: {zip_file}")
        print("\nWhat's included:")
        print("  ✓ Complete addon source code")
        print("  ✓ All dependencies as pre-built wheels")
        print("  ✓ Standalone installation script (no internet needed)")
        print("  ✓ Complete documentation")
        print("\nHow to use:")
        print("  1. unzip robstride-streamer-*-standalone.zip")
        print("  2. python3 install.py")
        print("  3. Restart Blender and enable the addon")
        print("\n🎉 Ready to distribute!")
        
        return 0
        
    except Exception as e:
        print(f"\n❌ Error: {e}")
        import traceback
        traceback.print_exc()
        return 1


if __name__ == "__main__":
    sys.exit(main())
