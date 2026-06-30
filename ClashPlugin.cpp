#include "pch.h"
#include "ClashPlugin.h"
#include "JsonParser.h"
#include "Settings.h"
#include "SystemProxy.h"
#include <commdlg.h>
#include <sstream>
#include <algorithm>
#include <unordered_map>
#include <cstdio>
#include <thread>
#include <cwctype>
#include <cstring>
// Win32 MultiByteToWideChar / WideCharToMultiByte used for UTF conversion

// ---------------------------------------------------------------------------
// UTF-8 <-> Wide string conversion helpers
// ---------------------------------------------------------------------------
static std::wstring UTF8ToWide(const std::string& utf8)
{
    if (utf8.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring result(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), &result[0], len);
    return result;
}

static std::string WideToUTF8(const std::wstring& wide)
{
    if (wide.empty()) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    std::string result(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), &result[0], len, nullptr, nullptr);
    return result;
}

static std::string JsonEscape(const std::string& s)
{
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s)
    {
        switch (c)
        {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        default:   out += c;      break;
        }
    }
    return out;
}

static std::string TrimCopy(const std::string& s)
{
    size_t b = 0, e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' || s[b] == '\n')) b++;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' || s[e - 1] == '\n')) e--;
    return s.substr(b, e - b);
}

static bool StartsWith(const std::string& s, const char* prefix)
{
    size_t n = strlen(prefix);
    return s.size() >= n && s.compare(0, n, prefix) == 0;
}

static std::string YamlValue(const std::string& line, const char* key)
{
    std::string t = TrimCopy(line);
    std::string p = std::string(key) + ":";
    if (!StartsWith(t, p.c_str())) return "";
    std::string v = TrimCopy(t.substr(p.size()));
    if (v == "null") return "";
    if (v.size() >= 2 && ((v.front() == '"' && v.back() == '"') || (v.front() == '\'' && v.back() == '\'')))
        v = v.substr(1, v.size() - 2);
    return v;
}

static int IndentOf(const std::string& line)
{
    int n = 0;
    while (n < static_cast<int>(line.size()) && line[n] == ' ') n++;
    return n;
}

static unsigned long long ParseUInt64(const std::string& s)
{
    unsigned long long v = 0;
    for (char c : TrimCopy(s))
    {
        if (c < '0' || c > '9') break;
        v = v * 10ULL + static_cast<unsigned long long>(c - '0');
    }
    return v;
}

static std::wstring LowerWide(std::wstring s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
    return s;
}

static std::string ReadFileUtf8(const std::wstring& path)
{
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) return "";
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0) { fclose(f); return ""; }
    std::string data(static_cast<size_t>(n), '\0');
    size_t r = fread(&data[0], 1, static_cast<size_t>(n), f);
    fclose(f);
    data.resize(r);
    return data;
}

static bool FileExists(const std::wstring& path)
{
    DWORD attr = GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

static bool DirNameLooksLikeClashApp(const std::wstring& name)
{
    std::wstring n = LowerWide(name);
    return n.find(L"clash") != std::wstring::npos ||
           n.find(L"verge") != std::wstring::npos ||
           n.find(L"mihomo") != std::wstring::npos;
}

// Percent-encode a UTF-8 string for use in a URL path segment.
// Needed because group names contain non-ASCII (e.g. "节点选择") and WinHttp
// does not reliably encode raw wide path characters.
static std::string UrlEncode(const std::string& s)
{
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s)
    {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~')
            out += static_cast<char>(c);
        else
        {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 0xF];
        }
    }
    return out;
}

// Human-readable delay suffix for menu labels
static std::wstring DelaySuffix(int delay)
{
    if (delay > 0)  return L"    " + std::to_wstring(delay) + L" ms";
    if (delay == 0) return L"    timeout";
    return L"    --";
}

static std::wstring FormatUsagePercent(unsigned long long used, unsigned long long total)
{
    if (total == 0) return L"--";
    int pct = static_cast<int>((used * 100ULL + total / 2ULL) / total);
    if (pct < 0) pct = 0;
    if (pct > 999) pct = 999;
    return std::to_wstring(pct) + L"%";
}

// Left-click popup menu command ids
namespace {
    constexpr UINT ID_MODE_RULE    = 2;
    constexpr UINT ID_MODE_GLOBAL  = 3;
    constexpr UINT ID_MODE_DIRECT  = 4;

    constexpr int IDC_OPT_HOST          = 1001;
    constexpr int IDC_OPT_PORT          = 1002;
    constexpr int IDC_OPT_SECRET        = 1003;
    constexpr int IDC_OPT_TIMEOUT       = 1004;
    constexpr int IDC_OPT_REFRESH       = 1005;
    constexpr int IDC_OPT_PROXY_HOST    = 1006;
    constexpr int IDC_OPT_PROXY_BYPASS  = 1007;
    constexpr int IDC_OPT_AUTO_GROUP    = 1008;
    constexpr int IDC_OPT_GROUP         = 1009;
    constexpr int IDC_OPT_AUTO_PROFILE  = 1010;
    constexpr int IDC_OPT_PROFILE_PATH  = 1011;
    constexpr int IDC_OPT_BROWSE        = 1012;
    constexpr int IDC_OPT_SUB_NAME      = 1013;
    constexpr int IDC_OPT_RESOLVED      = 1014;
    constexpr int IDC_OPT_SAVE          = 1015;
    constexpr int IDC_OPT_CANCEL        = 1016;
}

namespace {

struct OptionsState
{
    Settings value;
    std::wstring resolvedProfiles;
    std::wstring usageSummary;
    bool saved = false;
    bool done = false;
    HFONT font = nullptr;
    HFONT titleFont = nullptr;
    HBRUSH bgBrush = nullptr;
    HBRUSH fieldBrush = nullptr;
};

std::wstring GetControlText(HWND parent, int id)
{
    HWND h = GetDlgItem(parent, id);
    if (!h) return L"";
    int len = GetWindowTextLengthW(h);
    std::wstring text(static_cast<size_t>(len + 1), L'\0');
    if (len > 0)
        GetWindowTextW(h, &text[0], len + 1);
    text.resize(static_cast<size_t>(len));
    return text;
}

void SetControlText(HWND parent, int id, const std::wstring& text)
{
    HWND h = GetDlgItem(parent, id);
    if (h) SetWindowTextW(h, text.c_str());
}

int ReadInt(HWND parent, int id, int fallback)
{
    std::wstring s = GetControlText(parent, id);
    if (s.empty()) return fallback;
    int v = _wtoi(s.c_str());
    return v;
}

HWND AddLabel(HWND parent, OptionsState* st, const wchar_t* text, int x, int y, int w, int h)
{
    HWND c = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE,
                             x, y, w, h, parent, nullptr, nullptr, nullptr);
    SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(st->font), TRUE);
    return c;
}

HWND AddLabelId(HWND parent, OptionsState* st, int id, const wchar_t* text, int x, int y, int w, int h)
{
    HWND c = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE,
                             x, y, w, h, parent, reinterpret_cast<HMENU>(id), nullptr, nullptr);
    SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(st->font), TRUE);
    return c;
}

HWND AddEdit(HWND parent, OptionsState* st, int id, const std::wstring& text,
             int x, int y, int w, int h)
{
    HWND c = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", text.c_str(),
                             WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                             x, y, w, h, parent, reinterpret_cast<HMENU>(id), nullptr, nullptr);
    SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(st->font), TRUE);
    return c;
}

HWND AddCheck(HWND parent, OptionsState* st, int id, const wchar_t* text,
              bool checked, int x, int y, int w, int h)
{
    HWND c = CreateWindowExW(0, L"BUTTON", text,
                             WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                             x, y, w, h, parent, reinterpret_cast<HMENU>(id), nullptr, nullptr);
    SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(st->font), TRUE);
    SendMessageW(c, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
    return c;
}

HWND AddButton(HWND parent, OptionsState* st, int id, const wchar_t* text,
               int x, int y, int w, int h)
{
    HWND c = CreateWindowExW(0, L"BUTTON", text,
                             WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                             x, y, w, h, parent, reinterpret_cast<HMENU>(id), nullptr, nullptr);
    SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(st->font), TRUE);
    return c;
}

std::wstring ResolvedProfilesLabel(const OptionsState* st)
{
    if (!st || st->resolvedProfiles.empty())
        return L"Resolved profiles: not found";
    return L"Resolved profiles: " + st->resolvedProfiles;
}

void PopulateOptions(HWND h, OptionsState* st)
{
    AddLabel(h, st, L"Clash API", 40, 78, 100, 20);
    AddLabel(h, st, L"Host", 48, 108, 74, 22);
    AddEdit(h, st, IDC_OPT_HOST, st->value.apiHost, 124, 104, 150, 24);
    AddLabel(h, st, L"Port", 292, 108, 50, 22);
    AddEdit(h, st, IDC_OPT_PORT, std::to_wstring(st->value.apiPort), 344, 104, 72, 24);
    AddLabel(h, st, L"Secret", 48, 140, 74, 22);
    AddEdit(h, st, IDC_OPT_SECRET, st->value.apiSecret, 124, 136, 292, 24);
    AddLabel(h, st, L"Timeout", 48, 172, 74, 22);
    AddEdit(h, st, IDC_OPT_TIMEOUT, std::to_wstring(st->value.apiTimeoutMs), 124, 168, 92, 24);
    AddLabel(h, st, L"Refresh", 236, 172, 70, 22);
    AddEdit(h, st, IDC_OPT_REFRESH, std::to_wstring(st->value.refreshIntervalMs), 304, 168, 112, 24);

    AddLabel(h, st, L"Control", 40, 222, 100, 20);
    AddLabel(h, st, L"Proxy host", 48, 252, 88, 22);
    AddEdit(h, st, IDC_OPT_PROXY_HOST, st->value.systemProxyHost, 144, 248, 132, 24);
    AddCheck(h, st, IDC_OPT_AUTO_GROUP, L"Auto detect group", st->value.autoDetectGroup, 300, 249, 160, 24);
    AddLabel(h, st, L"Proxy group", 48, 284, 88, 22);
    AddEdit(h, st, IDC_OPT_GROUP, st->value.proxyGroup, 144, 280, 272, 24);
    AddLabel(h, st, L"Bypass", 48, 316, 88, 22);
    AddEdit(h, st, IDC_OPT_PROXY_BYPASS, st->value.systemProxyBypass, 144, 312, 376, 24);

    AddLabel(h, st, L"Subscription", 40, 370, 120, 20);
    AddCheck(h, st, IDC_OPT_AUTO_PROFILE, L"Auto discover profiles when path is empty",
             st->value.autoDiscoverProfiles, 48, 398, 320, 24);
    AddLabel(h, st, L"Profiles YAML", 48, 430, 100, 22);
    AddEdit(h, st, IDC_OPT_PROFILE_PATH, st->value.profilesPath, 154, 426, 274, 24);
    AddButton(h, st, IDC_OPT_BROWSE, L"Browse...", 438, 425, 82, 26);
    AddLabel(h, st, L"Subscription", 48, 462, 100, 22);
    AddEdit(h, st, IDC_OPT_SUB_NAME, st->value.subscriptionName, 154, 458, 168, 24);
    std::wstring usage = st->usageSummary.empty() ? L"Detected usage: --" : (L"Detected usage: " + st->usageSummary);
    AddLabel(h, st, usage.c_str(), 338, 462, 180, 22);
    AddLabelId(h, st, IDC_OPT_RESOLVED, ResolvedProfilesLabel(st).c_str(), 48, 490, 470, 22);

    AddButton(h, st, IDC_OPT_CANCEL, L"Cancel", 346, 526, 82, 30);
    AddButton(h, st, IDC_OPT_SAVE, L"Save changes", 438, 526, 102, 30);
}

bool BrowseProfilesFile(HWND h)
{
    wchar_t file[MAX_PATH] = { 0 };
    std::wstring current = GetControlText(h, IDC_OPT_PROFILE_PATH);
    if (!current.empty() && current.size() < MAX_PATH)
        wcscpy(file, current.c_str());

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = h;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"YAML files (*.yaml;*.yml)\0*.yaml;*.yml\0All files (*.*)\0*.*\0";
    ofn.lpstrTitle = L"Select Clash Verge profiles.yaml";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return false;

    SetControlText(h, IDC_OPT_PROFILE_PATH, file);
    SetControlText(h, IDC_OPT_RESOLVED, std::wstring(L"Resolved profiles: ") + file);
    return true;
}

bool ReadOptions(HWND h, OptionsState* st)
{
    Settings next = st->value;
    next.apiHost = GetControlText(h, IDC_OPT_HOST);
    next.apiSecret = GetControlText(h, IDC_OPT_SECRET);
    next.proxyGroup = GetControlText(h, IDC_OPT_GROUP);
    next.subscriptionName = GetControlText(h, IDC_OPT_SUB_NAME);
    next.profilesPath = GetControlText(h, IDC_OPT_PROFILE_PATH);
    next.systemProxyHost = GetControlText(h, IDC_OPT_PROXY_HOST);
    next.systemProxyBypass = GetControlText(h, IDC_OPT_PROXY_BYPASS);
    next.autoDetectGroup = SendMessageW(GetDlgItem(h, IDC_OPT_AUTO_GROUP), BM_GETCHECK, 0, 0) == BST_CHECKED;
    next.autoDiscoverProfiles = SendMessageW(GetDlgItem(h, IDC_OPT_AUTO_PROFILE), BM_GETCHECK, 0, 0) == BST_CHECKED;

    next.apiPort = ReadInt(h, IDC_OPT_PORT, next.apiPort);
    next.apiTimeoutMs = static_cast<DWORD>(ReadInt(h, IDC_OPT_TIMEOUT, static_cast<int>(next.apiTimeoutMs)));
    next.refreshIntervalMs = static_cast<DWORD>(ReadInt(h, IDC_OPT_REFRESH, static_cast<int>(next.refreshIntervalMs)));

    if (next.apiHost.empty())
    {
        MessageBoxW(h, L"Host cannot be empty.", L"Clash Monitor Options", MB_OK | MB_ICONWARNING);
        return false;
    }
    if (next.apiPort <= 0 || next.apiPort > 65535)
    {
        MessageBoxW(h, L"Port must be between 1 and 65535.", L"Clash Monitor Options", MB_OK | MB_ICONWARNING);
        return false;
    }
    if (next.apiTimeoutMs < 500 || next.refreshIntervalMs < 500)
    {
        MessageBoxW(h, L"Timeout and refresh interval must be at least 500 ms.", L"Clash Monitor Options", MB_OK | MB_ICONWARNING);
        return false;
    }
    if (next.systemProxyHost.empty())
    {
        MessageBoxW(h, L"System proxy host cannot be empty.", L"Clash Monitor Options", MB_OK | MB_ICONWARNING);
        return false;
    }
    if (next.systemProxyBypass.empty())
    {
        MessageBoxW(h, L"System proxy bypass cannot be empty.", L"Clash Monitor Options", MB_OK | MB_ICONWARNING);
        return false;
    }
    if (!next.profilesPath.empty() && !FileExists(next.profilesPath))
    {
        MessageBoxW(h, L"Profiles YAML path does not exist.", L"Clash Monitor Options", MB_OK | MB_ICONWARNING);
        return false;
    }

    st->value = next;
    return true;
}

void PaintOptions(HWND h, OptionsState* st)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(h, &ps);
    RECT rc;
    GetClientRect(h, &rc);

    HBRUSH bg = CreateSolidBrush(RGB(246, 248, 250));
    FillRect(dc, &rc, bg);
    DeleteObject(bg);

    SetBkMode(dc, TRANSPARENT);
    HFONT oldFont = static_cast<HFONT>(SelectObject(dc, st->titleFont));
    SetTextColor(dc, RGB(31, 35, 40));
    RECT title = { 22, 18, rc.right - 22, 48 };
    DrawTextW(dc, L"Clash Monitor Options", -1, &title, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, oldFont);

    struct Section { RECT r; COLORREF c; };
    Section sections[] = {
        { { 24, 64, 540, 202 }, RGB(9, 105, 218) },
        { { 24, 208, 540, 348 }, RGB(26, 127, 55) },
        { { 24, 356, 540, 516 }, RGB(154, 103, 0) },
    };

    HBRUSH panel = CreateSolidBrush(RGB(255, 255, 255));
    HPEN border = CreatePen(PS_SOLID, 1, RGB(208, 215, 222));
    HBRUSH oldB = static_cast<HBRUSH>(SelectObject(dc, panel));
    HPEN oldP = static_cast<HPEN>(SelectObject(dc, border));

    for (const auto& s : sections)
    {
        RoundRect(dc, s.r.left, s.r.top, s.r.right, s.r.bottom, 8, 8);
        RECT stripe = { s.r.left, s.r.top, s.r.left + 5, s.r.bottom };
        HBRUSH stripeBrush = CreateSolidBrush(s.c);
        FillRect(dc, &stripe, stripeBrush);
        DeleteObject(stripeBrush);
    }

    SelectObject(dc, oldB);
    SelectObject(dc, oldP);
    DeleteObject(panel);
    DeleteObject(border);
    EndPaint(h, &ps);
}

LRESULT CALLBACK OptionsWndProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    OptionsState* st = reinterpret_cast<OptionsState*>(GetWindowLongPtrW(h, GWLP_USERDATA));

    if (m == WM_NCCREATE)
    {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(l);
        st = reinterpret_cast<OptionsState*>(cs->lpCreateParams);
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));
        return TRUE;
    }

    switch (m)
    {
    case WM_CREATE:
    {
        HDC dc = GetDC(h);
        int dpiY = dc ? GetDeviceCaps(dc, LOGPIXELSY) : 96;
        if (dc) ReleaseDC(h, dc);
        st->font = CreateFontW(-MulDiv(9, dpiY, 72), 0, 0, 0, FW_NORMAL, 0, 0, 0,
                               DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        st->titleFont = CreateFontW(-MulDiv(16, dpiY, 72), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0,
                                    DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        st->bgBrush = CreateSolidBrush(RGB(246, 248, 250));
        st->fieldBrush = CreateSolidBrush(RGB(255, 255, 255));
        PopulateOptions(h, st);
        return 0;
    }
    case WM_CTLCOLORSTATIC:
    {
        HDC dc = reinterpret_cast<HDC>(w);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(31, 35, 40));
        return reinterpret_cast<LRESULT>(st->fieldBrush);
    }
    case WM_COMMAND:
        switch (LOWORD(w))
        {
        case IDC_OPT_BROWSE:
            BrowseProfilesFile(h);
            return 0;
        case IDC_OPT_SAVE:
            if (ReadOptions(h, st))
            {
                st->saved = true;
                st->done = true;
                DestroyWindow(h);
            }
            return 0;
        case IDC_OPT_CANCEL:
            st->done = true;
            DestroyWindow(h);
            return 0;
        }
        break;
    case WM_PAINT:
        PaintOptions(h, st);
        return 0;
    case WM_CLOSE:
        st->done = true;
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        if (st)
        {
            if (st->font) DeleteObject(st->font);
            if (st->titleFont) DeleteObject(st->titleFont);
            if (st->bgBrush) DeleteObject(st->bgBrush);
            if (st->fieldBrush) DeleteObject(st->fieldBrush);
            st->font = nullptr;
            st->titleFont = nullptr;
            st->bgBrush = nullptr;
            st->fieldBrush = nullptr;
        }
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

bool ShowOptionsWindow(HWND owner, Settings& settings,
                       const std::wstring& resolvedProfiles,
                       const std::wstring& usageSummary)
{
    const wchar_t* CLS = L"ClashMonitorOptionsWnd";
    HINSTANCE hInst = reinterpret_cast<HINSTANCE>(GetModuleHandleW(nullptr));
    static bool reg = false;
    if (!reg)
    {
        WNDCLASSW wc{};
        wc.lpfnWndProc = OptionsWndProc;
        wc.hInstance = hInst;
        wc.lpszClassName = CLS;
        wc.hCursor = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        RegisterClassW(&wc);
        reg = true;
    }

    OptionsState st;
    st.value = settings;
    st.resolvedProfiles = resolvedProfiles;
    st.usageSummary = usageSummary;

    RECT wa;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int width = 580;
    int height = 604;
    int x = wa.left + ((wa.right - wa.left) - width) / 2;
    int y = wa.top + ((wa.bottom - wa.top) - height) / 2;

    HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST, CLS, L"Clash Monitor Options",
                                WS_CAPTION | WS_SYSMENU | WS_POPUP,
                                x, y, width, height, owner, nullptr, hInst, &st);
    if (!hwnd) return false;

    if (owner) EnableWindow(owner, FALSE);
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG msg;
    while (!st.done && GetMessageW(&msg, nullptr, 0, 0))
    {
        if (!IsDialogMessageW(hwnd, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    if (owner) EnableWindow(owner, TRUE);
    if (owner) SetForegroundWindow(owner);

    if (st.saved)
        settings = st.value;
    return st.saved;
}

} // namespace

// ---------------------------------------------------------------------------
// Singleton
// ---------------------------------------------------------------------------
ClashPlugin& ClashPlugin::Instance()
{
    static ClashPlugin instance;
    return instance;
}

ClashPlugin::ClashPlugin()
{
    m_pluginName        = L"Clash Monitor";
    m_pluginDescription = L"Monitor and control Clash from the taskbar: proxy on/off, latency, current node. Click the item to toggle the system proxy and switch mode/node.";
    m_pluginAuthor      = L"Claude";
    m_pluginCopyright   = L"MIT";
    m_pluginVersion     = L"1.1.0";
    m_pluginUrl         = L"https://github.com";

    m_items[0].Init(ClashField::Node,    this);
    m_items[1].Init(ClashField::Mode,    this);
    m_items[2].Init(ClashField::Latency, this);
    m_items[3].Init(ClashField::Proxy,   this);
    m_items[4].Init(ClashField::Sub,     this);
    m_items[5].Init(ClashField::Usage,   this);

    m_upSpeedStr      = L"0 B/s";
    m_downSpeedStr    = L"0 B/s";
    m_currentNodeName = L"Loading...";
    m_currentMode     = L"unknown";
    m_proxyGroupName  = L"";
}

ClashPlugin::~ClashPlugin()
{
}

// ---------------------------------------------------------------------------
// ITMPlugin mandatory
// ---------------------------------------------------------------------------
IPluginItem* ClashPlugin::GetItem(int index)
{
    if (index >= 0 && index < ITEM_COUNT)
        return &m_items[index];
    return nullptr;
}

void ClashPlugin::DataRequired()
{
    DWORD now = GetTickCount();
    if (now - m_lastFetchTime < m_settings.refreshIntervalMs)
        return;
    m_lastFetchTime = now;

    FetchTraffic();

    // Proxy/config data is heavier to parse; refresh it less often, or
    // immediately when a background latency test just finished.
    if (m_forceProxyRefetch.exchange(false) || now - m_lastProxyFetch > 5000 || m_groups.empty())
    {
        m_lastProxyFetch = now;
        FetchProxies();
        FetchConfig();
        FetchSubscriptions();
    }

    PublishDisplay();
}

void ClashPlugin::PublishDisplay()
{
    // The four items pull live state directly via getters; here we only rebuild
    // the shared tooltip string.
    std::wstring mode, node, group, upStr, downStr, sub, usage;
    int delay;
    bool connected;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        mode      = m_currentMode;
        node      = m_currentNodeName;
        group     = m_proxyGroupName;
        delay     = m_currentDelay;
        connected = m_connected;
        upStr     = m_upSpeedStr;
        downStr   = m_downSpeedStr;
        sub       = m_currentSubscriptionName;
        usage     = m_usagePercentStr;
    }
    bool proxyEnabled = SystemProxy::IsEnabled();

    std::wostringstream tip;
    tip << L"Clash Monitor\r\n";
    if (!connected) tip << L"(API not reachable)\r\n";
    tip << L"Mode: ";
    if (mode == L"rule")        tip << L"Rule";
    else if (mode == L"global") tip << L"Global";
    else if (mode == L"direct") tip << L"Direct";
    else                        tip << mode;
    tip << L"\r\nGroup: " << group;
    tip << L"\r\nNode: "  << node;
    if (!sub.empty()) tip << L"\r\nSub: " << sub;
    tip << L"\r\nUsage: " << usage;
    if (delay > 0)       tip << L"\r\nDelay: " << delay << L" ms";
    else if (delay == 0) tip << L"\r\nDelay: timeout";
    tip << L"\r\n\x2191 " << upStr << L"  \x2193 " << downStr;
    tip << L"\r\nSystem Proxy: " << (proxyEnabled ? L"ON" : L"OFF");
    tip << L"\r\n(Click any item to control)";
    m_tooltip = tip.str();
}

const wchar_t* ClashPlugin::GetInfo(PluginInfoIndex index)
{
    switch (index)
    {
    case TMI_NAME:        return m_pluginName.c_str();
    case TMI_DESCRIPTION: return m_pluginDescription.c_str();
    case TMI_AUTHOR:      return m_pluginAuthor.c_str();
    case TMI_COPYRIGHT:   return m_pluginCopyright.c_str();
    case TMI_VERSION:     return m_pluginVersion.c_str();
    case TMI_URL:         return m_pluginUrl.c_str();
    default:              return L"";
    }
}

// ---------------------------------------------------------------------------
// ITMPlugin optional
// ---------------------------------------------------------------------------
ITMPlugin::OptionReturn ClashPlugin::ShowOptionsDialog(void* hParent)
{
    HWND hWnd = static_cast<HWND>(hParent);

    FetchSubscriptions();

    std::wstring usage;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        usage = m_usagePercentStr;
    }
    std::wstring resolved = ResolveProfilesPath();

    Settings edited = m_settings;
    bool saved = ShowOptionsWindow(hWnd, edited, resolved, usage);
    if (!saved) return OR_OPTION_UNCHANGED;

    m_settings = edited;
    if (!m_configDir.empty())
        m_settings.Save(m_configDir);

    m_http.SetBaseUrl(m_settings.apiHost, m_settings.apiPort);
    m_http.SetSecret(m_settings.apiSecret);
    m_http.SetTimeout(m_settings.apiTimeoutMs);

    if (!m_settings.autoDetectGroup && !m_settings.proxyGroup.empty())
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_proxyGroupName = m_settings.proxyGroup;
    }
    if (!m_settings.subscriptionName.empty())
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_currentSubscriptionName = m_settings.subscriptionName;
    }

    RefreshNow();
    return OR_OPTION_CHANGED;
}

void ClashPlugin::OnMonitorInfo(const MonitorInfo& monitor_info)
{
    // Not used — we fetch from Clash API directly
}

const wchar_t* ClashPlugin::GetTooltipInfo()
{
    return m_tooltip.c_str();
}

void ClashPlugin::OnExtenedInfo(ExtendedInfoIndex index, const wchar_t* data)
{
    if (index == EI_CONFIG_DIR && data)
    {
        m_configDir = data;
        m_settings.Load(m_configDir);
        m_settings.Save(m_configDir);  // ensure defaults written to disk

        m_http.SetBaseUrl(m_settings.apiHost, m_settings.apiPort);
        m_http.SetSecret(m_settings.apiSecret);
        m_http.SetTimeout(m_settings.apiTimeoutMs);

        if (!m_settings.autoDetectGroup && !m_settings.proxyGroup.empty())
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_proxyGroupName = m_settings.proxyGroup;
        }

        if (!m_settings.subscriptionName.empty())
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_currentSubscriptionName = m_settings.subscriptionName;
        }
    }
}

// ---------------------------------------------------------------------------
// Right-click commands — kept static (host caches the count).
//   0 = System Proxy (toggle)   1 = Rule   2 = Global   3 = Direct
// Full node switching lives in the left-click popup (ShowControlMenu).
// ---------------------------------------------------------------------------
int ClashPlugin::GetCommandCount()
{
    return CMD_COUNT;
}

const wchar_t* ClashPlugin::GetCommandName(int command_index)
{
    switch (command_index)
    {
    case CMD_SYSTEM_PROXY: return L"Clash: Toggle System Proxy";
    case CMD_MODE_RULE:    return L"Clash Mode: Rule";
    case CMD_MODE_GLOBAL:  return L"Clash Mode: Global";
    case CMD_MODE_DIRECT:  return L"Clash Mode: Direct";
    default:               return L"";
    }
}

void ClashPlugin::OnPluginCommand(int command_index, void* hWnd, void* para)
{
    switch (command_index)
    {
    case CMD_SYSTEM_PROXY: ToggleSystemProxy(); break;
    case CMD_MODE_RULE:    SwitchMode(L"rule");   RefreshNow(); break;
    case CMD_MODE_GLOBAL:  SwitchMode(L"global"); RefreshNow(); break;
    case CMD_MODE_DIRECT:  SwitchMode(L"direct"); RefreshNow(); break;
    default: break;
    }
}

int ClashPlugin::IsCommandChecked(int command_index)
{
    if (command_index == CMD_SYSTEM_PROXY)
        return SystemProxy::IsEnabled() ? 1 : 0;

    std::lock_guard<std::mutex> lock(m_mutex);
    if (command_index == CMD_MODE_RULE)   return (m_currentMode == L"rule")   ? 1 : 0;
    if (command_index == CMD_MODE_GLOBAL) return (m_currentMode == L"global") ? 1 : 0;
    if (command_index == CMD_MODE_DIRECT) return (m_currentMode == L"direct") ? 1 : 0;
    return 0;
}

// ---------------------------------------------------------------------------
// Data accessors
// ---------------------------------------------------------------------------
std::wstring ClashPlugin::GetCurrentNodeName() const
{ std::lock_guard<std::mutex> lock(m_mutex); return m_currentNodeName; }

std::wstring ClashPlugin::GetCurrentMode() const
{ std::lock_guard<std::mutex> lock(m_mutex); return m_currentMode; }

std::wstring ClashPlugin::GetUpSpeedStr() const
{ std::lock_guard<std::mutex> lock(m_mutex); return m_upSpeedStr; }

std::wstring ClashPlugin::GetDownSpeedStr() const
{ std::lock_guard<std::mutex> lock(m_mutex); return m_downSpeedStr; }

int ClashPlugin::GetCurrentDelay() const
{ std::lock_guard<std::mutex> lock(m_mutex); return m_currentDelay; }

bool ClashPlugin::IsConnected() const
{ std::lock_guard<std::mutex> lock(m_mutex); return m_connected; }

bool ClashPlugin::IsProxyEnabled() const
{ return SystemProxy::IsEnabled(); }

std::wstring ClashPlugin::GetProxyGroupName() const
{ std::lock_guard<std::mutex> lock(m_mutex); return m_proxyGroupName; }

std::wstring ClashPlugin::GetCurrentSubscriptionName() const
{ std::lock_guard<std::mutex> lock(m_mutex); return m_currentSubscriptionName; }

std::wstring ClashPlugin::GetUsagePercentStr() const
{ std::lock_guard<std::mutex> lock(m_mutex); return m_usagePercentStr; }

int ClashPlugin::GetUsagePercent() const
{ std::lock_guard<std::mutex> lock(m_mutex); return m_usagePercent; }

// ---------------------------------------------------------------------------
// Data fetching
// ---------------------------------------------------------------------------
// Speed comes from /connections totals (a clean snapshot) instead of /traffic,
// which is an infinite streaming endpoint that would block every poll.
void ClashPlugin::FetchTraffic()
{
    std::string json = m_http.Get(L"/connections");
    if (json.empty())
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_connected = false;
        return;
    }

    JsonValue root = JsonValue::Parse(json);
    if (!root.IsObject())
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_connected = false;
        return;
    }

    unsigned long long down = static_cast<unsigned long long>(root["downloadTotal"].AsInt());
    unsigned long long up   = static_cast<unsigned long long>(root["uploadTotal"].AsInt());
    DWORD now = GetTickCount();

    std::lock_guard<std::mutex> lock(m_mutex);
    m_connected = true;
    if (m_haveTrafficBaseline)
    {
        DWORD dt = now - m_lastTrafficTick;
        if (dt > 0)
        {
            // Guard against counter reset (clash restart) where totals shrink
            unsigned long long dd = (down >= m_lastDownTotal) ? down - m_lastDownTotal : 0;
            unsigned long long du = (up   >= m_lastUpTotal)   ? up   - m_lastUpTotal   : 0;
            m_downSpeed = dd * 1000ULL / dt;
            m_upSpeed   = du * 1000ULL / dt;
            m_downSpeedStr = FormatSpeed(m_downSpeed);
            m_upSpeedStr   = FormatSpeed(m_upSpeed);
        }
    }
    m_lastDownTotal = down;
    m_lastUpTotal   = up;
    m_lastTrafficTick = now;
    m_haveTrafficBaseline = true;
}

void ClashPlugin::FetchProxies()
{
    std::string json = m_http.Get(L"/proxies");
    if (json.empty()) return;

    JsonValue root = JsonValue::Parse(json);
    if (!root.IsObject()) return;

    const JsonValue& proxies = root["proxies"];
    if (!proxies.IsObject()) return;

    // Pass 1: most-recent measured delay for every proxy entry (nodes and groups)
    std::unordered_map<std::string, int> delayMap;
    for (const auto& kv : proxies.AsObject())
    {
        const JsonValue& e = kv.second;
        if (!e.IsObject()) continue;
        int d = -1;
        const JsonValue& hist = e["history"];
        if (hist.IsArray() && !hist.AsArray().empty())
            d = static_cast<int>(hist[hist.AsArray().size() - 1]["delay"].AsInt());
        delayMap[kv.first] = d;
    }

    // Pass 2: build every selectable group with its node list
    std::vector<ProxyGroup> groups;
    for (const auto& kv : proxies.AsObject())
    {
        const JsonValue& e = kv.second;
        if (!e.IsObject()) continue;
        std::string type = e["type"].AsString();
        if (type != "Selector" && type != "URLTest" && type != "Fallback" && type != "LoadBalance")
            continue;

        const JsonValue& all = e["all"];
        if (!all.IsArray() || all.AsArray().empty()) continue;

        ProxyGroup g;
        g.name = UTF8ToWide(kv.first);
        g.type = UTF8ToWide(type);
        g.now  = UTF8ToWide(e["now"].AsString());

        for (size_t i = 0; i < all.AsArray().size(); i++)
        {
            const JsonValue& nv = all[i];
            if (!nv.IsString()) continue;
            std::string nm = nv.AsString();
            ProxyNode n;
            n.name  = UTF8ToWide(nm);
            auto it = delayMap.find(nm);
            n.delay = (it != delayMap.end()) ? it->second : -1;
            n.displayLabel = n.name + DelaySuffix(n.delay);
            g.nodes.push_back(std::move(n));
        }
        groups.push_back(std::move(g));
    }

    // Deterministic, stable ordering for the menu
    std::sort(groups.begin(), groups.end(),
              [](const ProxyGroup& a, const ProxyGroup& b) { return a.name < b.name; });

    std::wstring preferred = m_settings.autoDetectGroup ? L"" : m_settings.proxyGroup;
    std::wstring primary = PickPrimaryGroup(groups, preferred);

    std::wstring curNode;
    int curDelay = -1;
    for (const auto& g : groups)
    {
        if (g.name == primary)
        {
            curNode = g.now;
            auto it = delayMap.find(WideToUTF8(g.now));
            if (it != delayMap.end()) curDelay = it->second;
            break;
        }
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    m_groups         = std::move(groups);
    m_proxyGroupName = primary;
    if (!curNode.empty()) m_currentNodeName = curNode;
    m_currentDelay   = curDelay;
}

void ClashPlugin::FetchConfig()
{
    std::string json = m_http.Get(L"/configs");
    if (json.empty()) return;

    JsonValue root = JsonValue::Parse(json);
    if (!root.IsObject()) return;

    std::string mode = root["mode"].AsString();

    // Pick the listen port to use for the Windows system proxy (HTTP).
    int mixed = static_cast<int>(root["mixed-port"].AsInt());
    int http  = static_cast<int>(root["port"].AsInt());
    int port  = (mixed > 0) ? mixed : http;

    std::lock_guard<std::mutex> lock(m_mutex);
    if (!mode.empty()) m_currentMode = UTF8ToWide(mode);
    if (port > 0)      m_proxyPort = port;
}

std::wstring ClashPlugin::AutoDiscoverProfilesPath()
{
    std::vector<std::wstring> bases;
    constexpr DWORD ENV_BUF = 32768;
    wchar_t buf[ENV_BUF] = { 0 };

    DWORD n = GetEnvironmentVariableW(L"APPDATA", buf, ENV_BUF);
    if (n > 0 && n < ENV_BUF)
        bases.push_back(buf);

    n = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, ENV_BUF);
    if (n > 0 && n < ENV_BUF)
        bases.push_back(buf);

    for (const auto& base : bases)
    {
        std::wstring direct = base + L"\\profiles.yaml";
        if (FileExists(direct)) return direct;

        WIN32_FIND_DATAW fd{};
        std::wstring pattern = base + L"\\*";
        HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) continue;

        do
        {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
            if (!DirNameLooksLikeClashApp(fd.cFileName)) continue;

            std::wstring candidate = base + L"\\" + fd.cFileName + L"\\profiles.yaml";
            if (FileExists(candidate))
            {
                FindClose(h);
                return candidate;
            }
        } while (FindNextFileW(h, &fd));

        FindClose(h);
    }

    return L"";
}

std::wstring ClashPlugin::ResolveProfilesPath() const
{
    if (!m_settings.profilesPath.empty() && FileExists(m_settings.profilesPath))
        return m_settings.profilesPath;

    if (m_settings.autoDiscoverProfiles)
        return AutoDiscoverProfilesPath();

    return L"";
}

bool ClashPlugin::FetchVergeSubscriptions(std::vector<SubscriptionInfo>& subs,
                                           std::wstring& currentUid)
{
    std::wstring path = ResolveProfilesPath();
    if (path.empty()) return false;

    std::string yaml = ReadFileUtf8(path);
    if (yaml.empty()) return false;

    std::vector<std::string> lines;
    size_t pos = 0;
    while (pos <= yaml.size())
    {
        size_t next = yaml.find('\n', pos);
        if (next == std::string::npos)
        {
            lines.push_back(yaml.substr(pos));
            break;
        }
        lines.push_back(yaml.substr(pos, next - pos));
        pos = next + 1;
    }

    std::string current = "";
    for (const auto& line : lines)
    {
        if (IndentOf(line) == 0)
        {
            std::string v = YamlValue(line, "current");
            if (!v.empty()) { current = v; break; }
        }
    }
    currentUid = UTF8ToWide(current);

    bool inItem = false;
    bool inExtra = false;
    SubscriptionInfo cur;
    std::wstring curUid;

    auto finishItem = [&]()
    {
        if (!inItem) return;
        if (cur.type == L"remote" && (!cur.name.empty() || !curUid.empty()))
        {
            if (cur.name.empty()) cur.name = curUid;
            subs.push_back(cur);
        }
    };

    for (const auto& raw : lines)
    {
        std::string line = raw;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::string t = TrimCopy(line);
        int indent = IndentOf(line);

        if (StartsWith(t, "- uid:"))
        {
            finishItem();
            inItem = true;
            inExtra = false;
            cur = SubscriptionInfo{};
            curUid = UTF8ToWide(YamlValue(t.substr(2), "uid"));
            cur.uid = curUid;
            continue;
        }
        if (!inItem) continue;

        if (indent <= 2 && StartsWith(t, "extra:"))
        {
            inExtra = true;
            continue;
        }
        if (indent <= 2 && !StartsWith(t, "extra:"))
            inExtra = false;

        std::string v;
        if (!(v = YamlValue(line, "type")).empty())
            cur.type = UTF8ToWide(v);
        else if (!(v = YamlValue(line, "name")).empty())
            cur.name = UTF8ToWide(v);
        else if (inExtra && !(v = YamlValue(line, "upload")).empty())
            cur.upload = ParseUInt64(v);
        else if (inExtra && !(v = YamlValue(line, "download")).empty())
            cur.download = ParseUInt64(v);
        else if (inExtra && !(v = YamlValue(line, "total")).empty())
            cur.total = ParseUInt64(v);
        else if (inExtra && !(v = YamlValue(line, "expire")).empty())
            cur.expire = ParseUInt64(v);

        if (cur.total > 0)
            cur.hasUsage = true;
    }
    finishItem();

    return !subs.empty();
}

void ClashPlugin::FetchSubscriptions()
{
    std::vector<SubscriptionInfo> subs;
    std::wstring currentUid;
    bool fromVerge = FetchVergeSubscriptions(subs, currentUid);

    if (!fromVerge)
    {
        std::string json = m_http.Get(L"/providers/proxies");
        if (json.empty()) return;

        JsonValue root = JsonValue::Parse(json);
        if (!root.IsObject()) return;

        const JsonValue& providers = root["providers"];
        if (!providers.IsObject()) return;

        for (const auto& kv : providers.AsObject())
        {
            const JsonValue& e = kv.second;
            if (!e.IsObject()) continue;

            SubscriptionInfo s;
            s.uid = UTF8ToWide(kv.first);
            s.name = UTF8ToWide(kv.first);
            s.type = UTF8ToWide(e["type"].AsString());

            const JsonValue& info = e["subscriptionInfo"];
            if (info.IsObject())
            {
                s.upload   = static_cast<unsigned long long>(info["upload"].AsInt());
                s.download = static_cast<unsigned long long>(info["download"].AsInt());
                s.total    = static_cast<unsigned long long>(info["total"].AsInt());
                s.expire   = static_cast<unsigned long long>(info["expire"].AsInt());
                s.hasUsage = s.total > 0;
            }

            // Providers without subscriptionInfo are still useful for explicit selection.
            subs.push_back(std::move(s));
        }
    }

    std::sort(subs.begin(), subs.end(),
              [](const SubscriptionInfo& a, const SubscriptionInfo& b) { return a.name < b.name; });

    std::wstring selected = m_settings.subscriptionName;
    bool selectedExists = selected.empty();
    if (!selected.empty())
    {
        std::wstring selectedLower = LowerWide(selected);
        for (const auto& s : subs)
        {
            if (LowerWide(s.name) == selectedLower)
            {
                selected = s.name;
                selectedExists = true;
                break;
            }
        }
    }
    if (!selectedExists)
        selected.clear();

    if (selected.empty())
    {
        if (fromVerge && !currentUid.empty())
        {
            for (const auto& s : subs)
                if (s.uid == currentUid) { selected = s.name; break; }
        }
        if (selected.empty())
            for (const auto& s : subs)
                if (s.hasUsage) { selected = s.name; break; }
        if (selected.empty() && !subs.empty())
            selected = subs.front().name;
    }

    std::wstring usageStr = L"--";
    int usagePct = -1;
    for (const auto& s : subs)
    {
        if (s.name != selected) continue;
        if (s.hasUsage)
        {
            unsigned long long used = s.upload + s.download;
            usageStr = FormatUsagePercent(used, s.total);
            usagePct = static_cast<int>((used * 100ULL + s.total / 2ULL) / s.total);
        }
        break;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    m_subscriptions = std::move(subs);
    m_currentSubscriptionName = selected;
    m_usagePercentStr = usageStr;
    m_usagePercent = usagePct;
}

void ClashPlugin::RefreshNow()
{
    FetchTraffic();
    FetchProxies();
    FetchConfig();
    FetchSubscriptions();
    PublishDisplay();
    DWORD now = GetTickCount();
    m_lastFetchTime  = now;
    m_lastProxyFetch = now;
}

// ---------------------------------------------------------------------------
// Primary group selection
// ---------------------------------------------------------------------------
std::wstring ClashPlugin::PickPrimaryGroup(const std::vector<ProxyGroup>& groups,
                                           const std::wstring& preferred)
{
    if (groups.empty()) return L"";

    if (!preferred.empty())
        for (const auto& g : groups)
            if (g.name == preferred) return preferred;

    // Common main-group names (UTF-8 byte literals so source encoding is irrelevant)
    static const char* prefs[] = {
        "\xe8\x8a\x82\xe7\x82\xb9\xe9\x80\x89\xe6\x8b\xa9", // 节点选择
        "\xe6\x89\x8b\xe5\x8a\xa8\xe5\x88\x87\xe6\x8d\xa2", // 手动切换
        "Proxy", "PROXY", "select", "Select", "GLOBAL",
        nullptr
    };
    for (int i = 0; prefs[i]; i++)
        for (const auto& g : groups)
            if (WideToUTF8(g.name) == prefs[i]) return g.name;

    // Fall back to the Selector group with the most nodes
    const ProxyGroup* best = nullptr;
    for (const auto& g : groups)
        if (g.type == L"Selector")
            if (!best || g.nodes.size() > best->nodes.size()) best = &g;
    if (best) return best->name;

    return groups.front().name;
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------
bool ClashPlugin::SwitchProxy(const std::wstring& group, const std::wstring& nodeName)
{
    if (group.empty() || nodeName.empty()) return false;

    std::string jsonBody = "{\"name\":\"" + JsonEscape(WideToUTF8(nodeName)) + "\"}";
    // Group name may be non-ASCII — percent-encode it for the URL path
    std::wstring path = L"/proxies/" + UTF8ToWide(UrlEncode(WideToUTF8(group)));

    bool ok = m_http.Put(path, jsonBody);
    if (ok)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& g : m_groups)
            if (g.name == group) { g.now = nodeName; break; }
        if (group == m_proxyGroupName)
            m_currentNodeName = nodeName;
    }
    return ok;
}

bool ClashPlugin::SwitchMode(const std::wstring& mode)
{
    std::string jsonBody = "{\"mode\":\"" + JsonEscape(WideToUTF8(mode)) + "\"}";
    bool ok = m_http.Patch(L"/configs", jsonBody);
    if (ok)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_currentMode = mode;
    }
    return ok;
}

void ClashPlugin::DebugLog(const std::wstring& msg)
{
    if (m_configDir.empty()) return;
    std::wstring path = m_configDir + L"\\ClashMonitor.log";
    FILE* f = _wfopen(path.c_str(), L"ab");
    if (!f) return;
    SYSTEMTIME st; GetLocalTime(&st);
    char ts[32];
    int n = sprintf(ts, "%02d:%02d:%02d  ", st.wHour, st.wMinute, st.wSecond);
    if (n > 0) fwrite(ts, 1, static_cast<size_t>(n), f);
    std::string line = WideToUTF8(msg) + "\r\n";
    fwrite(line.data(), 1, line.size(), f);
    fclose(f);
}

void ClashPlugin::ToggleSystemProxy()
{
    bool wasOn = SystemProxy::IsEnabled();
    if (wasOn)
    {
        SystemProxy::Disable();
    }
    else
    {
        int port;
        { std::lock_guard<std::mutex> lock(m_mutex); port = m_proxyPort; }
        std::wstring host = m_settings.systemProxyHost;
        std::wstring server = host + L":" + std::to_wstring(port);
        SystemProxy::Enable(server.c_str(), m_settings.systemProxyBypass.c_str());
    }
    DebugLog(std::wstring(L"toggle proxy ") + (wasOn ? L"ON->" : L"OFF->")
             + (SystemProxy::IsEnabled() ? L"ON" : L"OFF"));
    PublishDisplay();
}

void ClashPlugin::StartLatencyRefresh()
{
    // Run only one test at a time
    bool expected = false;
    if (!m_latencyRefreshing.compare_exchange_strong(expected, true))
        return;

    std::wstring node;
    { std::lock_guard<std::mutex> lock(m_mutex); node = m_currentNodeName; }

    // Snapshot connection settings (set once at startup, stable)
    std::wstring host   = m_settings.apiHost;
    int          port   = m_settings.apiPort;
    std::wstring secret = m_settings.apiSecret;

    // Active delay test runs on a background thread (can take several seconds);
    // a dedicated HttpHelper avoids racing the UI thread's shared one.
    std::thread([this, node, host, port, secret]()
    {
        HttpHelper http;
        http.SetBaseUrl(host, port);
        http.SetSecret(secret);
        http.SetTimeout(8000); // delay probe itself uses up to 5s

        std::wstring enc = UTF8ToWide(UrlEncode(WideToUTF8(node)));
        std::wstring path = L"/proxies/" + enc +
                            L"/delay?timeout=5000&url=http%3A%2F%2Fwww.gstatic.com%2Fgenerate_204";
        http.Get(path);

        // Ask the UI thread to refetch /proxies so the new history shows up
        m_forceProxyRefetch.store(true);
        m_latencyRefreshing.store(false);
    }).detach();
}

void ClashPlugin::RefreshUsageNow()
{
    FetchSubscriptions();
    PublishDisplay();
}

// ---------------------------------------------------------------------------
// Left-click control menu — the interactive "button"
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// Node picker — a custom popup window with a scrollable, owner-drawn listbox.
// Standard menus can't be capped to half-screen height; this gives wheel
// scrolling, a scrollbar, and a fixed max height.
// ---------------------------------------------------------------------------
namespace {

COLORREF NodeSeverity(int delay)
{
    if (delay < 0)    return RGB(150, 150, 150);
    if (delay == 0)   return RGB(244, 67, 54);
    if (delay <= 150) return RGB(76, 175, 80);
    if (delay <= 400) return RGB(255, 193, 7);
    return RGB(244, 67, 54);
}

struct PickRow
{
    bool         header = false;
    std::wstring name;        // node name, or header text
    int          delay = -1;
    bool         current = false;
    std::wstring group;       // owning group (nodes only)
    int          groupIndex = -1;
};

struct PickGroup
{
    std::wstring name;
    std::wstring headerText;
    std::vector<PickRow> nodes;
    bool expanded = false;
};

struct PickerState
{
    std::vector<PickRow>* rows = nullptr;
    std::vector<PickGroup>* groups = nullptr;
    HFONT   font = nullptr;
    HWND    list = nullptr;
    int     itemH = 20;
    WNDPROC oldList = nullptr;
    bool    dark = false;
    bool    done = false;
    int     result = -1;      // selected row index, -1 = cancel
    int     wheelRemainder = 0;
};

int RebuildPickerRows(PickerState* st)
{
    if (!st || !st->rows || !st->groups) return -1;

    st->rows->clear();
    int curIndex = -1;
    for (size_t gi = 0; gi < st->groups->size(); gi++)
    {
        PickGroup& g = (*st->groups)[gi];

        PickRow h;
        h.header = true;
        h.name = std::wstring(g.expanded ? L"\x25BE " : L"\x25B8 ") + g.headerText;
        h.group = g.name;
        h.groupIndex = static_cast<int>(gi);
        st->rows->push_back(h);

        if (!g.expanded) continue;
        for (const auto& node : g.nodes)
        {
            PickRow r = node;
            r.groupIndex = static_cast<int>(gi);
            if (r.current && curIndex < 0)
                curIndex = static_cast<int>(st->rows->size());
            st->rows->push_back(r);
        }
    }
    return curIndex;
}

void ReloadPickerList(PickerState* st, int selectedIndex)
{
    if (!st || !st->list || !st->rows) return;

    SendMessageW(st->list, WM_SETREDRAW, FALSE, 0);
    SendMessageW(st->list, LB_RESETCONTENT, 0, 0);
    for (auto& r : *st->rows)
        SendMessageW(st->list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(r.name.c_str()));
    if (selectedIndex >= 0 && selectedIndex < static_cast<int>(st->rows->size()))
        SendMessageW(st->list, LB_SETCURSEL, selectedIndex, 0);
    SendMessageW(st->list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(st->list, nullptr, TRUE);
}

void TogglePickerGroup(HWND h, PickerState* st, int index)
{
    if (!st || !st->rows || !st->groups) return;
    if (index < 0 || index >= static_cast<int>(st->rows->size())) return;

    const PickRow& row = (*st->rows)[index];
    if (!row.header || row.groupIndex < 0 || row.groupIndex >= static_cast<int>(st->groups->size()))
        return;

    int top = static_cast<int>(SendMessageW(h, LB_GETTOPINDEX, 0, 0));
    (*st->groups)[row.groupIndex].expanded = !(*st->groups)[row.groupIndex].expanded;
    RebuildPickerRows(st);
    int nextSel = (index < static_cast<int>(st->rows->size())) ? index : static_cast<int>(st->rows->size()) - 1;
    ReloadPickerList(st, nextSel);

    int count = static_cast<int>(SendMessageW(h, LB_GETCOUNT, 0, 0));
    if (count <= 0) return;
    if (top >= count) top = count - 1;
    SendMessageW(h, LB_SETTOPINDEX, top, 0);
}

void ScrollPickerList(HWND h, PickerState* st, int wheelDelta)
{
    if (!st) return;

    st->wheelRemainder += wheelDelta;
    int steps = st->wheelRemainder / WHEEL_DELTA;
    st->wheelRemainder %= WHEEL_DELTA;
    if (steps == 0) return;

    UINT wheelLines = 3;
    SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &wheelLines, 0);

    RECT rc;
    GetClientRect(h, &rc);
    int visibleRows = (rc.bottom - rc.top) / (st->itemH > 0 ? st->itemH : 1);
    if (visibleRows < 1) visibleRows = 1;

    int count = static_cast<int>(SendMessageW(h, LB_GETCOUNT, 0, 0));
    int maxTop = count - visibleRows;
    if (maxTop < 0) maxTop = 0;

    int linesPerStep = (wheelLines == WHEEL_PAGESCROLL) ? visibleRows : static_cast<int>(wheelLines);
    if (linesPerStep < 1) linesPerStep = 1;

    int top = static_cast<int>(SendMessageW(h, LB_GETTOPINDEX, 0, 0));
    int nextTop = top - (steps * linesPerStep);
    if (nextTop < 0) nextTop = 0;
    if (nextTop > maxTop) nextTop = maxTop;
    SendMessageW(h, LB_SETTOPINDEX, nextTop, 0);
}

LRESULT CALLBACK PickerListProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    PickerState* st = reinterpret_cast<PickerState*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (!st) return DefWindowProcW(h, m, w, l);

    switch (m)
    {
    case WM_LBUTTONUP:
    {
        LRESULT idx = SendMessageW(h, LB_ITEMFROMPOINT, 0, MAKELPARAM(LOWORD(l), HIWORD(l)));
        if (HIWORD(idx) == 0)
        {
            int i = static_cast<int>(LOWORD(idx));
            if (i >= 0 && i < static_cast<int>(st->rows->size()) && (*st->rows)[i].header)
            {
                TogglePickerGroup(h, st, i);
                return 0;
            }
            if (i >= 0 && i < static_cast<int>(st->rows->size()) && !(*st->rows)[i].header)
            {
                st->result = i;
                st->done = true;
            }
        }
        break;
    }
    case WM_KEYDOWN:
        if (w == VK_RETURN)
        {
            int i = static_cast<int>(SendMessageW(h, LB_GETCURSEL, 0, 0));
            if (i >= 0 && i < static_cast<int>(st->rows->size()) && (*st->rows)[i].header)
            {
                TogglePickerGroup(h, st, i);
                return 0;
            }
            if (i >= 0 && i < static_cast<int>(st->rows->size()) && !(*st->rows)[i].header)
            { st->result = i; st->done = true; }
            return 0;
        }
        if (w == VK_LEFT || w == VK_RIGHT || w == VK_SPACE)
        {
            int i = static_cast<int>(SendMessageW(h, LB_GETCURSEL, 0, 0));
            if (i >= 0 && i < static_cast<int>(st->rows->size()) && (*st->rows)[i].header)
            {
                TogglePickerGroup(h, st, i);
                return 0;
            }
        }
        if (w == VK_ESCAPE) { st->result = -1; st->done = true; return 0; }
        break;
    case WM_MOUSEWHEEL:
        ScrollPickerList(h, st, GET_WHEEL_DELTA_WPARAM(w));
        return 0;
    }
    return CallWindowProcW(st->oldList, h, m, w, l);
}

LRESULT CALLBACK PickerWndProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    PickerState* st = reinterpret_cast<PickerState*>(GetWindowLongPtrW(h, GWLP_USERDATA));

    switch (m)
    {
    case WM_MEASUREITEM:
    {
        auto* mi = reinterpret_cast<MEASUREITEMSTRUCT*>(l);
        if (st) mi->itemHeight = st->itemH;
        return TRUE;
    }
    case WM_DRAWITEM:
    {
        auto* di = reinterpret_cast<DRAWITEMSTRUCT*>(l);
        if (!st || di->itemID == static_cast<UINT>(-1)) return TRUE;
        const PickRow& r = (*st->rows)[di->itemID];
        HDC dc = di->hDC;
        RECT rc = di->rcItem;
        bool sel = (di->itemState & ODS_SELECTED) && !r.header;

        COLORREF bg    = st->dark ? RGB(43, 43, 46)   : RGB(250, 250, 250);
        COLORREF selbg = st->dark ? RGB(0, 90, 158)   : RGB(0, 120, 215);
        COLORREF tx    = st->dark ? RGB(235, 235, 235): RGB(25, 25, 25);
        COLORREF hdr   = st->dark ? RGB(140, 170, 210): RGB(90, 110, 150);

        HBRUSH b = CreateSolidBrush(sel ? selbg : bg);
        FillRect(dc, &rc, b);
        DeleteObject(b);
        SetBkMode(dc, TRANSPARENT);
        HFONT of = static_cast<HFONT>(SelectObject(dc, st->font));
        const int padL = 8;

        if (r.header)
        {
            SetTextColor(dc, hdr);
            RECT t = { rc.left + padL, rc.top, rc.right - 6, rc.bottom };
            DrawTextW(dc, r.name.c_str(), -1, &t, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        }
        else
        {
            int cx = rc.left + padL;
            if (r.current)
            {
                SetTextColor(dc, sel ? RGB(255, 255, 255) : RGB(76, 175, 80));
                RECT ck = { cx, rc.top, cx + 16, rc.bottom };
                DrawTextW(dc, L"\x2022", -1, &ck, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            }
            int nameL = cx + 18;

            std::wstring dl = (r.delay > 0) ? std::to_wstring(r.delay) + L" ms"
                              : (r.delay == 0 ? std::wstring(L"timeout") : std::wstring(L"--"));
            SIZE ds = { 0, 0 };
            GetTextExtentPoint32W(dc, dl.c_str(), static_cast<int>(dl.size()), &ds);
            int dlx = rc.right - 8 - ds.cx;

            SetTextColor(dc, sel ? RGB(255, 255, 255) : tx);
            RECT nt = { nameL, rc.top, dlx - 6, rc.bottom };
            DrawTextW(dc, r.name.c_str(), -1, &nt, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

            SetTextColor(dc, sel ? RGB(255, 255, 255) : NodeSeverity(r.delay));
            RECT dt2 = { dlx, rc.top, rc.right - 8, rc.bottom };
            DrawTextW(dc, dl.c_str(), -1, &dt2, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
        SelectObject(dc, of);
        return TRUE;
    }
    case WM_ACTIVATE:
        if (st && LOWORD(w) == WA_INACTIVE) { st->result = -1; st->done = true; }
        return 0;
    case WM_CLOSE:
        if (st) { st->result = -1; st->done = true; }
        return 0;
    case WM_MOUSEWHEEL:
        if (st && st->list)
        {
            SendMessageW(st->list, WM_MOUSEWHEEL, w, l);
            return 0;
        }
        break;
    }
    return DefWindowProcW(h, m, w, l);
}

// Returns selected row index (a node row in rows), or -1 if cancelled.
int ShowNodePicker(HWND owner, std::vector<PickGroup>& groups, std::vector<PickRow>& rows, int curIndex, bool dark)
{
    const wchar_t* CLS = L"ClashNodePickerWnd";
    HINSTANCE hInst = reinterpret_cast<HINSTANCE>(GetModuleHandleW(nullptr));
    static bool reg = false;
    if (!reg)
    {
        WNDCLASSW wc{};
        wc.lpfnWndProc   = PickerWndProc;
        wc.hInstance     = hInst;
        wc.lpszClassName = CLS;
        wc.hCursor       = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
        RegisterClassW(&wc);
        reg = true;
    }

    HDC sdc = GetDC(nullptr);
    int dpiY = GetDeviceCaps(sdc, LOGPIXELSY);
    HFONT font = CreateFontW(-MulDiv(10, dpiY, 72), 0, 0, 0, FW_NORMAL, 0, 0, 0,
                             DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Microsoft YaHei");
    HFONT osf = static_cast<HFONT>(SelectObject(sdc, font));
    int maxName = 0;
    for (auto& g : groups)
    {
        SIZE s = { 0, 0 };
        std::wstring header = L"\x25BE " + g.headerText;
        GetTextExtentPoint32W(sdc, header.c_str(), static_cast<int>(header.size()), &s);
        if (s.cx > maxName) maxName = s.cx;
        for (auto& r : g.nodes)
        {
            SIZE ns = { 0, 0 };
            GetTextExtentPoint32W(sdc, r.name.c_str(), static_cast<int>(r.name.size()), &ns);
            if (ns.cx > maxName) maxName = ns.cx;
        }
    }
    TEXTMETRICW tm; GetTextMetricsW(sdc, &tm);
    int itemH = tm.tmHeight + MulDiv(8, dpiY, 96);
    SelectObject(sdc, osf);
    ReleaseDC(nullptr, sdc);

    RECT wa; SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int screenW = wa.right - wa.left, screenH = wa.bottom - wa.top;
    int sbW = GetSystemMetrics(SM_CXVSCROLL);
    int width = 18 + maxName + 16 + 72 + sbW + 12;
    if (width > screenW / 2) width = screenW / 2;
    if (width < 200) width = 200;

    int contentH = static_cast<int>(rows.size()) * itemH;
    int maxWinH = screenH / 2;
    if (maxWinH < itemH + 2) maxWinH = itemH + 2;
    int winH = contentH + 2;
    if (winH > maxWinH) winH = maxWinH;

    POINT pt; GetCursorPos(&pt);
    int x = pt.x; if (x + width > wa.right) x = wa.right - width; if (x < wa.left) x = wa.left;
    int y; if (pt.y + winH <= wa.bottom) y = pt.y; else y = pt.y - winH; if (y < wa.top) y = wa.top;

    PickerState st;
    st.rows = &rows; st.groups = &groups; st.font = font; st.itemH = itemH; st.dark = dark;

    HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, CLS, L"",
                                WS_POPUP | WS_BORDER, x, y, width, winH, owner, nullptr, hInst, nullptr);
    if (!hwnd) { DeleteObject(font); return -1; }
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&st));

    DWORD lbStyle = WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_OWNERDRAWFIXED |
                    LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | LBS_HASSTRINGS;
    HWND list = CreateWindowExW(0, L"LISTBOX", L"", lbStyle, 0, 0, width - 2, winH - 2,
                                hwnd, reinterpret_cast<HMENU>(1), hInst, nullptr);
    st.list = list;
    SendMessageW(list, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    ReloadPickerList(&st, curIndex);
    st.oldList = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(list, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(PickerListProc)));
    SetWindowLongPtrW(list, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&st));

    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);
    SetFocus(list);

    MSG msg;
    while (!st.done && GetMessageW(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    DestroyWindow(hwnd);
    DeleteObject(font);
    return st.result;
}

struct SubPickRow
{
    std::wstring name;
    std::wstring detail;
    bool current = false;
    bool hasUsage = false;
    int usagePercent = -1;
};

struct SubPickerState
{
    std::vector<SubPickRow>* rows = nullptr;
    HFONT   font = nullptr;
    HWND    list = nullptr;
    int     itemH = 22;
    WNDPROC oldList = nullptr;
    bool    dark = false;
    bool    done = false;
    int     result = -1;
    int     wheelRemainder = 0;
};

COLORREF UsageColor(int pct)
{
    if (pct < 0)  return RGB(150, 150, 150);
    if (pct < 70) return RGB(76, 175, 80);
    if (pct < 90) return RGB(255, 193, 7);
    return RGB(244, 67, 54);
}

void ScrollSubList(HWND h, SubPickerState* st, int wheelDelta)
{
    if (!st) return;

    st->wheelRemainder += wheelDelta;
    int steps = st->wheelRemainder / WHEEL_DELTA;
    st->wheelRemainder %= WHEEL_DELTA;
    if (steps == 0) return;

    UINT wheelLines = 3;
    SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &wheelLines, 0);

    RECT rc;
    GetClientRect(h, &rc);
    int visibleRows = (rc.bottom - rc.top) / (st->itemH > 0 ? st->itemH : 1);
    if (visibleRows < 1) visibleRows = 1;

    int count = static_cast<int>(SendMessageW(h, LB_GETCOUNT, 0, 0));
    int maxTop = count - visibleRows;
    if (maxTop < 0) maxTop = 0;

    int linesPerStep = (wheelLines == WHEEL_PAGESCROLL) ? visibleRows : static_cast<int>(wheelLines);
    if (linesPerStep < 1) linesPerStep = 1;

    int top = static_cast<int>(SendMessageW(h, LB_GETTOPINDEX, 0, 0));
    int nextTop = top - (steps * linesPerStep);
    if (nextTop < 0) nextTop = 0;
    if (nextTop > maxTop) nextTop = maxTop;
    SendMessageW(h, LB_SETTOPINDEX, nextTop, 0);
}

LRESULT CALLBACK SubListProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    SubPickerState* st = reinterpret_cast<SubPickerState*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (!st) return DefWindowProcW(h, m, w, l);

    switch (m)
    {
    case WM_LBUTTONUP:
    {
        LRESULT idx = SendMessageW(h, LB_ITEMFROMPOINT, 0, MAKELPARAM(LOWORD(l), HIWORD(l)));
        if (HIWORD(idx) == 0)
        {
            int i = static_cast<int>(LOWORD(idx));
            if (i >= 0 && i < static_cast<int>(st->rows->size()))
            {
                st->result = i;
                st->done = true;
                return 0;
            }
        }
        break;
    }
    case WM_KEYDOWN:
        if (w == VK_RETURN)
        {
            int i = static_cast<int>(SendMessageW(h, LB_GETCURSEL, 0, 0));
            if (i >= 0 && i < static_cast<int>(st->rows->size()))
            { st->result = i; st->done = true; }
            return 0;
        }
        if (w == VK_ESCAPE) { st->result = -1; st->done = true; return 0; }
        break;
    case WM_MOUSEWHEEL:
        ScrollSubList(h, st, GET_WHEEL_DELTA_WPARAM(w));
        return 0;
    }
    return CallWindowProcW(st->oldList, h, m, w, l);
}

LRESULT CALLBACK SubPickerWndProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    SubPickerState* st = reinterpret_cast<SubPickerState*>(GetWindowLongPtrW(h, GWLP_USERDATA));

    switch (m)
    {
    case WM_MEASUREITEM:
    {
        auto* mi = reinterpret_cast<MEASUREITEMSTRUCT*>(l);
        if (st) mi->itemHeight = st->itemH;
        return TRUE;
    }
    case WM_DRAWITEM:
    {
        auto* di = reinterpret_cast<DRAWITEMSTRUCT*>(l);
        if (!st || di->itemID == static_cast<UINT>(-1)) return TRUE;
        const SubPickRow& r = (*st->rows)[di->itemID];
        HDC dc = di->hDC;
        RECT rc = di->rcItem;
        bool sel = (di->itemState & ODS_SELECTED) != 0;

        COLORREF bg    = st->dark ? RGB(43, 43, 46)   : RGB(250, 250, 250);
        COLORREF selbg = st->dark ? RGB(0, 90, 158)   : RGB(0, 120, 215);
        COLORREF tx    = st->dark ? RGB(235, 235, 235): RGB(25, 25, 25);

        HBRUSH b = CreateSolidBrush(sel ? selbg : bg);
        FillRect(dc, &rc, b);
        DeleteObject(b);
        SetBkMode(dc, TRANSPARENT);
        HFONT of = static_cast<HFONT>(SelectObject(dc, st->font));

        int cx = rc.left + 8;
        if (r.current)
        {
            SetTextColor(dc, sel ? RGB(255, 255, 255) : RGB(76, 175, 80));
            RECT ck = { cx, rc.top, cx + 16, rc.bottom };
            DrawTextW(dc, L"\x2022", -1, &ck, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }
        int nameL = cx + 18;

        SIZE ds = { 0, 0 };
        GetTextExtentPoint32W(dc, r.detail.c_str(), static_cast<int>(r.detail.size()), &ds);
        int dlx = rc.right - 8 - ds.cx;

        SetTextColor(dc, sel ? RGB(255, 255, 255) : tx);
        RECT nt = { nameL, rc.top, dlx - 8, rc.bottom };
        DrawTextW(dc, r.name.c_str(), -1, &nt, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

        SetTextColor(dc, sel ? RGB(255, 255, 255) : UsageColor(r.usagePercent));
        RECT dt = { dlx, rc.top, rc.right - 8, rc.bottom };
        DrawTextW(dc, r.detail.c_str(), -1, &dt, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        SelectObject(dc, of);
        return TRUE;
    }
    case WM_ACTIVATE:
        if (st && LOWORD(w) == WA_INACTIVE) { st->result = -1; st->done = true; }
        return 0;
    case WM_CLOSE:
        if (st) { st->result = -1; st->done = true; }
        return 0;
    case WM_MOUSEWHEEL:
        if (st && st->list)
        {
            SendMessageW(st->list, WM_MOUSEWHEEL, w, l);
            return 0;
        }
        break;
    }
    return DefWindowProcW(h, m, w, l);
}

int ShowSubPicker(HWND owner, std::vector<SubPickRow>& rows, int curIndex, bool dark)
{
    const wchar_t* CLS = L"ClashSubPickerWnd";
    HINSTANCE hInst = reinterpret_cast<HINSTANCE>(GetModuleHandleW(nullptr));
    static bool reg = false;
    if (!reg)
    {
        WNDCLASSW wc{};
        wc.lpfnWndProc   = SubPickerWndProc;
        wc.hInstance     = hInst;
        wc.lpszClassName = CLS;
        wc.hCursor       = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
        RegisterClassW(&wc);
        reg = true;
    }

    HDC sdc = GetDC(nullptr);
    int dpiY = GetDeviceCaps(sdc, LOGPIXELSY);
    HFONT font = CreateFontW(-MulDiv(10, dpiY, 72), 0, 0, 0, FW_NORMAL, 0, 0, 0,
                             DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Microsoft YaHei");
    HFONT osf = static_cast<HFONT>(SelectObject(sdc, font));
    int maxName = 0;
    for (auto& r : rows)
    {
        SIZE s = { 0, 0 };
        GetTextExtentPoint32W(sdc, r.name.c_str(), static_cast<int>(r.name.size()), &s);
        if (s.cx > maxName) maxName = s.cx;
    }
    TEXTMETRICW tm; GetTextMetricsW(sdc, &tm);
    int itemH = tm.tmHeight + MulDiv(8, dpiY, 96);
    SelectObject(sdc, osf);
    ReleaseDC(nullptr, sdc);

    RECT wa; SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int screenW = wa.right - wa.left, screenH = wa.bottom - wa.top;
    int sbW = GetSystemMetrics(SM_CXVSCROLL);
    int width = 18 + maxName + 104 + sbW + 20;
    if (width > screenW / 2) width = screenW / 2;
    if (width < 220) width = 220;

    int contentH = static_cast<int>(rows.size()) * itemH;
    int maxWinH = screenH / 2;
    if (maxWinH < itemH + 2) maxWinH = itemH + 2;
    int winH = contentH + 2;
    if (winH > maxWinH) winH = maxWinH;

    POINT pt; GetCursorPos(&pt);
    int x = pt.x; if (x + width > wa.right) x = wa.right - width; if (x < wa.left) x = wa.left;
    int y; if (pt.y + winH <= wa.bottom) y = pt.y; else y = pt.y - winH; if (y < wa.top) y = wa.top;

    SubPickerState st;
    st.rows = &rows; st.font = font; st.itemH = itemH; st.dark = dark;

    HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, CLS, L"",
                                WS_POPUP | WS_BORDER, x, y, width, winH, owner, nullptr, hInst, nullptr);
    if (!hwnd) { DeleteObject(font); return -1; }
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&st));

    DWORD lbStyle = WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_OWNERDRAWFIXED |
                    LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | LBS_HASSTRINGS;
    HWND list = CreateWindowExW(0, L"LISTBOX", L"", lbStyle, 0, 0, width - 2, winH - 2,
                                hwnd, reinterpret_cast<HMENU>(1), hInst, nullptr);
    st.list = list;
    SendMessageW(list, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    for (auto& r : rows)
        SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(r.name.c_str()));
    if (curIndex >= 0 && curIndex < static_cast<int>(rows.size()))
        SendMessageW(list, LB_SETCURSEL, curIndex, 0);
    st.oldList = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(list, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(SubListProc)));
    SetWindowLongPtrW(list, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&st));

    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);
    SetFocus(list);

    MSG msg;
    while (!st.done && GetMessageW(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    DestroyWindow(hwnd);
    DeleteObject(font);
    return st.result;
}

} // namespace

void ClashPlugin::ShowModeMenu(void* hWndV)
{
    HWND hWnd = static_cast<HWND>(hWndV);
    std::wstring mode;
    { std::lock_guard<std::mutex> lock(m_mutex); mode = m_currentMode; }

    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | (mode == L"rule"   ? MF_CHECKED : 0), ID_MODE_RULE,   L"Rule");
    AppendMenuW(menu, MF_STRING | (mode == L"global" ? MF_CHECKED : 0), ID_MODE_GLOBAL, L"Global");
    AppendMenuW(menu, MF_STRING | (mode == L"direct" ? MF_CHECKED : 0), ID_MODE_DIRECT, L"Direct");

    POINT pt; GetCursorPos(&pt);
    SetForegroundWindow(hWnd);
    int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTBUTTON | TPM_RIGHTBUTTON,
                             pt.x, pt.y, 0, hWnd, nullptr);
    PostMessageW(hWnd, WM_NULL, 0, 0);
    DestroyMenu(menu);

    DebugLog(L"mode menu cmd=" + std::to_wstring(cmd));
    if (cmd == ID_MODE_RULE)        { SwitchMode(L"rule");   RefreshNow(); }
    else if (cmd == ID_MODE_GLOBAL) { SwitchMode(L"global"); RefreshNow(); }
    else if (cmd == ID_MODE_DIRECT) { SwitchMode(L"direct"); RefreshNow(); }
}

void ClashPlugin::ShowSubscriptionMenu(void* hWndV)
{
    HWND hWnd = static_cast<HWND>(hWndV);

    FetchSubscriptions();

    std::vector<SubscriptionInfo> subs;
    std::wstring current;
    bool dark;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        subs = m_subscriptions;
        current = m_currentSubscriptionName;
    }
    dark = m_darkHint;
    if (subs.empty()) return;

    std::vector<SubPickRow> rows;
    int curIndex = -1;
    for (size_t i = 0; i < subs.size(); i++)
    {
        const auto& s = subs[i];
        SubPickRow r;
        r.name = s.name;
        r.current = (s.name == current);
        r.hasUsage = s.hasUsage;
        if (s.hasUsage)
        {
            unsigned long long used = s.upload + s.download;
            r.detail = FormatUsagePercent(used, s.total);
            r.usagePercent = static_cast<int>((used * 100ULL + s.total / 2ULL) / s.total);
        }
        else
        {
            r.detail = L"--";
        }
        if (r.current) curIndex = static_cast<int>(i);
        rows.push_back(std::move(r));
    }

    int sel = ShowSubPicker(hWnd, rows, curIndex, dark);
    if (sel < 0 || sel >= static_cast<int>(subs.size())) return;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_currentSubscriptionName = subs[sel].name;
    }
    m_settings.subscriptionName = subs[sel].name;
    if (!m_configDir.empty()) m_settings.Save(m_configDir);

    DebugLog(L"select subscription '" + subs[sel].name + L"'");
    FetchSubscriptions();
    PublishDisplay();
}

void ClashPlugin::ShowNodeMenu(void* hWndV)
{
    HWND hWnd = static_cast<HWND>(hWndV);

    std::vector<ProxyGroup> groups;
    std::wstring primary;
    bool connected, dark;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        groups    = m_groups;
        primary   = m_proxyGroupName;
        connected = m_connected;
    }
    dark = m_darkHint;
    if (!connected || groups.empty()) return;

    // Primary group is open by default; secondary groups start collapsed and
    // can be expanded from their header row.
    std::vector<PickGroup> pickerGroups;
    std::vector<PickRow> rows;
    int curIndex = -1;

    auto addGroup = [&](const ProxyGroup& g)
    {
        PickGroup pg;
        pg.name = g.name;
        pg.headerText = g.name + (g.now.empty() ? L"" : (L"  \x2192 " + g.now));
        pg.expanded = (g.name == primary);
        for (const auto& n : g.nodes)
        {
            PickRow r;
            r.name    = n.name;
            r.delay   = n.delay;
            r.group   = g.name;
            r.current = (n.name == g.now);
            pg.nodes.push_back(r);
        }
        pickerGroups.push_back(std::move(pg));
    };

    for (const auto& g : groups) if (g.name == primary) { addGroup(g); break; }
    for (const auto& g : groups) if (g.name != primary) addGroup(g);

    PickerState initState;
    initState.rows = &rows;
    initState.groups = &pickerGroups;
    curIndex = RebuildPickerRows(&initState);

    int sel = ShowNodePicker(hWnd, pickerGroups, rows, curIndex, dark);
    if (sel < 0 || sel >= static_cast<int>(rows.size()) || rows[sel].header) return;

    bool ok = SwitchProxy(rows[sel].group, rows[sel].name);
    DebugLog(L"switch node '" + rows[sel].name + L"' ok=" + std::to_wstring(ok));
    RefreshNow();
}
