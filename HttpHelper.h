#pragma once
#include <string>
#include <windows.h>
#include <winhttp.h>

#pragma comment(lib, "winhttp.lib")

class HttpHelper
{
public:
    HttpHelper();
    ~HttpHelper();

    void SetBaseUrl(const std::wstring& host, int port);
    void SetSecret(const std::wstring& secret);
    void SetTimeout(DWORD timeoutMs);

    std::string Get(const std::wstring& path);
    bool Put(const std::wstring& path, const std::string& jsonBody);
    bool Patch(const std::wstring& path, const std::string& jsonBody);

    std::wstring GetLastError() const { return m_lastError; }
    DWORD LastStatus() const { return m_lastStatus; }

private:
    HINTERNET m_hSession;
    std::wstring m_host;
    int m_port;
    std::wstring m_secret;
    DWORD m_timeout;
    std::wstring m_lastError;
    DWORD m_lastStatus = 0;

    HINTERNET Connect(const std::wstring& path);
    std::string SendRequest(const std::wstring& path, const std::wstring& method,
                            const std::string* body = nullptr, const wchar_t* contentType = nullptr);
    void SetLastError(const std::wstring& msg);
};
