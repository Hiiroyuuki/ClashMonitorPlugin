#include "pch.h"
#include "SystemProxy.h"
#include <wininet.h>

#pragma comment(lib, "wininet.lib")

namespace SystemProxy
{

bool Enable(const wchar_t* proxyServer, const wchar_t* bypassList)
{
    INTERNET_PER_CONN_OPTION_LISTW list;
    INTERNET_PER_CONN_OPTIONW options[3];
    DWORD bufSize = sizeof(list);

    options[0].dwOption = INTERNET_PER_CONN_FLAGS;
    options[0].Value.dwValue = PROXY_TYPE_PROXY;

    options[1].dwOption = INTERNET_PER_CONN_PROXY_SERVER;
    options[1].Value.pszValue = const_cast<wchar_t*>(proxyServer);

    options[2].dwOption = INTERNET_PER_CONN_PROXY_BYPASS;
    options[2].Value.pszValue = const_cast<wchar_t*>(bypassList);

    list.dwSize = sizeof(list);
    list.pszConnection = nullptr;
    list.dwOptionCount = 3;
    list.dwOptionError = 0;
    list.pOptions = options;

    BOOL result = InternetSetOptionW(nullptr, INTERNET_OPTION_PER_CONNECTION_OPTION,
                                      &list, bufSize);
    if (!result) return false;

    InternetSetOptionW(nullptr, INTERNET_OPTION_SETTINGS_CHANGED, nullptr, 0);
    InternetSetOptionW(nullptr, INTERNET_OPTION_REFRESH, nullptr, 0);
    return true;
}

bool Disable()
{
    INTERNET_PER_CONN_OPTION_LISTW list;
    INTERNET_PER_CONN_OPTIONW options[1];
    DWORD bufSize = sizeof(list);

    options[0].dwOption = INTERNET_PER_CONN_FLAGS;
    options[0].Value.dwValue = PROXY_TYPE_DIRECT;

    list.dwSize = sizeof(list);
    list.pszConnection = nullptr;
    list.dwOptionCount = 1;
    list.dwOptionError = 0;
    list.pOptions = options;

    BOOL result = InternetSetOptionW(nullptr, INTERNET_OPTION_PER_CONNECTION_OPTION,
                                      &list, bufSize);
    if (!result) return false;

    InternetSetOptionW(nullptr, INTERNET_OPTION_SETTINGS_CHANGED, nullptr, 0);
    InternetSetOptionW(nullptr, INTERNET_OPTION_REFRESH, nullptr, 0);
    return true;
}

bool IsEnabled()
{
    INTERNET_PER_CONN_OPTION_LISTW list;
    INTERNET_PER_CONN_OPTIONW options[1];
    DWORD bufSize = sizeof(list);

    options[0].dwOption = INTERNET_PER_CONN_FLAGS;
    options[0].Value.dwValue = 0;

    list.dwSize = sizeof(list);
    list.pszConnection = nullptr;
    list.dwOptionCount = 1;
    list.dwOptionError = 0;
    list.pOptions = options;

    if (!InternetQueryOptionW(nullptr, INTERNET_OPTION_PER_CONNECTION_OPTION, &list, &bufSize))
    {
        return false;
    }

    return (options[0].Value.dwValue & PROXY_TYPE_PROXY) != 0;
}

std::wstring GetCurrentProxyServer()
{
    INTERNET_PER_CONN_OPTION_LISTW list;
    INTERNET_PER_CONN_OPTIONW options[1];
    DWORD bufSize = sizeof(list);

    wchar_t buf[256] = {0};
    options[0].dwOption = INTERNET_PER_CONN_PROXY_SERVER;
    options[0].Value.pszValue = buf;

    list.dwSize = sizeof(list);
    list.pszConnection = nullptr;
    list.dwOptionCount = 1;
    list.dwOptionError = 0;
    list.pOptions = options;

    if (!InternetQueryOptionW(nullptr, INTERNET_OPTION_PER_CONNECTION_OPTION, &list, &bufSize))
    {
        return L"";
    }

    if (bufSize > sizeof(buf))
    {
        std::wstring result(bufSize / sizeof(wchar_t) + 1, L'\0');
        options[0].Value.pszValue = result.data();
        list.pOptions = options;
        if (!InternetQueryOptionW(nullptr, INTERNET_OPTION_PER_CONNECTION_OPTION, &list, &bufSize))
        {
            return L"";
        }
        result.resize(wcslen(result.c_str()));
        return result;
    }

    return std::wstring(buf);
}

bool Toggle(const wchar_t* proxyServer, const wchar_t* bypassList)
{
    if (IsEnabled())
        return !Disable();
    else
        return Enable(proxyServer, bypassList);
}

} // namespace SystemProxy
