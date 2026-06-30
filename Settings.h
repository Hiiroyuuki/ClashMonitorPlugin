#pragma once
#include <string>
#include <windows.h>

// Plugin configuration persisted to ini file
class Settings
{
public:
    static constexpr const wchar_t* DEFAULT_API_HOST = L"127.0.0.1";
    static constexpr int DEFAULT_API_PORT = 9097;
    static constexpr const wchar_t* DEFAULT_API_SECRET = L"admin";
    static constexpr const wchar_t* DEFAULT_SYSTEM_PROXY_HOST = L"127.0.0.1";
    static constexpr const wchar_t* DEFAULT_SYSTEM_PROXY_BYPASS = L"localhost;127.*;10.*;172.16.*;172.17.*;172.18.*;172.19.*;172.20.*;172.21.*;172.22.*;172.23.*;172.24.*;172.25.*;172.26.*;172.27.*;172.28.*;172.29.*;172.30.*;172.31.*;192.168.*;<local>";

    Settings();

    void Load(const std::wstring& configDir);
    void Save(const std::wstring& configDir);

    // Clash API connection
    std::wstring apiHost = DEFAULT_API_HOST;
    int apiPort = DEFAULT_API_PORT;
    std::wstring apiSecret = DEFAULT_API_SECRET;
    DWORD apiTimeoutMs = 3000;
    DWORD refreshIntervalMs = 2000;

    // Proxy group to monitor/control (default: first Selector group found)
    std::wstring proxyGroup = L"";

    // Subscription/provider to display usage for (default: first provider with usage)
    std::wstring subscriptionName = L"";

    // Clash Verge profiles.yaml discovery. When empty and auto-discovery is on,
    // the plugin searches common AppData locations instead of using a fixed path.
    bool autoDiscoverProfiles = true;
    std::wstring profilesPath = L"";

    // Windows system proxy endpoint. Port is still refreshed from Clash /configs
    // when available; this host lets non-local Clash setups be configured.
    std::wstring systemProxyHost = DEFAULT_SYSTEM_PROXY_HOST;
    std::wstring systemProxyBypass = DEFAULT_SYSTEM_PROXY_BYPASS;

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
