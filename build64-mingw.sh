#!/usr/bin/env bash
export PATH=/mingw64/bin:$PATH
echo "Using: $(which g++)"
echo "Target: $(g++ -dumpmachine)"
cd "d:/pythonProject/trafficmonitor-plugins/ClashMonitorPlugin" || exit 1
rm -f ClashMonitorPlugin.dll

# mingw64 = MSVCRT-based (NOT UCRT)
# Links against msvcrt.dll which is present on ALL Windows systems
g++ -std=c++17 -shared -o ClashMonitorPlugin.dll \
    dllmain.cpp ClashPlugin.cpp ClashNodeItem.cpp \
    HttpHelper.cpp JsonParser.cpp Settings.cpp \
    SystemProxy.cpp pch.cpp \
    -fno-exceptions -fno-rtti \
    -static -static-libgcc -static-libstdc++ \
    -Wl,--gc-sections -O2 \
    -lwinhttp -lwininet -lgdi32 -luser32 -lcomdlg32 2>&1
# NOTE: -static is required — std::mutex pulls libwinpthread-1.dll otherwise

ret=$?
echo "BUILD_EXIT: $ret"

if [ $ret -eq 0 ]; then
    echo "--- File type: ---"
    file ClashMonitorPlugin.dll
    echo "--- Size: ---"
    ls -la ClashMonitorPlugin.dll
    echo "--- Dependencies: ---"
    objdump -p ClashMonitorPlugin.dll | grep "DLL Name:"
    echo "--- Subsystem: ---"
    objdump -p ClashMonitorPlugin.dll | grep -E "Subsystem|MajorSub|MinorSub"
    echo "--- Load test: ---"
    g++ -std=c++17 loadtest.cpp -o loadtest.exe -static && ./loadtest.exe

    strip ClashMonitorPlugin.dll
    echo "--- Stripped: ---"
    ls -la ClashMonitorPlugin.dll
fi
