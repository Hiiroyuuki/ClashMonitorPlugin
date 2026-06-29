# Clash Monitor — TrafficMonitor V1.86 Plugin

Displays Clash proxy status in the Windows taskbar via [TrafficMonitor](https://github.com/zhongyang219/TrafficMonitor). Four clickable components: current node, proxy mode, latency, and system proxy toggle.

## Installation

1. Copy `ClashMonitorPlugin.dll` to the TrafficMonitor `plugins\` folder (e.g. `C:\Program Files\TrafficMonitor\plugins\`).
2. **Do NOT copy `libwinpthread-1.dll`** — statically linked, not needed.
3. Restart TrafficMonitor.
4. Right-click the TrafficMonitor taskbar window → **Display Settings** → check ClashNode, ClashMode, ClashLatency, ClashProxy.
5. Right-click any Clash item → configure Clash API connection (host, port, secret).

## Components

| Item | Behavior | Click Action |
|---|---|---|
| **ClashNode** | Current proxy node name | Opens collapsible node picker |
| **ClashMode** | Current mode (Rule/Global/Direct) | Opens mode switch menu |
| **ClashLatency** | Current node latency in ms | Triggers latency test |
| **ClashProxy** | System proxy ON/OFF | Toggles system proxy |

## Features

- **Collapsible node picker** — primary group (`节点选择`) expanded by default, other groups collapsed. Group headers toggle with click or Enter/Space. Supports mouse wheel scrolling.
- **Latency severity coloring** — green (≤150ms), yellow (≤400ms), red (>400ms), gray (unknown).
- **Hover border** — chip border changes color on hover for click feedback.
- **System proxy** — toggle Windows system proxy via WinINet, pointing to Clash mixed port.
- **Stable latency width** — latency item width pinned to `8888 ms` sample to prevent UI jumping during refresh.

## Configuration

Plugin config at `plugins\ClashMonitor.ini`:

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

- `Group` — override primary proxy group (empty = auto-detect).
- `AutoDetectGroup` — auto-detect primary group by naming convention (`节点选择` → `Proxy` → `Selector` with most nodes).
- `Secret` — Clash Dashboard API secret (set in Clash config as `secret`).

## Build

| Arch | Script | Toolchain | Output |
|---|---|---|---|
| x86 (32-bit) | `bash build32-final.sh` | MSYS2 mingw32 | `ClashMonitorPlugin.dll` |
| x64 (64-bit) | `bash build64.sh` | MSYS2 mingw64 | `ClashMonitorPlugin_x64.dll` |

**IMPORTANT**: Target architecture must match TrafficMonitor version. For `TrafficMonitor_V1.86_x86`, build with `build32-final.sh` to produce `pei-i386` DLL. x64 DLL will fail with error 193 (not a valid Win32 application).

Or via CMake:
```bash
mkdir build && cd build
cmake .. -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

Build flags: `-static -static-libgcc -static-libstdc++ -fno-exceptions -fno-rtti -O2 -Wl,--gc-sections`

## Clash API Endpoints Used

| Endpoint | Purpose |
|---|---|
| `GET /proxies` | Node list, groups, delays |
| `GET /connections` | Upload/download totals for speed |
| `GET /configs` | Current mode, mixed port |
| `PUT /proxies/{group}` | Switch node |
| `PATCH /configs` | Switch mode |
| `GET /proxies/{node}/delay` | Trigger latency test |

## Compatibility

- **Target**: TrafficMonitor V1.86 / Plugin API Version 7
- **Windows**: 7 SP1+ (32-bit DLL) or 64-bit Windows (x64 DLL)
- **Compiler**: MSYS2 mingw32-g++ / mingw64-g++, C++17
- **Dependencies**: Windows system DLLs only (KERNEL32, USER32, GDI32, WINHTTP, WININET)
- **No third-party libraries** — custom JSON parser, no nlohmann/json, no libcurl

## Known Issues

- **Hover not real-time** — TrafficMonitor plugin API has no mouse-move callback. Hover detection runs in `DrawItem()`, only refreshes when host repaints.
- **Picker height static** — node picker window height doesn't resize on group expand/collapse. Max height capped at half screen.
- **Collapse state not persisted** — groups reset to default (primary expanded, others collapsed) each time picker opens.

## License

MIT
