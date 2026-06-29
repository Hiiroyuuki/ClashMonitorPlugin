#pragma once
#include <string>
#include <windows.h>

// Plugin configuration persisted to ini file
class Settings
{
public:
    Settings();

    void Load(const std::wstring& configDir);
    void Save(const std::wstring& configDir);

    // Clash API connection
    std::wstring apiHost = L"127.0.0.1";
    int apiPort = 9097;
    std::wstring apiSecret = L"admin";
    DWORD apiTimeoutMs = 3000;
    DWORD refreshIntervalMs = 2000;

    // Proxy group to monitor/control (default: first Selector group found)
    std::wstring proxyGroup = L"";

    // Auto-detect proxy group on startup
    bool autoDetectGroup = true;

    // Display options
    bool showNodeDelay = true;
    bool showUpDownSpeed = true;

private:
    std::wstring GetIniPath(const std::wstring& configDir);
};

// Format bytes per second to human readable string
std::wstring FormatSpeed(unsigned long long bytesPerSec);
