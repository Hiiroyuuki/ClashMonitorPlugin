#!/usr/bin/env bash
export PATH=/mingw64/bin:$PATH
echo "Using: $(which g++)"
echo "Target: $(g++ -dumpmachine)"
cd "d:/pythonProject/trafficmonitor-plugins/ClashMonitorPlugin" || exit 1
rm -f ClashMonitorPlugin.dll

# mingw64 (MSVCRT-based, no UCRT)
# Set subsystem version higher, use proper DLL flags
g++ -std=c++17 -shared -o ClashMonitorPlugin.dll \
    dllmain.cpp ClashPlugin.cpp ClashNodeItem.cpp \
    HttpHelper.cpp JsonParser.cpp Settings.cpp \
    SystemProxy.cpp pch.cpp \
    -fno-exceptions -fno-rtti \
    -static-libgcc -static-libstdc++ \
    -Wl,-Bstatic -lwinpthread -Wl,-Bdynamic \
    -lwinhttp -lwininet -lgdi32 -luser32 \
    -Wl,--major-os-version,6 -Wl,--minor-os-version,0 \
    -Wl,--gc-sections -O2 2>&1

ret=$?
echo "BUILD_EXIT: $ret"

if [ $ret -eq 0 ]; then
    echo "--- File type: ---"
    file ClashMonitorPlugin.dll
    echo "--- Dependencies: ---"
    objdump -p ClashMonitorPlugin.dll | grep "DLL Name:"
    objdump -p ClashMonitorPlugin.dll | grep "Subsystem"
    strip ClashMonitorPlugin.dll
    echo "--- Stripped: ---"
    ls -la ClashMonitorPlugin.dll
    # Test load
    g++ -std=c++17 loadtest.cpp -o loadtest.exe -static && ./loadtest.exe
fi
