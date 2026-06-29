#!/usr/bin/env bash
export PATH=/mingw32/bin:$PATH
echo "Using: $(which g++)"
echo "Target: $(g++ -dumpmachine)"
cd "d:/pythonProject/trafficmonitor-plugins/ClashMonitorPlugin" || exit 1
rm -f ClashMonitorPlugin.dll libwinpthread-1.dll

# 32-bit MINGW32 build for 32-bit TrafficMonitor
# MSVCRT-based (no UCRT API sets)
g++ -std=c++17 -shared -o ClashMonitorPlugin.dll \
    dllmain.cpp ClashPlugin.cpp ClashNodeItem.cpp \
    HttpHelper.cpp JsonParser.cpp Settings.cpp \
    SystemProxy.cpp pch.cpp \
    -fno-exceptions -fno-rtti \
    -static -static-libgcc -static-libstdc++ \
    -Wl,--gc-sections -O2 \
    -lwinhttp -lwininet -lgdi32 -luser32 2>&1
# -static required — std::mutex pulls libwinpthread-1.dll otherwise.
# Must build with the 32-bit (i686) toolchain for a 32-bit TrafficMonitor,
# else loading fails with error 193 (not a valid Win32 application).

ret=$?
echo "BUILD_EXIT: $ret"

if [ $ret -eq 0 ]; then
    echo "--- File type (expect PE32, i386): ---"
    file ClashMonitorPlugin.dll
    echo "--- Dependencies (should be system DLLs only): ---"
    objdump -p ClashMonitorPlugin.dll | grep "DLL Name:"
    strip ClashMonitorPlugin.dll
    echo "--- Final: ---"
    ls -la ClashMonitorPlugin.dll
fi
