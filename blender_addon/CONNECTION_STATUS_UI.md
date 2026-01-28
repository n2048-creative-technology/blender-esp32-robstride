# Connection Status Indicator

## Overview
The RobStride Streamer UI now displays a real-time connection status indicator at the top of the panel.

## Status Display

The status indicator shows different states with visual icons:

### States

| State | Display | Icon | Meaning |
|-------|---------|------|---------|
| **Connected & Streaming** | ● STREAMING | PLAY | Motor data is being sent to ESP32 |
| **Connected (Idle)** | ● CONNECTED | CHECKMARK | Serial port open, ready to stream |
| **Loopback Mode** | ● LOOPBACK MODE | INFO | Testing/debug mode, no real device |
| **Disconnected** | ● DISCONNECTED | X | No connection, click "Connect" |

## Connection Details

When connected, the status box also displays:
- **Port:** The serial port being used (e.g., `/dev/ttyACM0` or `Loopback`)
- **Baud Rate:** The communication speed (e.g., `921600 baud`)

Example:
```
● CONNECTED ✓
Port: /dev/ttyACM0 @ 921600 baud 🔊
```

## Implementation

The status indicator is added to the top of the RobStride panel using:
- Status properties already available in the addon (`connected`, `streaming`, `loopback`)
- Blender's layout system for visual organization
- Icons from Blender's built-in icon set

## Files Modified

- `robstride_streamer/ui.py` - Updated `ROBSTRIDE_PT_panel.draw()` method

## Workflow

1. **Startup:** Panel shows "● DISCONNECTED"
2. **After Connect:** Panel shows "● CONNECTED" with port/baud details
3. **During Stream:** Panel shows "● STREAMING" with play icon
4. **Loopback Mode:** Panel shows "● LOOPBACK MODE" for testing without hardware

## Usage

No configuration needed! The indicator updates automatically as you:
- Click "Connect" button
- Start/Stop streaming
- Toggle loopback mode
- Change port or baud rate

Just look at the top of the RobStride panel to see your connection status.

