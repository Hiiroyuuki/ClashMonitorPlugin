# ClashMonitorPlugin 项目总结

这份文档用于把当前项目交给另一个 AI 或开发者继续修改。项目路径：

`D:\pythonProject\trafficmonitor-plugins\ClashMonitorPlugin`

实际运行软件路径：

`D:\download\TrafficMonitor_V1.86_x86\TrafficMonitor`

实际插件目录：

`D:\download\TrafficMonitor_V1.86_x86\TrafficMonitor\plugins`

当前 TrafficMonitor 是 x86/32 位版本，所以最终 DLL 必须是 `pei-i386`。如果编译成 x64，会报：

`插件模块加载失败，故障代码:193 %1不是有效的Win32应用程序。`

## 项目定位

这是一个 TrafficMonitor 插件，用 C++/Win32 编写，编译成 `ClashMonitorPlugin.dll`。

功能：

- 在 TrafficMonitor 任务栏窗口显示 Clash 状态。
- 暴露四个独立组件：
  - `ClashNode`: 当前节点，点击后打开节点选择菜单。
  - `ClashMode`: 当前模式，点击后打开 Rule/Global/Direct 菜单。
  - `ClashLatency`: 当前节点延迟，点击后主动检测延迟。
  - `ClashProxy`: 系统代理 ON/OFF，点击后切换系统代理。
- 从 Clash API 读取 `/connections`、`/proxies`、`/configs`。
- 通过 Clash API 写入 `/proxies/{group}` 和 `/configs` 来切换节点/模式。
- 通过 Windows Internet Settings 修改系统代理。

## 关键文件

### `PluginInterface.h`

TrafficMonitor 插件接口定义。核心接口：

- `ITMPlugin::GetItem(int index)`: 返回一个插件显示组件。
- `IPluginItem::DrawItem(...)`: 自绘组件。
- `IPluginItem::OnMouseEvent(...)`: 处理点击、滚轮等事件。
- `ITMPlugin::OnExtenedInfo(EI_CONFIG_DIR, ...)`: TrafficMonitor 传入插件配置目录。

注意：当前接口没有 mouse-move 事件，所以“鼠标移到组件上边框变色”只能在 `DrawItem` 时用当前鼠标位置判断，效果取决于宿主重绘频率。

### `ClashPlugin.h / ClashPlugin.cpp`

插件主类 `ClashPlugin`。

职责：

- 初始化四个 `ClashInfoItem`。
- 拉取 Clash 数据并缓存。
- 构建 Tooltip。
- 处理菜单、切换节点、切换模式、切换系统代理。
- 实现 node picker 自绘窗口。

重要成员：

- `m_items[4]`: 四个显示组件。
- `m_currentNodeName`: 当前节点名。
- `m_currentMode`: 当前 Clash 模式。
- `m_proxyGroupName`: 当前主代理组。
- `m_currentDelay`: 当前延迟。
- `m_groups`: Clash 代理组和节点缓存。
- `m_latencyRefreshing`: 延迟检测中状态。
- `m_forceProxyRefetch`: 延迟检测完成后强制刷新 `/proxies`。
- `m_darkHint`: 最近一次组件绘制时的暗色模式提示，用于菜单配色。

### `ClashNodeItem.h / ClashNodeItem.cpp`

四个显示组件共用一个类 `ClashInfoItem`，用 `ClashField` 区分：

- `Node`
- `Mode`
- `Latency`
- `Proxy`

职责：

- 返回 item name/id/label/value。
- 计算组件宽度。
- 自绘 chip 背景、label、value、延迟圆点。
- 处理点击事件。

重要逻辑：

- `CurrentValue()` 决定显示文本。
- `GetItemWidthEx()` 用当前文本测量宽度。
- `DrawItem()` 负责绘制圆角 chip。
- `OnMouseEvent()` 决定点击动作。

### `HttpHelper.*`

WinHTTP 封装，用于 Clash REST API。

支持：

- `GET`
- `PUT`
- `PATCH`
- Base URL、secret、timeout 设置。

### `JsonParser.*`

项目自带简单 JSON 解析器，无第三方依赖。

### `Settings.*`

读写 `ClashMonitor.ini`。

实际插件配置文件位于：

`D:\download\TrafficMonitor_V1.86_x86\TrafficMonitor\plugins\ClashMonitor.ini`

当前配置示例：

```ini
[Connection]
Host=127.0.0.1
Port=9097
Secret=admin
Timeout=3000
RefreshInterval=2000
[Proxy]
Group=
AutoDetectGroup=1
[Display]
ShowNodeDelay=1
ShowUpDownSpeed=1
```

### `SystemProxy.*`

修改 Windows 系统代理：

- `SystemProxy::IsEnabled()`
- `SystemProxy::Enable(server)`
- `SystemProxy::Disable()`

## 当前暴露的四个组件

在 `ClashPlugin::ClashPlugin()`：

```cpp
m_items[0].Init(ClashField::Node,    this);
m_items[1].Init(ClashField::Mode,    this);
m_items[2].Init(ClashField::Latency, this);
m_items[3].Init(ClashField::Proxy,   this);
```

在 `ClashPlugin::GetItem(int index)`：

```cpp
if (index >= 0 && index < 4)
    return &m_items[index];
return nullptr;
```

TrafficMonitor 通过这个接口枚举插件 item。

## 已完成的修改

### 1. 延迟刷新时不显示 refreshing

原需求：不要显示 `refreshing`，显示两个横杠 `--`，字体和圆形都变灰。

当前实现：

- `ClashInfoItem::CurrentValue()` 中，`Latency` 检测中返回 `--`。
- `DrawItem()` 中，如果 `m_latencyRefreshing` 为 true，延迟圆点和文字颜色使用 `RGB(150, 150, 150)`。

关键位置：

```cpp
if (m_plugin->IsLatencyRefreshing()) return L"--";
```

```cpp
COLORREF sev = refreshing ? RGB(150, 150, 150) : SeverityColor(connected, delay);
```

### 2. 修复延迟刷新导致 UI 宽度跳动

问题：

`Latency` 从 `113 ms` 变成 `--` 时，`GetItemWidthEx()` 按 `--` 测宽，组件变窄；检测完成后又变宽，导致 UI 抖动。

当前实现：

`Latency` 组件测宽时，取当前文本和样例 `8888 ms` 的最大宽度，显示仍然是 `--`，但宽度稳定。

关键逻辑：

```cpp
if (m_field == ClashField::Latency)
{
    std::wstring stableText = m_label + L" " + m_sample;
    SIZE stableSz = { 0, 0 };
    GetTextExtentPoint32W(hdc, stableText.c_str(), static_cast<int>(stableText.size()), &stableSz);
    if (stableSz.cx > sz.cx) sz.cx = stableSz.cx;
}
```

### 3. 组件 hover 边框变色

需求：鼠标移到组件上后，组件边缘变色，让用户知道选中了组件。

当前实现：

在 `DrawItem()` 中：

- 用 `WindowFromDC(hdc)` 获取绘制窗口。
- 用 `GetCursorPos` + `ScreenToClient` 获取鼠标位置。
- 用 `PtInRect` 判断鼠标是否在当前 chip 内。
- hover 时边框变蓝。

限制：

TrafficMonitor 插件接口没有 mouse-move 回调，所以 hover 状态只会在宿主触发重绘时更新。不是完整实时 hover，但可用。

关键颜色：

- 暗色：`RGB(92, 170, 255)`
- 亮色：`RGB(0, 120, 215)`

### 4. Node 菜单高度限制和鼠标滚轮

当前 node 菜单是自定义 popup window + owner-drawn listbox，不再用标准 Windows menu。

原因：

标准 `HMENU/TrackPopupMenu` 不适合限制半屏高度，也不好控制滚轮。

实现位置：

- `ShowNodePicker(...)`
- `PickerWndProc(...)`
- `PickerListProc(...)`
- `ScrollPickerList(...)`

功能：

- 最大高度限制为工作区高度的一半：

```cpp
int maxWinH = screenH / 2;
```

- 支持鼠标滚轮：

```cpp
case WM_MOUSEWHEEL:
    ScrollPickerList(h, st, GET_WHEEL_DELTA_WPARAM(w));
    return 0;
```

### 5. Node 菜单改为可折叠式

最新需求：

`节点选择` 默认展开，其它比如 `AI-AUTO`、`claude` 默认折叠。

当前 workspace 源码已实现：

- 新增 `PickGroup`。
- `PickGroup.expanded` 控制展开状态。
- `RebuildPickerRows(...)` 根据 group 状态重建可见 rows。
- `ReloadPickerList(...)` 刷新 listbox。
- `TogglePickerGroup(...)` 点击 header 展开/折叠。
- group header 前显示：
  - 展开：`▼`
  - 折叠：`▸`

在 `ShowNodeMenu()` 中：

```cpp
pg.expanded = (g.name == primary);
```

也就是说只有主组默认展开。当前主组一般是 `节点选择`，由 `PickPrimaryGroup(...)` 自动选出。

交互：

- 点击 group 标题：展开/折叠。
- 选中 group 标题按 Enter/Space/Left/Right：展开/折叠。
- 点击 node：切换节点。

## 当前非常重要的状态

折叠菜单代码已经在 workspace 中实现，并已编译验证：

`D:\pythonProject\trafficmonitor-plugins\ClashMonitorPlugin\ClashMonitorPlugin.dll`

验证结果：

- `objdump`: `file format pei-i386`
- 导出：`TMPluginGetInstance`
- 32 位 `LoadLibrary` 测试通过。

但是实际 TrafficMonitor 插件目录中的 DLL 没能覆盖：

`D:\download\TrafficMonitor_V1.86_x86\TrafficMonitor\plugins\ClashMonitorPlugin.dll`

原因：

TrafficMonitor 正在运行，DLL 被占用。

需要下一步：

1. 退出 TrafficMonitor。
2. 复制 workspace DLL 到实际插件目录。
3. 重新启动 TrafficMonitor。

复制命令：

```powershell
Copy-Item -LiteralPath "D:\pythonProject\trafficmonitor-plugins\ClashMonitorPlugin\ClashMonitorPlugin.dll" `
  -Destination "D:\download\TrafficMonitor_V1.86_x86\TrafficMonitor\plugins\ClashMonitorPlugin.dll" `
  -Force
```

## TrafficMonitor 配置中的空白组件问题

用户截图里曾出现四个正常组件外，多两个空 chip。

原因不是插件暴露了六个组件，而是 TrafficMonitor 配置残留旧 item：

```ini
plugin_display_item = ClashLatency,ClashMode,ClashNode,ClashNodeItem001,ClashProxy
```

并且 `[plugin_display_str_taskbar_window]` 中还有：

```ini
ClashNodeItem001 = []
 = []
```

已经清理过实际配置：

`D:\download\TrafficMonitor_V1.86_x86\TrafficMonitor\config.ini`

当前应为：

```ini
plugin_display_item = ClashLatency,ClashMode,ClashNode,ClashProxy
```

备份文件：

`D:\download\TrafficMonitor_V1.86_x86\TrafficMonitor\config.ini.bak-20260629193324`

如果空白组件再次出现，优先检查 `config.ini`：

- `[task_bar] plugin_display_item`
- `[plugin_display_str_taskbar_window]`

不要先怀疑 `GetItem()`，当前源码只返回 4 个 item。

## 构建方式

项目有多个 `.sh` 脚本，但在当前 PowerShell 环境里 `bash` 不在 PATH，所以之前用 PowerShell 直接调用 MinGW。

必须使用 32 位 MinGW：

`C:\msys64\mingw32\bin\g++.exe`

构建命令：

```powershell
$env:PATH = "C:\msys64\mingw32\bin;" + $env:PATH
& "C:\msys64\mingw32\bin\g++.exe" -std=c++17 -shared -o ClashMonitorPlugin.dll `
  dllmain.cpp ClashPlugin.cpp ClashNodeItem.cpp `
  HttpHelper.cpp JsonParser.cpp Settings.cpp `
  SystemProxy.cpp pch.cpp `
  -fno-exceptions -fno-rtti `
  -static -static-libgcc -static-libstdc++ `
  "-Wl,--gc-sections" -O2 `
  -lwinhttp -lwininet -lgdi32 -luser32
```

压缩：

```powershell
$env:PATH = "C:\msys64\mingw32\bin;" + $env:PATH
& "C:\msys64\mingw32\bin\strip.exe" ClashMonitorPlugin.dll
```

检查架构和导出：

```powershell
$env:PATH = "C:\msys64\mingw32\bin;" + $env:PATH
& "C:\msys64\mingw32\bin\objdump.exe" -p ClashMonitorPlugin.dll |
  Select-String "file format|DLL Name:|TMPluginGetInstance"
```

期望：

```text
file format pei-i386
TMPluginGetInstance
```

## 加载测试

`loadtest.cpp` 用 `LoadLibraryW` 加载 workspace 下的 DLL，并检查 `TMPluginGetInstance` 导出。

用 32 位 g++ 编译测试：

```powershell
$env:PATH = "C:\msys64\mingw32\bin;" + $env:PATH
& "C:\msys64\mingw32\bin\g++.exe" -std=c++17 loadtest.cpp -o loadtest32.exe -static
.\loadtest32.exe
```

期望：

```text
OK: DLL loaded
Export: ...
```

测试后可删除：

```powershell
Remove-Item -LiteralPath loadtest32.exe -Force
```

## Clash API 逻辑

### `FetchTraffic()`

读取：

`GET /connections`

用 `downloadTotal/uploadTotal` 的差值计算上下行速度。

注意：

这里不用 `/traffic`，因为 `/traffic` 是流式 endpoint，会阻塞轮询。

### `FetchProxies()`

读取：

`GET /proxies`

做两遍解析：

1. 从每个 proxy 的 `history` 中读取最新 `delay`，建立 `delayMap`。
2. 找出可选择的 group：
   - `Selector`
   - `URLTest`
   - `Fallback`
   - `LoadBalance`

每个 group 保存：

- `name`
- `type`
- `now`
- `nodes`

然后调用 `PickPrimaryGroup(...)` 选择主组。

主组优先级：

1. 配置文件指定 group。
2. 常见名字：
   - `节点选择`
   - `手动切换`
   - `Proxy`
   - `PROXY`
   - `select`
   - `Select`
   - `GLOBAL`
3. 节点最多的 `Selector`。
4. 第一个 group。

### `FetchConfig()`

读取：

`GET /configs`

保存：

- `mode`
- `mixed-port` 或 `port`

`mixed-port` 优先，用于系统代理地址：

`127.0.0.1:{port}`

### `SwitchProxy(group, nodeName)`

写入：

`PUT /proxies/{encoded group}`

body：

```json
{"name":"nodeName"}
```

注意：

group 名可能有中文，所以路径要 URL encode。

### `SwitchMode(mode)`

写入：

`PATCH /configs`

body：

```json
{"mode":"rule"}
```

或：

```json
{"mode":"global"}
```

或：

```json
{"mode":"direct"}
```

### `StartLatencyRefresh()`

点击 `Latency` item 后触发。

逻辑：

- CAS 设置 `m_latencyRefreshing=true`，避免重复检测。
- 后台线程创建独立 `HttpHelper`。
- 请求：

```text
/proxies/{node}/delay?timeout=5000&url=http%3A%2F%2Fwww.gstatic.com%2Fgenerate_204
```

- 请求结束后：
  - `m_forceProxyRefetch=true`
  - `m_latencyRefreshing=false`

UI 刷新时重新拉 `/proxies`，从 history 读最新延迟。

## UI 设计风格

当前四个 item 都是自绘 chip：

- 圆角矩形背景。
- label 灰色。
- value 根据状态着色。
- 延迟有小圆点。
- Proxy ON 绿色，OFF 红色。
- Latency 根据延迟分级：
  - unknown: 灰
  - timeout: 红
  - <=150ms: 绿
  - <=400ms: 黄
  - >400ms: 红

组件宽度按文本动态测量，只有 `Latency` 为避免刷新时跳动，做了最小稳定宽度。

## 可能继续优化点

### 1. Hover 体验不完全实时

原因：TrafficMonitor 插件接口没有 mouse-move 回调。

可选方案：

- 接受当前 DrawItem 时检测。
- 或在点击/菜单后触发宿主重绘，但需要 TrafficMonitor API 支持。
- 不建议开定时器强行重绘，可能影响性能。

### 2. Node picker 高度不会随折叠/展开动态调整窗口高度

当前窗口初始高度按默认可见 rows 计算，并限制半屏。

展开折叠时只刷新 listbox 内容，不动态 resize popup window。

如果要做更精致：

- `TogglePickerGroup` 后重新计算 visible row count。
- 用 `SetWindowPos` 调整 popup 和 listbox 高度。
- 仍需保证不超过半屏，并避免菜单跳动。

### 3. 折叠状态不会跨菜单打开保存

当前每次打开 node 菜单：

- 主组展开。
- 其它组折叠。

如果要保存用户上次展开状态，可在 `ClashPlugin` 中增加：

```cpp
std::unordered_set<std::wstring> m_expandedGroups;
```

但要注意 `std::wstring` hash 和线程锁。

### 4. 设置项 `ShowNodeDelay/ShowUpDownSpeed` 当前没有完全用于 UI

`Settings` 里有：

- `showNodeDelay`
- `showUpDownSpeed`

当前主要显示逻辑没有明显使用这些开关。以后可做成配置项控制是否显示延迟/速度。

### 5. 没有完整单元测试

项目目前主要靠：

- 编译。
- `LoadLibrary`。
- 实际 TrafficMonitor UI 测试。

如果要增加测试，可先测：

- `JsonParser`
- `UrlEncode`
- `PickPrimaryGroup`
- `FormatSpeed`

## 已知坑

### 1. 不要编成 x64

当前用户运行的是：

`TrafficMonitor_V1.86_x86`

必须编译 `pei-i386`。

### 2. 复制 DLL 前要退出 TrafficMonitor

否则会报：

```text
The process cannot access the file ... ClashMonitorPlugin.dll because it is being used by another process.
```

### 3. 配置残留会导致空白组件

如果看到空 chip，查：

`D:\download\TrafficMonitor_V1.86_x86\TrafficMonitor\config.ini`

尤其：

```ini
plugin_display_item =
```

### 4. 当前目录不是 git 仓库

运行过：

```text
fatal: not a git repository
```

所以不要依赖 git diff/status。

### 5. PowerShell 调 g++ 时 `-Wl,--gc-sections` 要加引号

否则 PowerShell 会把逗号当语法：

```text
Missing argument in parameter list.
```

正确：

```powershell
"-Wl,--gc-sections"
```

### 6. 手动调用 mingw32 g++ 前要把 mingw32/bin 加到 PATH

否则子进程可能找不到工具链运行库，编译失败且输出很少。

正确：

```powershell
$env:PATH = "C:\msys64\mingw32\bin;" + $env:PATH
```

## 建议给下一个 AI 的接手任务

当前最直接任务：

1. 确认 TrafficMonitor 已退出。
2. 把 workspace 下的新 DLL 复制到实际插件目录。
3. 重启 TrafficMonitor。
4. 点击 `Node`，检查：
   - `节点选择` 是否默认展开。
   - `AI-AUTO`、`claude` 等是否默认折叠。
   - 点击 group 标题是否展开/折叠。
   - 点击节点是否正常切换。
   - 鼠标滚轮是否正常。
5. 如菜单展开后高度体验不好，再优化 `TogglePickerGroup` 后的窗口 resize。

如果要继续改代码，优先看：

- `ClashPlugin.cpp` 的 picker 区域，大约从 `Node picker` 注释开始。
- `ClashNodeItem.cpp` 的 `DrawItem()` 和 `GetItemWidthEx()`。

