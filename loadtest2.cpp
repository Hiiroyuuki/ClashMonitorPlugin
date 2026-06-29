#include <windows.h>
#include <stdio.h>

int main()
{
    // Test 1: Plain LoadLibrary (what TrafficMonitor uses)
    HMODULE h1 = LoadLibraryW(L"ClashMonitorPlugin.dll");
    printf("Test 1 - LoadLibrary:  %s (err=%lu)\n", h1 ? "OK" : "FAIL", GetLastError());
    if (h1) { FreeLibrary(h1); h1 = NULL; }

    // Test 2: Full path LoadLibrary
    wchar_t fullPath[MAX_PATH];
    GetFullPathNameW(L"ClashMonitorPlugin.dll", MAX_PATH, fullPath, NULL);
    HMODULE h2 = LoadLibraryW(fullPath);
    printf("Test 2 - Full path:     %s (err=%lu)\n", h2 ? "OK" : "FAIL", GetLastError());
    printf("         Path: %ls\n", fullPath);
    if (h2) { FreeLibrary(h2); h2 = NULL; }

    // Test 3: Check architecture
    USHORT arch;
    BOOL isWow64 = FALSE;
    IsWow64Process(GetCurrentProcess(), &isWow64);
    printf("Test 3 - Process:       %s-bit, WOW64=%d\n",
           sizeof(void*) == 8 ? "64" : "32", isWow64);

    // Test 4: Try with SetDllDirectory
    wchar_t dir[MAX_PATH];
    GetCurrentDirectoryW(MAX_PATH, dir);
    SetDllDirectoryW(dir);
    HMODULE h4 = LoadLibraryW(L"ClashMonitorPlugin.dll");
    printf("Test 4 - SetDllDir:     %s (err=%lu)\n", h4 ? "OK" : "FAIL", GetLastError());
    if (h4) FreeLibrary(h4);

    return 0;
}
