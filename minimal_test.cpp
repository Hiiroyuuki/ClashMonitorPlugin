// Minimal test plugin — isolates build system from logic
#include "PluginInterface.h"

class MinimalItem : public IPluginItem
{
public:
    const wchar_t* GetItemName() const override { return L"Minimal"; }
    const wchar_t* GetItemId() const override { return L"Minimal001"; }
    const wchar_t* GetItemLableText() const override { return L"Test"; }
    const wchar_t* GetItemValueText() const override { return L"OK"; }
    const wchar_t* GetItemValueSampleText() const override { return L"OK"; }
};

class MinimalPlugin : public ITMPlugin
{
public:
    IPluginItem* GetItem(int index) override { return (index == 0) ? &m_item : nullptr; }
    void DataRequired() override {}
    const wchar_t* GetInfo(PluginInfoIndex index) override
    {
        switch (index)
        {
        case TMI_NAME:        return L"Minimal";
        case TMI_DESCRIPTION: return L"Minimal test plugin";
        case TMI_AUTHOR:      return L"Test";
        case TMI_COPYRIGHT:   return L"Test";
        case TMI_VERSION:     return L"1.0";
        case TMI_URL:         return L"";
        default:              return L"";
        }
    }
private:
    MinimalItem m_item;
};

static MinimalPlugin g_plugin;

extern "C" __declspec(dllexport) ITMPlugin* TMPluginGetInstance()
{
    return &g_plugin;
}
