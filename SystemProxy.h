#pragma once

// Toggle Windows system proxy on/off
namespace SystemProxy
{
    // Enable system proxy pointing to Clash (default 127.0.0.1:7890)
    bool Enable(const wchar_t* proxyServer = L"127.0.0.1:7890",
                const wchar_t* bypassList = L"localhost;127.*;10.*;172.16.*;172.17.*;172.18.*;172.19.*;172.20.*;172.21.*;172.22.*;172.23.*;172.24.*;172.25.*;172.26.*;172.27.*;172.28.*;172.29.*;172.30.*;172.31.*;192.168.*;<local>");

    // Disable system proxy
    bool Disable();

    // Check if system proxy is enabled
    bool IsEnabled();

    // Get current proxy server string
    std::wstring GetCurrentProxyServer();

    // Toggle: return new state (true=enabled)
    bool Toggle();
};
