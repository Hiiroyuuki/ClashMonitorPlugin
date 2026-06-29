#include "pch.h"
#include "Settings.h"
#include <sstream>
#include <iomanip>

Settings::Settings()
{
}

std::wstring Settings::GetIniPath(const std::wstring& configDir)
{
    return configDir + L"\\ClashMonitor.ini";
}

void Settings::Load(const std::wstring& configDir)
{
    std::wstring iniPath = GetIniPath(configDir);

    wchar_t buf[256];

    GetPrivateProfileStringW(L"Connection", L"Host", L"127.0.0.1", buf, 256, iniPath.c_str());
    apiHost = buf;

    apiPort = GetPrivateProfileIntW(L"Connection", L"Port", 9097, iniPath.c_str());

    GetPrivateProfileStringW(L"Connection", L"Secret", L"admin", buf, 256, iniPath.c_str());
    apiSecret = buf;

    apiTimeoutMs = GetPrivateProfileIntW(L"Connection", L"Timeout", 3000, iniPath.c_str());
    refreshIntervalMs = GetPrivateProfileIntW(L"Connection", L"RefreshInterval", 2000, iniPath.c_str());

    GetPrivateProfileStringW(L"Proxy", L"Group", L"", buf, 256, iniPath.c_str());
    proxyGroup = buf;

    autoDetectGroup = GetPrivateProfileIntW(L"Proxy", L"AutoDetectGroup", 1, iniPath.c_str()) != 0;
    showNodeDelay = GetPrivateProfileIntW(L"Display", L"ShowNodeDelay", 1, iniPath.c_str()) != 0;
    showUpDownSpeed = GetPrivateProfileIntW(L"Display", L"ShowUpDownSpeed", 1, iniPath.c_str()) != 0;
}

void Settings::Save(const std::wstring& configDir)
{
    std::wstring iniPath = GetIniPath(configDir);

    WritePrivateProfileStringW(L"Connection", L"Host", apiHost.c_str(), iniPath.c_str());
    WritePrivateProfileStringW(L"Connection", L"Port", std::to_wstring(apiPort).c_str(), iniPath.c_str());
    WritePrivateProfileStringW(L"Connection", L"Secret", apiSecret.c_str(), iniPath.c_str());
    WritePrivateProfileStringW(L"Connection", L"Timeout", std::to_wstring(apiTimeoutMs).c_str(), iniPath.c_str());
    WritePrivateProfileStringW(L"Connection", L"RefreshInterval", std::to_wstring(refreshIntervalMs).c_str(), iniPath.c_str());

    WritePrivateProfileStringW(L"Proxy", L"Group", proxyGroup.c_str(), iniPath.c_str());
    WritePrivateProfileStringW(L"Proxy", L"AutoDetectGroup", autoDetectGroup ? L"1" : L"0", iniPath.c_str());

    WritePrivateProfileStringW(L"Display", L"ShowNodeDelay", showNodeDelay ? L"1" : L"0", iniPath.c_str());
    WritePrivateProfileStringW(L"Display", L"ShowUpDownSpeed", showUpDownSpeed ? L"1" : L"0", iniPath.c_str());
}

std::wstring FormatSpeed(unsigned long long bytesPerSec)
{
    if (bytesPerSec < 1024ULL)
    {
        return std::to_wstring(bytesPerSec) + L" B/s";
    }
    else if (bytesPerSec < 1024ULL * 1024ULL)
    {
        std::wostringstream ss;
        ss << std::fixed << std::setprecision(1) << (bytesPerSec / 1024.0) << L" KB/s";
        return ss.str();
    }
    else if (bytesPerSec < 1024ULL * 1024ULL * 1024ULL)
    {
        std::wostringstream ss;
        ss << std::fixed << std::setprecision(1) << (bytesPerSec / (1024.0 * 1024.0)) << L" MB/s";
        return ss.str();
    }
    else
    {
        std::wostringstream ss;
        ss << std::fixed << std::setprecision(2) << (bytesPerSec / (1024.0 * 1024.0 * 1024.0)) << L" GB/s";
        return ss.str();
    }
}
