#!/usr/bin/env python3
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
            "C:\\Program Files\\Blender Foundation\\Blender\\blender.exe",
            "C:\\Program Files (x86)\\Blender Foundation\\Blender\\blender.exe",
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
    print(f"\n📦 Installing dependencies from wheels...")
    
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
    print(f"\n📂 Installing addon to {target_path}...")
    
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
    
    print("\n🔍 Searching for Blender installation...")
    blender_path = find_blender()
    
    if not blender_path:
        print("❌ Could not find Blender")
        print("\nPlease specify the Blender executable path:")
        blender_path = input("> ").strip()
        if not os.path.exists(blender_path):
            print(f"❌ Blender not found")
            return 1
    
    print(f"✅ Found Blender at: {blender_path}")
    
    print("\n🔍 Locating Blender addons directory...")
    addon_path = get_blender_addon_path()
    
    if not addon_path:
        print("❌ Could not determine Blender addons directory")
        print("\nPlease specify the target addons directory:")
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
        print("\n" + "=" * 60)
        print("✅ Installation Complete!")
        print("=" * 60)
        print("\nNext steps:")
        print("1. Start or restart Blender")
        print("2. Go to Edit → Preferences → Add-ons")
        print("3. Search for 'RobStride' and enable it")
        print("4. Open the RobStride panel in the 3D View (press N)")
        return 0
    else:
        print("\n❌ Installation failed")
        return 1


if __name__ == "__main__":
    sys.exit(main())
