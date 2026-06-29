#pragma once
#include "PluginInterface.h"
#include <string>

// Forward declare to access ClashPlugin data
class ClashPlugin;

// Which piece of Clash state this item displays
enum class ClashField
{
    Node,     // current node name
    Mode,     // proxy mode (rule/global/direct)
    Latency,  // current node delay
    Proxy     // system proxy on/off
};

// A single standalone TrafficMonitor display item. Four of these are exposed,
// so each value (node/mode/latency/proxy) is its own component the user can
// place independently.
//
// Custom-draw: the plugin owns the whole rectangle. Width is measured from the
// actual "label value" text (no fixed sample → no trailing blank padding) and
// drawing reuses the host's currently selected font (matches the user's
// configured font/size and theme colour).
class ClashInfoItem : public IPluginItem
{
public:
    ClashInfoItem();

    void Init(ClashField field, ClashPlugin* plugin);

    // IPluginItem mandatory
    const wchar_t* GetItemName() const override;
    const wchar_t* GetItemId() const override;
    const wchar_t* GetItemLableText() const override;
    const wchar_t* GetItemValueText() const override;
    const wchar_t* GetItemValueSampleText() const override;

    // Custom draw — size to content, draw with host font
    bool IsCustomDraw() const override { return true; }
    int  GetItemWidth() const override;
    int  GetItemWidthEx(void* hDC) const override;
    void DrawItem(void* hDC, int x, int y, int w, int h, bool dark_mode) override;

    int OnMouseEvent(MouseEventType type, int x, int y, void* hWnd, int flag) override;

private:
    std::wstring CurrentValue() const; // live value text for this field

    ClashField   m_field;
    ClashPlugin* m_plugin;

    std::wstring m_name;
    std::wstring m_id;
    std::wstring m_label;
    std::wstring m_sample;
    mutable std::wstring m_value; // rebuilt on each query from live plugin state
};
