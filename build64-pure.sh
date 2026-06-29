#!/usr/bin/env bash
export PATH=/ucrt64/bin:$PATH
echo "Using: $(which g++)"
cd "d:/pythonProject/trafficmonitor-plugins/ClashMonitorPlugin" || exit 1
rm -f ClashMonitorPlugin.dll

# Nuclear option: -nostdlib, no C++ stdlib, no libgcc, no UCRT
# Only link against kernel32 + Windows DLLs
# This forces us to avoid all C++ features that need runtime support

g++ -std=c++17 -shared -o ClashMonitorPlugin.dll \
    dllmain.cpp ClashPlugin.cpp ClashNodeItem.cpp \
    HttpHelper.cpp JsonParser.cpp Settings.cpp \
    SystemProxy.cpp pch.cpp \
    -fno-exceptions -fno-rtti \
    -fno-unwind-tables -fno-asynchronous-unwind-tables \
    -nostdlib \
    -Wl,-e,DllMainCRTStartup \
    -lmingw32 -lgcc -lgcc_eh -lmoldname -lmingwex -lmsvcrt \
    -lkernel32 -luser32 -lgdi32 -lwinhttp -lwininet -ladvapi32 \
    -Wl,--gc-sections -O2 2>&1

ret=$?
echo "BUILD_EXIT: $ret"

if [ $ret -eq 0 ]; then
    echo "--- File type: ---"
    file ClashMonitorPlugin.dll
    echo "--- Dependencies: ---"
    objdump -p ClashMonitorPlugin.dll | grep "DLL Name:"
    echo "--- Subsystem: ---"
    objdump -p ClashMonitorPlugin.dll | grep -E "Subsystem|MajorSubsystem|MinorSubsystem"
    strip ClashMonitorPlugin.dll
    echo "--- Stripped: ---"
    ls -la ClashMonitorPlugin.dll

    # Test load
    g++ -std=c++17 loadtest.cpp -o loadtest.exe -static && ./loadtest.exe
fi
