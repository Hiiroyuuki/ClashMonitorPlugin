#include <windows.h>
#include <stdio.h>

int main()
{
    HMODULE h = LoadLibraryW(L"d:\\pythonProject\\trafficmonitor-plugins\\ClashMonitorPlugin\\ClashMonitorPlugin.dll");
    if (h) {
        printf("OK: DLL loaded at %p\n", h);
        FARPROC f = GetProcAddress(h, "TMPluginGetInstance");
        printf("Export: %p\n", f);
        FreeLibrary(h);
        return 0;
    } else {
        DWORD err = GetLastError();
        wchar_t buf[512];
        FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM, NULL, err, 0, buf, 512, NULL);
        wprintf(L"FAIL: %lu - %s\n", err, buf);
        return 1;
    }
}
