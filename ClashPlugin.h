#pragma once
#include "PluginInterface.h"
#include "ClashNodeItem.h"
#include "HttpHelper.h"
#include "Settings.h"
#include <string>
#include <vector>
#include <mutex>
#include <atomic>

class ClashPlugin : public ITMPlugin
{
public:
    static ClashPlugin& Instance();

    // ITMPlugin mandatory
    IPluginItem* GetItem(int index) override;
    void DataRequired() override;
    const wchar_t* GetInfo(PluginInfoIndex index) override;

    // ITMPlugin optional
    OptionReturn ShowOptionsDialog(void* hParent) override;
    void OnMonitorInfo(const MonitorInfo& monitor_info) override;
    const wchar_t* GetTooltipInfo() override;
    void OnExtenedInfo(ExtendedInfoIndex index, const wchar_t* data) override;

    // Plugin commands (right-click menu)
    int GetCommandCount() override;
    const wchar_t* GetCommandName(int command_index) override;
    void OnPluginCommand(int command_index, void* hWnd, void* para) override;
    int IsCommandChecked(int command_index) override;

    // Access to current data (called by ClashNodeItem for display + menu)
    struct ProxyNode
    {
        std::wstring name;
        int delay = -1;              // ms, -1 = unknown, 0 = clash-measured timeout
        std::wstring displayLabel;   // pre-computed label for menu display
    };

    struct ProxyGroup
    {
        std::wstring name;
        std::wstring type;           // Selector / URLTest / ...
        std::wstring now;            // currently selected entry in this group
        std::vector<ProxyNode> nodes;
    };

    std::wstring GetCurrentNodeName() const;
    std::wstring GetCurrentMode() const;
    std::wstring GetUpSpeedStr() const;
    std::wstring GetDownSpeedStr() const;
    int GetCurrentDelay() const;
    bool IsConnected() const;
    bool IsProxyEnabled() const;
    std::wstring GetProxyGroupName() const;

    // Per-item click actions
    void ShowNodeMenu(void* hWnd);   // Node item: dropdown to pick a node
    void ShowModeMenu(void* hWnd);   // Mode item: dropdown to pick rule/global/direct
    void ToggleSystemProxy();        // Proxy item: flip ON<->OFF directly
    void StartLatencyRefresh();      // Latency item: active delay test (background)
    bool IsLatencyRefreshing() const { return m_latencyRefreshing.load(); }

    // Items report the host's dark/light state during draw so popups can match
    void SetDarkHint(bool dark) { m_darkHint = dark; }

private:
    ClashPlugin();
    ~ClashPlugin();
    ClashPlugin(const ClashPlugin&) = delete;
    ClashPlugin& operator=(const ClashPlugin&) = delete;

    // Data fetching
    void FetchTraffic();
    void FetchProxies();
    void FetchConfig();
    void RefreshNow();      // force an immediate full refresh
    void PublishDisplay();  // push cached data to the taskbar item + tooltip
    void DebugLog(const std::wstring& msg); // append a line to ClashMonitor.log
    bool SwitchProxy(const std::wstring& group, const std::wstring& nodeName);
    bool SwitchMode(const std::wstring& mode);

    // Pick the primary group to surface in the taskbar from all fetched groups
    static std::wstring PickPrimaryGroup(const std::vector<ProxyGroup>& groups,
                                         const std::wstring& preferred);

    // Members — four standalone display items (node / mode / latency / proxy)
    ClashInfoItem m_items[4];
    HttpHelper m_http;
    Settings m_settings;
    ITrafficMonitor* m_pApp = nullptr;

    // Cached data (protected by mutex)
    mutable std::mutex m_mutex;
    std::wstring m_currentNodeName;
    std::wstring m_currentMode;      // "rule" / "global" / "direct"
    std::wstring m_proxyGroupName;   // primary group surfaced in taskbar
    unsigned long long m_upSpeed = 0;
    unsigned long long m_downSpeed = 0;
    int m_currentDelay = -1;          // ms
    bool m_connected = false;
    std::vector<ProxyGroup> m_groups; // all selectable groups
    int m_proxyPort = 7890;           // clash mixed/http listen port for system proxy

    // Latency active-refresh state (set by background thread)
    std::atomic<bool> m_latencyRefreshing{false};
    std::atomic<bool> m_forceProxyRefetch{false};

    bool m_darkHint = false; // last dark/light state seen during draw

    // Traffic snapshot baseline (speed = delta of /connections totals)
    unsigned long long m_lastDownTotal = 0;
    unsigned long long m_lastUpTotal = 0;
    DWORD m_lastTrafficTick = 0;
    bool m_haveTrafficBaseline = false;

    // Display strings
    std::wstring m_upSpeedStr;
    std::wstring m_downSpeedStr;
    std::wstring m_tooltip;
    std::wstring m_configDir;

    DWORD m_lastFetchTime = 0;
    DWORD m_lastProxyFetch = 0;

    // Info strings
    std::wstring m_pluginName;
    std::wstring m_pluginDescription;
    std::wstring m_pluginAuthor;
    std::wstring m_pluginCopyright;
    std::wstring m_pluginVersion;
    std::wstring m_pluginUrl;

    // Static right-click command layout (host caches the count, so keep it fixed;
    // dynamic node switching lives in the left-click popup instead)
    static constexpr int CMD_SYSTEM_PROXY = 0;
    static constexpr int CMD_MODE_RULE    = 1;
    static constexpr int CMD_MODE_GLOBAL  = 2;
    static constexpr int CMD_MODE_DIRECT  = 3;
    static constexpr int CMD_COUNT        = 4;
};
