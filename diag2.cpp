#include <windows.h>
#include <winhttp.h>
#include <stdio.h>
#include <string>
#include <cstdlib>

#pragma comment(lib, "winhttp.lib")

void TestWithHeaders(const wchar_t* host, int port, const wchar_t* secret, const wchar_t* path)
{
    printf("=== %ls ===\n", path);
    HINTERNET hReq = nullptr;
    HINTERNET hSess = WinHttpOpen(L"D/2.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    HINTERNET hConn = WinHttpConnect(hSess, host, port, 0);
    if (hConn) {
        hReq = WinHttpOpenRequest(hConn, L"GET", path, nullptr, WINHTTP_NO_REFERER, nullptr, 0);
        WinHttpCloseHandle(hConn);
    }

    if (!hReq) { printf("OpenReq fail\n"); WinHttpCloseHandle(hSess); return; }

    DWORD to = 2000;
    WinHttpSetTimeouts(hReq, to, to, to, to);

    std::wstring hdr;
    if (wcslen(secret) > 0)
        hdr = L"Authorization: Bearer " + std::wstring(secret) + L"\r\n";

    BOOL ok = WinHttpSendRequest(hReq,
        hdr.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : hdr.c_str(),
        hdr.empty() ? 0 : (DWORD)hdr.length(),
        WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    if (!ok) { printf("Send fail: %lu\n", GetLastError()); WinHttpCloseHandle(hReq); WinHttpCloseHandle(hSess); return; }

    ok = WinHttpReceiveResponse(hReq, nullptr);
    if (!ok) { printf("Recv fail: %lu\n", GetLastError()); WinHttpCloseHandle(hReq); WinHttpCloseHandle(hSess); return; }

    // Get all headers
    DWORD hdrSize = 0;
    WinHttpQueryHeaders(hReq, WINHTTP_QUERY_RAW_HEADERS_CRLF,
        WINHTTP_HEADER_NAME_BY_INDEX, WINHTTP_NO_OUTPUT_BUFFER, &hdrSize,
        WINHTTP_NO_HEADER_INDEX);
    if (hdrSize > 0)
    {
        std::wstring headers(hdrSize / sizeof(wchar_t), L'\0');
        WinHttpQueryHeaders(hReq, WINHTTP_QUERY_RAW_HEADERS_CRLF,
            WINHTTP_HEADER_NAME_BY_INDEX, &headers[0], &hdrSize,
            WINHTTP_NO_HEADER_INDEX);
        printf("Headers:\n%ls\n", headers.c_str());
    }

    // Read only first chunk
    DWORD avail = 0;
    WinHttpQueryDataAvailable(hReq, &avail);
    printf("Available: %lu bytes\n", avail);
    if (avail > 0)
    {
        std::string buf(avail, '\0');
        DWORD read = 0;
        WinHttpReadData(hReq, &buf[0], avail, &read);
        printf("Read: %lu bytes\n", read);
        printf("Body: %s\n", buf.c_str());
    }

    // Check if more data is available (streaming test)
    avail = 0;
    if (WinHttpQueryDataAvailable(hReq, &avail) && avail > 0)
        printf("WARNING: MORE DATA AVAILABLE (%lu bytes) - may be streaming!\n", avail);
    else
        printf("No more data - connection complete.\n");

    WinHttpCloseHandle(hReq);
    WinHttpCloseHandle(hSess);
    printf("\n");
}

int main()
{
    TestWithHeaders(L"127.0.0.1", 9097, L"admin", L"/traffic");
    TestWithHeaders(L"127.0.0.1", 9097, L"admin", L"/proxies");
    TestWithHeaders(L"127.0.0.1", 9097, L"admin", L"/configs");
    return 0;
}
