bl_info = {
    "name": "RobStride Live Streamer",
    "author": "OpenAI Codex",
    "version": (0, 1, 1),
    "blender": (3, 0, 0),
    "location": "View3D > Sidebar > RobStride",
    "description": "Stream real-time motion setpoints from animation curves to ESP32 over Serial",
    "category": "System",
}

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
