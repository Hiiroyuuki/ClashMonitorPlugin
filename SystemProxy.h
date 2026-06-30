#pragma once

// Toggle Windows system proxy on/off
namespace SystemProxy
{
    // Enable system proxy pointing to Clash.
    bool Enable(const wchar_t* proxyServer, const wchar_t* bypassList);

    // Disable system proxy
    bool Disable();

    // Check if system proxy is enabled
    bool IsEnabled();

    // Get current proxy server string
    std::wstring GetCurrentProxyServer();

    // Toggle: return new state (true=enabled)
    bool Toggle(const wchar_t* proxyServer, const wchar_t* bypassList);
};
