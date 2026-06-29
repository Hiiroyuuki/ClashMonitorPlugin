#include <windows.h>
#include <winhttp.h>
#include <stdio.h>
#include <string>

#pragma comment(lib, "winhttp.lib")

void TestEndpoint(const wchar_t* host, int port, const wchar_t* secret, const wchar_t* path)
{
    printf("  GET %ls  ... ", path);
    HINTERNET hSession = WinHttpOpen(L"Diag/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) { printf("session fail\n"); return; }

    HINTERNET hConnect = WinHttpConnect(hSession, host, port, 0);
    if (!hConnect) { printf("connect fail (err=%lu)\n", GetLastError()); WinHttpCloseHandle(hSession); return; }

    HINTERNET hReq = WinHttpOpenRequest(hConnect, L"GET", path, nullptr, WINHTTP_NO_REFERER, nullptr, 0);
    WinHttpCloseHandle(hConnect);
    if (!hReq) { printf("request fail (err=%lu)\n", GetLastError()); WinHttpCloseHandle(hSession); return; }

    DWORD timeout = 2000;
    WinHttpSetTimeouts(hReq, timeout, timeout, timeout, timeout);

    BOOL result;
    if (wcslen(secret) > 0)
    {
        std::wstring hdr = L"Authorization: Bearer " + std::wstring(secret) + L"\r\n";
        result = WinHttpSendRequest(hReq, hdr.c_str(), (DWORD)hdr.length(), WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    }
    else
    {
        result = WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    }

    if (!result)
    {
        DWORD err = GetLastError();
        if (err == 12002)       printf("TIMEOUT\n");
        else if (err == 12029)  printf("REFUSED\n");
        else if (err == 12007)  printf("NO HOST\n");
        else                    printf("SEND FAIL (err=%lu)\n", err);
        WinHttpCloseHandle(hReq);
        WinHttpCloseHandle(hSession);
        return;
    }

    result = WinHttpReceiveResponse(hReq, nullptr);
    if (!result) { printf("RECV FAIL (err=%lu)\n", GetLastError()); WinHttpCloseHandle(hReq); WinHttpCloseHandle(hSession); return; }

    DWORD status = 0, size = sizeof(status);
    WinHttpQueryHeaders(hReq, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);

    DWORD avail = 0;
    std::string body;
    while (WinHttpQueryDataAvailable(hReq, &avail) && avail > 0) {
        std::string buf(avail, '\0');
        DWORD read = 0;
        if (WinHttpReadData(hReq, &buf[0], avail, &read))
            body.append(buf.data(), read);
        else break;
    }

    if (status == 200) {
        printf("OK (%zu bytes)\n", body.size());
        if (body.size() <= 200) printf("    %s\n", body.c_str());
    } else if (status == 401 || status == 403) {
        printf("AUTH FAIL (HTTP %lu)\n", status);
    } else {
        printf("HTTP %lu (%zu bytes)\n", status, body.size());
    }

    WinHttpCloseHandle(hReq);
    WinHttpCloseHandle(hSession);
}

int main(int argc, char* argv[])
{
    wchar_t host[256] = L"127.0.0.1";
    int port = 9097;
    wchar_t secret[256] = L"admin";
    wchar_t extraPortStr[32] = L"";

    if (argc > 1) { mbstowcs(host, argv[1], 255); host[255] = 0; }
    if (argc > 2) port = atoi(argv[2]);
    if (argc > 3) { mbstowcs(secret, argv[3], 255); secret[255] = 0; }
    if (argc > 4) { mbstowcs(extraPortStr, argv[4], 31); extraPortStr[31] = 0; }

    printf("=== Clash API Diagnostic ===\n");
    printf("Host: %ls  Port: %d  Secret: \"%ls\"\n\n", host, port, secret);

    TestEndpoint(host, port, secret, L"/version");
    TestEndpoint(host, port, secret, L"/traffic");
    TestEndpoint(host, port, secret, L"/configs");
    TestEndpoint(host, port, secret, L"/proxies");

    if (wcslen(extraPortStr) > 0) {
        int p2 = _wtoi(extraPortStr);
        if (p2 > 0 && p2 != port) {
            printf("\n--- Also testing port %d ---\n", p2);
            TestEndpoint(host, p2, secret, L"/version");
            TestEndpoint(host, p2, secret, L"/traffic");
        }
    }

    return 0;
}
