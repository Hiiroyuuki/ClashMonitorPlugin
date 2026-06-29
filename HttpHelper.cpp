#include "pch.h"
#include "HttpHelper.h"
#include <sstream>

HttpHelper::HttpHelper()
    : m_hSession(nullptr)
    , m_host(L"127.0.0.1")
    , m_port(9097)
    , m_timeout(3000)
{
    m_hSession = WinHttpOpen(L"ClashMonitorPlugin/1.0",
                              WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                              WINHTTP_NO_PROXY_NAME,
                              WINHTTP_NO_PROXY_BYPASS, 0);
}

HttpHelper::~HttpHelper()
{
    if (m_hSession)
        WinHttpCloseHandle(m_hSession);
}

void HttpHelper::SetBaseUrl(const std::wstring& host, int port)
{
    m_host = host;
    m_port = port;
}

void HttpHelper::SetSecret(const std::wstring& secret)
{
    m_secret = secret;
}

void HttpHelper::SetTimeout(DWORD timeoutMs)
{
    m_timeout = timeoutMs;
}

void HttpHelper::SetLastError(const std::wstring& msg)
{
    m_lastError = msg;
}

HINTERNET HttpHelper::Connect(const std::wstring& path)
{
    if (!m_hSession) return nullptr;

    HINTERNET hConnect = WinHttpConnect(m_hSession, m_host.c_str(), m_port, 0);
    if (!hConnect) return nullptr;

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(),
                                             nullptr, WINHTTP_NO_REFERER,
                                             WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    WinHttpCloseHandle(hConnect);

    if (hRequest)
    {
        WinHttpSetTimeouts(hRequest, m_timeout, m_timeout, m_timeout, m_timeout);
    }
    return hRequest;
}

std::string HttpHelper::SendRequest(const std::wstring& path, const std::wstring& method,
                                     const std::string* body, const wchar_t* contentType)
{
    // Reset per-request state so stale results never leak into success checks
    m_lastError.clear();
    m_lastStatus = 0;

    if (!m_hSession)
    {
        SetLastError(L"Session not initialized");
        return "";
    }

    HINTERNET hConnect = WinHttpConnect(m_hSession, m_host.c_str(), m_port, 0);
    if (!hConnect)
    {
        SetLastError(L"WinHttpConnect failed");
        return "";
    }

    LPCWSTR acceptTypes[] = { L"application/json", nullptr };
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, method.c_str(), path.c_str(),
                                             nullptr, WINHTTP_NO_REFERER,
                                             acceptTypes, 0);
    WinHttpCloseHandle(hConnect);

    if (!hRequest)
    {
        SetLastError(L"WinHttpOpenRequest failed");
        return "";
    }

    WinHttpSetTimeouts(hRequest, m_timeout, m_timeout, m_timeout, m_timeout);

    // Build headers
    std::wstring headers;
    if (body && !body->empty())
    {
        headers = L"Content-Type: application/json\r\n";
    }

    // Clash API authorization
    if (!m_secret.empty())
    {
        headers += L"Authorization: Bearer " + m_secret + L"\r\n";
    }

    DWORD headerLen = headers.empty() ? 0 : static_cast<DWORD>(headers.length());

    BOOL result = WinHttpSendRequest(
        hRequest,
        headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(),
        headerLen,
        body && !body->empty() ? (LPVOID)body->data() : WINHTTP_NO_REQUEST_DATA,
        body ? (DWORD)body->size() : 0,
        body ? (DWORD)body->size() : 0,
        0);

    if (!result)
    {
        SetLastError(L"WinHttpSendRequest failed");
        WinHttpCloseHandle(hRequest);
        return "";
    }

    result = WinHttpReceiveResponse(hRequest, nullptr);
    if (!result)
    {
        SetLastError(L"WinHttpReceiveResponse failed");
        WinHttpCloseHandle(hRequest);
        return "";
    }

    // Check HTTP status
    DWORD statusCode = 0;
    DWORD statusCodeSize = sizeof(statusCode);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                         WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusCodeSize,
                         WINHTTP_NO_HEADER_INDEX);
    m_lastStatus = statusCode;

    // Check Content-Length to know expected size
    DWORD contentLength = 0;
    DWORD clSize = sizeof(contentLength);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                         WINHTTP_HEADER_NAME_BY_INDEX, &contentLength, &clSize,
                         WINHTTP_NO_HEADER_INDEX);

    std::string response;
    DWORD bytesAvailable = 0;
    // For streaming endpoints without Content-Length, read up to 1MB
    const DWORD maxRead = (contentLength > 0) ? contentLength : 1048576;
    DWORD totalRead = 0;

    while (WinHttpQueryDataAvailable(hRequest, &bytesAvailable) && bytesAvailable > 0)
    {
        DWORD toRead = bytesAvailable;
        if (totalRead + toRead > maxRead)
            toRead = maxRead - totalRead;

        std::vector<char> buffer(toRead + 1);
        DWORD bytesRead = 0;
        if (WinHttpReadData(hRequest, buffer.data(), toRead, &bytesRead))
        {
            response.append(buffer.data(), bytesRead);
            totalRead += bytesRead;
            if (totalRead >= maxRead) break;
        }
        else
        {
            break;
        }
    }

    WinHttpCloseHandle(hRequest);

    if (statusCode >= 400)
    {
        SetLastError(L"HTTP " + std::to_wstring(statusCode) + L": "
                     + std::wstring(response.begin(), response.end()));
        return "";
    }

    return response;
}

std::string HttpHelper::Get(const std::wstring& path)
{
    return SendRequest(path, L"GET");
}

bool HttpHelper::Put(const std::wstring& path, const std::string& jsonBody)
{
    // Clash returns 204 No Content (empty body) on success — judge by HTTP status only
    SendRequest(path, L"PUT", &jsonBody);
    return m_lastStatus >= 200 && m_lastStatus < 300;
}

bool HttpHelper::Patch(const std::wstring& path, const std::string& jsonBody)
{
    SendRequest(path, L"PATCH", &jsonBody);
    return m_lastStatus >= 200 && m_lastStatus < 300;
}
