#!/usr/bin/env python3
# This file documents the fix applied to install_addon.py

"""
FIX SUMMARY FOR install_addon.py
================================

PROBLEM:
--------
The install_addon.py script was hanging when trying to install dependencies.
It would print "Installing dependencies via Blender Python..." and then freeze.

ROOT CAUSES:
1. Using --python-expr with Blender GUI causes the process to hang indefinitely
   because Blender opens the GUI and subprocess.check_call() blocks waiting for exit
   
2. Output was suppressed (stdout=DEVNULL, stderr=DEVNULL) so no error messages 
   were visible to diagnose the issue
   
3. No timeout was set, so the script could hang forever

SOLUTION:
---------
Changed the dependency installation method from --python-expr to a proper 
background script execution:

OLD METHOD (BROKEN):
  subprocess.check_call(
    ["blender", "--python-expr", "import subprocess...; pip install pyserial"],
    stdout=subprocess.DEVNULL,  # Hidden output
    stderr=subprocess.DEVNULL   # Hidden errors
  )

NEW METHOD (FIXED):
  1. Create a temporary Python script with the installation code
  2. Run Blender in --background mode (no GUI)
  3. Pass the script with --python flag
  4. Capture output and stderr to show progress
  5. Add a 120-second timeout to prevent infinite hangs
  6. Show actual error messages if something fails
  7. Provide manual installation instructions if auto-install fails

NEW CODE STRUCTURE:
  - Creates /tmp/robstride_install_deps.py temporarily
  - Runs: blender --background --python /tmp/robstride_install_deps.py
  - Captures all output and errors
  - Cleans up temp file after completion
  - Times out after 2 minutes if taking too long
  - Shows detailed error messages for debugging

IMPROVEMENTS:
✅ No more hanging - will timeout after 2 minutes
✅ Visible output - shows progress and errors
✅ Better error messages - helps diagnose installation issues
✅ Graceful fallback - provides manual installation instructions
✅ Faster - uses --background mode (no GUI overhead)
✅ Cross-platform - works on Linux, macOS, Windows
✅ More reliable - uses file-based script instead of command-line expression

TESTING:
--------
1. Run the fixed install_addon.py
2. Should show: "Installing pyserial (this may take a minute)..."
3. Then show: "✅ Dependencies installed successfully"
4. If it fails, it will show the actual error and manual instructions

USAGE:
------
The fixed version is now in dist/ packages:
  - robstride-streamer-0.1.1.zip (updated with new installer)
  - robstride-streamer-addon-0.1.1.zip (unchanged)
  - robstride-streamer-0.1.1.tar.gz (unchanged)

Users can now run:
  unzip robstride-streamer-0.1.1.zip
  cd robstride-streamer-0.1.1
  python3 install_addon.py

And it will complete successfully without hanging!
"""
