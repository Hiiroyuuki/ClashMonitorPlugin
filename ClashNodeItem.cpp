#include "pch.h"
#include "ClashNodeItem.h"
#include "ClashPlugin.h"
#include <string>

ClashInfoItem::ClashInfoItem()
    : m_field(ClashField::Node)
    , m_plugin(nullptr)
{
}

void ClashInfoItem::Init(ClashField field, ClashPlugin* plugin)
{
    m_field  = field;
    m_plugin = plugin;

    switch (field)
    {
    case ClashField::Node:
        m_name = L"Clash Node";    m_id = L"ClashNode";    m_label = L"Node:";    m_sample = L"Hong Kong IEPL Premium 01"; break;
    case ClashField::Mode:
        m_name = L"Clash Mode";    m_id = L"ClashMode";    m_label = L"Mode:";    m_sample = L"Global"; break;
    case ClashField::Latency:
        m_name = L"Clash Latency"; m_id = L"ClashLatency"; m_label = L"Latency:"; m_sample = L"8888 ms"; break;
    case ClashField::Proxy:
        m_name = L"Clash Proxy";   m_id = L"ClashProxy";   m_label = L"Proxy:";   m_sample = L"OFF"; break;
    case ClashField::Sub:
        m_name = L"Clash Sub";     m_id = L"ClashSub";     m_label = L"Sub:";     m_sample = L"My Subscription"; break;
    case ClashField::Usage:
        m_name = L"Clash Usage";   m_id = L"ClashUsage";   m_label = L"Usage:";   m_sample = L"100%"; break;
    }
}

const wchar_t* ClashInfoItem::GetItemName() const  { return m_name.c_str(); }
const wchar_t* ClashInfoItem::GetItemId() const    { return m_id.c_str(); }
const wchar_t* ClashInfoItem::GetItemLableText() const { return m_label.c_str(); }
const wchar_t* ClashInfoItem::GetItemValueSampleText() const { return m_sample.c_str(); }

std::wstring ClashInfoItem::CurrentValue() const
{
    if (!m_plugin) return L"--";

    bool connected = m_plugin->IsConnected();

    switch (m_field)
    {
    case ClashField::Node:
    {
        std::wstring v = connected ? m_plugin->GetCurrentNodeName() : std::wstring(L"Disconnected");
        return v.empty() ? std::wstring(L"--") : v;
    }
    case ClashField::Mode:
    {
        std::wstring mode = m_plugin->GetCurrentMode();
        if (mode == L"rule")        return L"Rule";
        if (mode == L"global")      return L"Global";
        if (mode == L"direct")      return L"Direct";
        return connected ? mode : std::wstring(L"--");
    }
    case ClashField::Latency:
    {
        if (m_plugin->IsLatencyRefreshing()) return L"--";
        int d = m_plugin->GetCurrentDelay();
        if (!connected)  return L"--";
        if (d > 0)       return std::to_wstring(d) + L" ms";
        if (d == 0)      return L"timeout";
        return L"--";
    }
    case ClashField::Proxy:
        return m_plugin->IsProxyEnabled() ? L"ON" : L"OFF";
    case ClashField::Sub:
    {
        std::wstring v = m_plugin->GetCurrentSubscriptionName();
        return v.empty() ? std::wstring(L"--") : v;
    }
    case ClashField::Usage:
        return m_plugin->GetUsagePercentStr();
    }
    return L"--";
}

const wchar_t* ClashInfoItem::GetItemValueText() const
{
    m_value = CurrentValue();
    return m_value.c_str();
}

int ClashInfoItem::GetItemWidth() const
{
    // Fallback when no DC is available to measure with
    return 80;
}

// Colour for latency severity (also used for the latency dot + number)
static COLORREF SeverityColor(bool connected, int delay)
{
    if (!connected || delay < 0) return RGB(150, 150, 150); // unknown = gray
    if (delay == 0)              return RGB(244, 67, 54);   // timeout = red
    if (delay <= 150)            return RGB(76, 175, 80);   // fast = green
    if (delay <= 400)            return RGB(255, 193, 7);   // medium = amber
    return RGB(244, 67, 54);                                // slow = red
}

int ClashInfoItem::GetItemWidthEx(void* hDC) const
{
    HDC hdc = static_cast<HDC>(hDC);
    if (!hdc) return GetItemWidth();

    int extra = 16; // left + right padding for the background chip
    if (m_field == ClashField::Latency || m_field == ClashField::Usage) extra += 13; // status dot + gap

    std::wstring text = m_label + L" " + CurrentValue();
    SIZE sz = { 0, 0 };
    GetTextExtentPoint32W(hdc, text.c_str(), static_cast<int>(text.size()), &sz);

    if (m_field == ClashField::Latency)
    {
        std::wstring stableText = m_label + L" " + m_sample;
        SIZE stableSz = { 0, 0 };
        GetTextExtentPoint32W(hdc, stableText.c_str(), static_cast<int>(stableText.size()), &stableSz);
        if (stableSz.cx > sz.cx) sz.cx = stableSz.cx;
    }

    return sz.cx + extra;
}

void ClashInfoItem::DrawItem(void* hDC, int x, int y, int w, int h, bool dark_mode)
{
    HDC hdc = static_cast<HDC>(hDC);
    if (!hdc) return;

    if (m_plugin) m_plugin->SetDarkHint(dark_mode); // so popups can match theme

    int saved = SaveDC(hdc);
    SetBkMode(hdc, TRANSPARENT);

    // ---- rounded background chip ----
    COLORREF bg = dark_mode ? RGB(52, 52, 56) : RGB(231, 231, 234);
    RECT chip = { x + 1, y + 1, x + w - 1, y + h - 1 };
    bool hovered = false;
    HWND drawWnd = WindowFromDC(hdc);
    if (drawWnd)
    {
        POINT pt;
        if (GetCursorPos(&pt) && ScreenToClient(drawWnd, &pt))
            hovered = PtInRect(&chip, pt) != FALSE;
    }
    COLORREF border = hovered ? (dark_mode ? RGB(92, 170, 255) : RGB(0, 120, 215)) : bg;
    HBRUSH bgBrush = CreateSolidBrush(bg);
    HPEN   bgPen   = CreatePen(PS_SOLID, 1, border);
    HBRUSH oldB = static_cast<HBRUSH>(SelectObject(hdc, bgBrush));
    HPEN   oldP = static_cast<HPEN>(SelectObject(hdc, bgPen));
    int rad = (h >= 24) ? 8 : 6;
    RoundRect(hdc, chip.left, chip.top, chip.right, chip.bottom, rad, rad);
    SelectObject(hdc, oldB);
    SelectObject(hdc, oldP);
    DeleteObject(bgBrush);
    DeleteObject(bgPen);

    COLORREF textColor  = dark_mode ? RGB(238, 238, 238) : RGB(28, 28, 28);
    COLORREF labelColor = dark_mode ? RGB(170, 170, 170) : RGB(110, 110, 110);

    const int padX = 7;
    int curX  = x + padX;
    int right = x + w - padX;

    // ---- label (dim) ----
    std::wstring label = m_label + L" ";
    SIZE lsz = { 0, 0 };
    GetTextExtentPoint32W(hdc, label.c_str(), static_cast<int>(label.size()), &lsz);
    RECT rLabel = { curX, y, curX + lsz.cx, y + h };
    SetTextColor(hdc, labelColor);
    DrawTextW(hdc, label.c_str(), -1, &rLabel, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    curX += lsz.cx;

    // ---- value (field-specific colour) ----
    std::wstring value = CurrentValue();

    if (m_field == ClashField::Latency)
    {
        bool connected  = m_plugin && m_plugin->IsConnected();
        int  delay      = m_plugin ? m_plugin->GetCurrentDelay() : -1;
        bool refreshing = m_plugin && m_plugin->IsLatencyRefreshing();
        COLORREF sev    = refreshing ? RGB(150, 150, 150) : SeverityColor(connected, delay);

        int dot = 7;
        int dy  = y + (h - dot) / 2;
        HBRUSH db = CreateSolidBrush(sev);
        HPEN   dp = CreatePen(PS_SOLID, 1, sev);
        HBRUSH ob = static_cast<HBRUSH>(SelectObject(hdc, db));
        HPEN   op = static_cast<HPEN>(SelectObject(hdc, dp));
        Ellipse(hdc, curX, dy, curX + dot, dy + dot);
        SelectObject(hdc, ob);
        SelectObject(hdc, op);
        DeleteObject(db);
        DeleteObject(dp);
        curX += dot + 4;

        RECT rv = { curX, y, right, y + h };
        SetTextColor(hdc, sev);
        DrawTextW(hdc, value.c_str(), -1, &rv, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    }
    else if (m_field == ClashField::Proxy)
    {
        bool on = (value == L"ON");
        COLORREF c = on ? RGB(76, 175, 80) : RGB(244, 67, 54);
        RECT rv = { curX, y, right, y + h };
        SetTextColor(hdc, c);
        DrawTextW(hdc, value.c_str(), -1, &rv, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    }
    else if (m_field == ClashField::Usage)
    {
        int pct = m_plugin ? m_plugin->GetUsagePercent() : -1;
        COLORREF c = RGB(150, 150, 150);
        if (pct >= 0 && pct < 70) c = RGB(76, 175, 80);
        else if (pct >= 70 && pct < 90) c = RGB(255, 193, 7);
        else if (pct >= 90) c = RGB(244, 67, 54);

        int dot = 7;
        int dy  = y + (h - dot) / 2;
        HBRUSH db = CreateSolidBrush(c);
        HPEN   dp = CreatePen(PS_SOLID, 1, c);
        HBRUSH ob = static_cast<HBRUSH>(SelectObject(hdc, db));
        HPEN   op = static_cast<HPEN>(SelectObject(hdc, dp));
        Ellipse(hdc, curX, dy, curX + dot, dy + dot);
        SelectObject(hdc, ob);
        SelectObject(hdc, op);
        DeleteObject(db);
        DeleteObject(dp);
        curX += dot + 4;

        RECT rv = { curX, y, right, y + h };
        SetTextColor(hdc, c);
        DrawTextW(hdc, value.c_str(), -1, &rv, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    }
    else
    {
        RECT rv = { curX, y, right, y + h };
        SetTextColor(hdc, textColor);
        DrawTextW(hdc, value.c_str(), -1, &rv, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    }

    RestoreDC(hdc, saved);
}

int ClashInfoItem::OnMouseEvent(MouseEventType type, int x, int y, void* hWnd, int flag)
{
    if (!m_plugin) return 0;

    // Each item is its own clickable control.
    if (type == MT_LCLICKED)
    {
        switch (m_field)
        {
        case ClashField::Node:    m_plugin->ShowNodeMenu(hWnd);       break;
        case ClashField::Mode:    m_plugin->ShowModeMenu(hWnd);       break;
        case ClashField::Proxy:   m_plugin->ToggleSystemProxy();      break;
        case ClashField::Latency: m_plugin->StartLatencyRefresh();    break;
        case ClashField::Sub:     m_plugin->ShowSubscriptionMenu(hWnd); break;
        case ClashField::Usage:   m_plugin->RefreshUsageNow();        break;
        }
        return 1;
    }
    return 0;
}
