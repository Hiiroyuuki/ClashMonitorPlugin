#!/usr/bin/env bash
export PATH=/mingw32/bin:$PATH
echo "Using: $(which g++)"
cd "d:/pythonProject/trafficmonitor-plugins/ClashMonitorPlugin" || exit 1
rm -f ClashMonitorPlugin.dll

g++ -std=c++17 -shared -o ClashMonitorPlugin.dll \
    dllmain.cpp ClashPlugin.cpp ClashNodeItem.cpp \
    HttpHelper.cpp JsonParser.cpp Settings.cpp \
    SystemProxy.cpp pch.cpp \
    -fno-exceptions -fno-rtti \
    -static-libgcc -static-libstdc++ \
    -lwinhttp -lwininet -lgdi32 -luser32 \
    -Wl,--gc-sections \
    -Os 2>&1

ret=$?
echo "BUILD_EXIT: $ret"

if [ $ret -eq 0 ]; then
    ls -la ClashMonitorPlugin.dll
    echo "--- Dependencies: ---"
    objdump -p ClashMonitorPlugin.dll | grep "DLL Name:"
    strip ClashMonitorPlugin.dll
    echo "--- Stripped: ---"
    ls -la ClashMonitorPlugin.dll
    echo ""
    echo "NOTE: Copy libwinpthread-1.dll alongside ClashMonitorPlugin.dll in plugins/"
fi
