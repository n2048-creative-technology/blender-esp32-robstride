bl_info = {
    "name": "RobStride Live Streamer",
    "author": "OpenAI Codex",
    "version": (0, 1, 1),
    "blender": (3, 0, 0),
    "location": "View3D > Sidebar > RobStride",
    "description": "Stream real-time motion setpoints from animation curves to ESP32 over Serial",
    "category": "System",
}

import sys
from pathlib import Path

# Add user site-packages to path for bundled dependencies (e.g., pyserial)
_addon_dir = Path(__file__).parent.parent
_site_packages = _addon_dir / "site-packages"
if _site_packages.exists() and str(_site_packages) not in sys.path:
    sys.path.insert(0, str(_site_packages))

# Also check Blender config directory for installed wheels
# Try all common Blender version directories (3.0-6.0)
_home = Path.home()
for _version in ["5.0", "6.0", "4.2", "3.6", "3.5", "3.4", "3.3", "3.2", "3.1", "3.0"]:
    _blender_site_packages = _home / ".config" / "blender" / _version / "scripts" / "addons" / "site-packages"
    if _blender_site_packages.exists() and str(_blender_site_packages) not in sys.path:
        sys.path.insert(0, str(_blender_site_packages))

import bpy
from . import ui as _ui


classes = _ui.classes


def register():
    for cls in classes:
        bpy.utils.register_class(cls)
    _ui.register_props()


def unregister():
    _ui.unregister_props()
    for cls in reversed(classes):
        bpy.utils.unregister_class(cls)
