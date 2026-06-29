#!/usr/bin/env bash
export PATH=/ucrt64/bin:$PATH
echo "Using: $(which g++)"
cd "d:/pythonProject/trafficmonitor-plugins/ClashMonitorPlugin" || exit 1
rm -f ClashMonitorPlugin.dll libwinpthread-1.dll

# Manually specify all libs in order: static GNU libs, then Windows libs
# -nodefaultlibs: don't let gcc driver add default libs at end
# -lgcc -lgcc_eh -lstdc++ -lwinpthread: all before Bdynamic
g++ -std=c++17 -shared -o ClashMonitorPlugin.dll \
    dllmain.cpp ClashPlugin.cpp ClashNodeItem.cpp \
    HttpHelper.cpp JsonParser.cpp Settings.cpp \
    SystemProxy.cpp pch.cpp \
    -fno-exceptions -fno-rtti \
    -nodefaultlibs \
    -Wl,-Bstatic -lstdc++ -lgcc -lgcc_eh -lwinpthread -Wl,-Bdynamic \
    -lmingw32 -lmoldname -lmingwex -lucrt \
    -lwinhttp -lwininet -lgdi32 -luser32 -lkernel32 -ladvapi32 -lshell32 \
    2>&1

ret=$?
echo "BUILD_EXIT: $ret"

if [ $ret -eq 0 ]; then
    echo "--- File type: ---"
    file ClashMonitorPlugin.dll
    echo "--- Dependencies: ---"
    objdump -p ClashMonitorPlugin.dll | grep "DLL Name:"
    strip ClashMonitorPlugin.dll
    echo "--- Stripped: ---"
    ls -la ClashMonitorPlugin.dll
fi
