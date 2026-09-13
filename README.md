# system-overlay

Lightweight, game-style performance overlay (HUD) for **CachyOS / Arch Linux + KDE Plasma 6**.
It shows live system statistics in the **bottom-right corner of the screen** as a vertical
stack, rendered as a real Wayland **layer-shell overlay** so it stays visible above normal
windows, maximized windows, browser fullscreen (F11) and KWin fullscreen applications —
while remaining completely transparent to input (click-through, no focus, no keyboard,
no taskbar entry). Bottom-anchored positions automatically stay clear of panels.

```
CPU: 10% 49°C
GPU: 14% 47°C
RAM: 5.4/31.3 GiB
VRAM: 0.5/8.0 GiB
```

Each row has a thin (3 px) progress bar underneath, and every percentage-based
value shares one colour function: normal green below the warning threshold,
**yellow at ≥75 %** and **red at ≥90 %** (thresholds and colours configurable,
see `[colors]` below). RAM shows `used/total` from `/proc/meminfo`; VRAM total
comes from the kernel (AMD `mem_info_vram_total`, NVIDIA NVML/`nvidia-smi`,
Intel: largest PCI memory BAR — hidden when not discoverable). The tray menu
has checkable CPU/GPU/RAM/VRAM entries to show or hide rows at runtime.

<!-- Screenshot: overlay above the Plasma panel in the bottom-right corner,
     light green monospace text, one metric per line -->

## What it shows

| Metric | Source |
|---|---|
| CPU utilization | `/proc/stat` delta (user, nice, system, idle, iowait, irq, softirq, steal) |
| CPU temperature | `/sys/class/hwmon/*` — k10temp (Tctl/Tdie), zenpower, coretemp (Package id) or any CPU-ish label; discovered, never hard-coded |
| RAM used | `/proc/meminfo`: `MemTotal − MemAvailable` (the meaningful "used" value, not `MemTotal − MemFree`) |
| GPU utilization / temperature / VRAM | Vendor backend (see below), discovered via `/sys/class/drm/card*` |

Missing sensors are never fatal — the HUD shows `--%` / `--°C` / `-- GiB` and keeps running.

## How the overlay works (Wayland)

On KDE Plasma / KWin Wayland the HUD is a **`zwlr_layer_shell` surface with layer
`overlay`**, created through KDE's **LayerShellQt**:

- layer = **overlay** (above normal windows and fullscreen applications)
- anchor = configurable corner, default **bottom + right**, margins = configurable
  offsets (default 10, 10 px from the anchored edges)
- **exclusive zone = 0 on bottom corners** → the HUD itself reserves no space but the
  compositor keeps it above bottom panels (never overlaps the Plasma panel);
  top corners use −1 (ignore panels entirely). Neither mode pushes other
  applications around — it stays a HUD, not a panel
- **keyboard interactivity = none** → it can never take keyboard focus
- **empty input region** (`wl_surface.set_input_region`) → the compositor itself routes
  all pointer clicks to the applications below, even when clicking exactly on the text

Why LayerShellQt instead of a plain always-on-top Qt window? On Wayland there is no
global "always on top". An ordinary toplevel cannot reliably stay above fullscreen
windows, and click-through cannot be guaranteed at compositor level. Layer-shell is
the mechanism KDE's own panels/OSDs use, and KWin honours it for fullscreen windows.

Verified at protocol level with `WAYLAND_DEBUG=1`: `get_layer_surface(..., layer=3,
"system-overlay")`, `set_anchor(10)` (bottom|right), `set_exclusive_zone(0)`,
`set_keyboard_interactivity(0)`, `set_input_region(<empty>)`.

### X11 fallback

On X11 sessions (`QT_QPA_PLATFORM=xcb` or a real X11 login) the overlay uses a frameless
`Qt::Tool` window with `WindowStaysOnTopHint`, `Qt::WindowTransparentForInput` (the Qt xcb
platform sets an **empty X11 input shape via XFixes** — server-level click-through) and
`Qt::WindowDoesNotAcceptFocus` (clears WM_HINTS input). `_NET_WM_WINDOW_TYPE_UTILITY`
keeps it out of the taskbar and Alt+Tab.

## GPU backends

The primary GPU is discovered from `/sys/class/drm/card*` (ranked by connected display
connectors and `boot_vga`; `card0` is *not* assumed to be the gaming GPU).

### AMD (amdgpu)
- utilization: `card*/device/gpu_busy_percent`
- VRAM: `card*/device/mem_info_vram_used` / `mem_info_vram_total` (exact)
- temperature: `card*/device/hwmon/hwmon*/temp*_input` (label preference: edge > junction > package)

### NVIDIA
- NVML loaded at **runtime via dlopen** (`libnvidia-ml.so.1`) — no build dependency; the
  binary builds and runs on machines without NVIDIA drivers
- fallback: one long-running `nvidia-smi --query-gpu=... -lms <interval>` process
  (no process spawning per refresh)
- device selected by PCI bus id so multi-GPU machines report the right card

### Intel (i915 / xe)
- temperature: `card*/device/hwmon/hwmon*/temp*_input` (exact)
- utilization: sum of per-client cumulative engine busy time from `/proc/*/fdinfo`
  (`drm-engine-render/compute/copy`, deduplicated by `drm-client-id`) divided by wall
  time. Video-engine time is not counted. Processes of other users cannot be read
  without root and are skipped.
- VRAM: shared-buffer-aware estimate from `/proc/*/fdinfo` — raw client sums
  double count every buffer shared between clients (compositor surfaces,
  dma-buf imports) and can inflate far beyond the physical size, so the HUD
  computes `Σ max(0, total−shared) + largest shared pool` and clamps the
  result to the physical size. Still an approximation: multiple independent
  shared pools are under-counted and VRAM used by root-owned processes is
  invisible.
- i915 PMU (`engine-busy` counters) would give exact utilization but requires
  `perf_event_paranoid ≤ 1`; the fdinfo method needs no privileges.
- Unknown GPUs simply show `--` values; the application never crashes on them.

## Build dependencies (CachyOS / Arch)

```
sudo pacman -S --needed base-devel cmake qt6-base layer-shell-qt
```

(`qt6-wayland` is already present on any KDE Plasma system. X11 fallback needs no extra
packages — it uses Qt's xcb platform, which is part of `qt6-base`.)

## Build & install

```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build
sudo cmake --install build          # installs /usr/bin/system-overlay + .desktop file
```

Uninstall:

```bash
sudo cmake --build build --target uninstall   # removes the files from build/install_manifest.txt
```

Run without installing:

```bash
./build/system-overlay
```

## Closing / pausing / colours

The HUD itself never accepts input (that is the point of a click-through overlay), so it
ships with a **system tray icon** (painted, no assets). Right-click it for:

- **Gizle / Göster** — hide or restore the HUD
- **Duraklat / Devam et** — freeze/resume metric updates
- **CPU / GPU / RAM / VRAM** checkboxes — show or hide rows at runtime
- **Renk ▸** — change the base (normal-band) HUD colour:
  - preset palette (9 tints, applied instantly),
  - **RGB gir…** — type a colour as `#RRGGBB`, bare `rrggbb` or `R,G,B` (0-255);
    invalid input shows a tray notification,
  - **Renk penceresi…** — the full Qt colour dialog.
  The choice is saved to `[display] text_color` and survives restarts. Warning/critical
  alert colours keep coming from `[colors]`.
- **Çıkış** — quit the application

The tray can be disabled with `[display] show_tray=false` (then the only way to stop the
app is `killall system-overlay`).

## Command line

```
system-overlay --help
system-overlay --version
system-overlay --interval 500        # refresh interval ms (100–60000, default 1000)
system-overlay --screen primary      # primary | all | output name (DP-1, HDMI-A-1, ...)
system-overlay --debug               # dump hwmon + GPU source discovery to stderr
```

`--debug` example output:

```
hwmon scan:
  hwmon: k10temp (/sys/class/hwmon/hwmon3)
    Tctl = 44.8 C  [/sys/class/hwmon/hwmon3/temp1_input]
  selected CPU temp: k10temp / Tctl -> /sys/class/hwmon/hwmon3/temp1_input
metric sources:
Detected GPU:
  GPU Intel (card1): Intel (fdinfo + hwmon)
  utilization source: /proc/*/fdinfo drm-engine-{render,compute,copy} (sum of client deltas)
  VRAM source: /proc/*/fdinfo drm-total-local0 (sum over clients, approximate)
  temperature source: /sys/class/drm/card1/device/hwmon/hwmon2/temp1_input
```

## Configuration

`~/.config/system-overlay/config.ini` (written with defaults on first run; missing
keys/values fall back to defaults and never crash):

```ini
[general]
refresh_interval=1000          ; ms; 250/500/1000/2000 are sensible values

[display]
screen=primary                 ; primary | all | output name
position=bottom-right          ; top-left | top-right | bottom-left | bottom-right
offset_x=10                    ; distance from the anchored edge(s)
offset_y=10                    ; bottom positions also avoid panels
font_size=14                   ; logical pixels
font_family=monospace
show_background=false          ; optional translucent panel behind the text
show_tray=true                 ; tray icon (hide/restore/pause/quit menu)
text_color=#a6f28f
outline_color=#000000

[colors]
# One colour function for every percentage-based metric (CPU/GPU
# utilisation, RAM/VRAM fill): at >= warning_pct the row turns
# warning_color, at >= critical_pct critical_color.
warning_pct=75
critical_pct=90
warning_color=#f2d24f
critical_color=#f25d5d

[metrics]
show_cpu_usage=true
show_cpu_temp=true
show_gpu_usage=true
show_gpu_temp=true
show_ram=true
show_vram=true
```

## Autostart (opt-in)

Nothing is enabled automatically. Options:

**XDG autostart (recommended, most reliable on Plasma Wayland):**

```bash
mkdir -p ~/.config/autostart
cp /usr/share/applications/system-overlay.desktop ~/.config/autostart/
```

(Or build with `-DINSTALL_AUTOSTART=ON` to install it system-wide into `/etc/xdg/autostart`.)

**systemd user service (alternative):** `contrib/system-overlay.service` — copy it to
`~/.config/systemd/user/`, adjust `ExecStart` if needed, then
`systemctl --user enable --now system-overlay.service`. The unit is tied to
`graphical-session.target`; on Plasma this works, but the XDG autostart method above is
the most robust because it inherits the full session environment.

## Performance

Measured on the development machine (Ryzen 7 5700X, Arc A750, Plasma Wayland):
**≈0.8 % of one core at the default 1000 ms interval** (≈1.8 % at 500 ms), RSS ≈ 60 MB,
no worker threads, no process spawning. The only periodic work is a handful of
sysfs/procfs reads; the `/proc/*/fd` scan (Intel backend) runs as an incremental
cached scan. Text is rasterized once per refresh and blitted on frame updates.

## Supported / tested scenarios

| Scenario | Result |
|---|---|
| KDE Plasma Wayland desktop | ✅ visible, correct values |
| Normal / maximized application windows | ✅ stays on top |
| Browser fullscreen (F11, Chromium-based) | ✅ stays on top |
| KWin fullscreen application (mpv `--fullscreen`) | ✅ stays on top |
| Bottom Plasma panel | ✅ HUD sits above it (exclusive-zone avoidance), never overlaps |
| Click-through (compositor-level empty input region) | ✅ verified in Wayland protocol trace; click lands on the app below |
| Focus stealing | ✅ none (`keyboard_interactivity=none`, `activateOnShow=false`; active window unchanged) |
| Taskbar / Alt+Tab | ✅ absent (KWin reports `skipTaskbar/skipSwitcher/skipPager = true` for the layer surface) |
| KDE scaling 100 % | ✅ pixel-perfect |
| KDE scaling 125/150/200 % | ✅ position and rendering survive live scale changes; see limitation below |
| X11 (KWin_X11 / XWayland) | ✅ builds and runs as frameless always-on-top input-transparent tool window |

### Known limitations

- **Fractional scaling (125/150 %):** the HUD stays at the correct position and keeps
  working, but its text is rendered at its nominal pixel size — it appears smaller than
  the rest of the scaled desktop (the layer-shell surface is not grown by the fractional
  factor). Compensate with `font_size` if you use fractional scaling.
- **Wayland is not visible from:** text consoles (VT switch), DRM-lease clients
  (VR compositors) that take over the display, and scenarios where the display is driven
  outside the compositor. The overlay is composited by KWin and cannot appear over those.
- **Intel VRAM** is an approximation (see Intel backend section). AMD VRAM is exact.
- **Intel/NVIDIA GPU utilization** does not include video-engine load (decode/encode);
  it reflects render+compute+copy engines.
- The HUD only reads what an unprivileged user can read; it never needs root, never
  spawns shells, and never chmods sensors.

## Troubleshooting

- `--debug` prints every discovered hwmon chip and the exact data sources chosen.
- `CPU: --°C` → no CPU sensor matched (check `--debug`); only k10temp/zenpower/coretemp
  and CPU-labelled sensors are considered to avoid bogus ACPI values.
- `GPU: --% --°C` → no supported/known GPU or unreadable sources; on NVIDIA make sure
  `libnvidia-ml.so.1` (driver package) or `nvidia-smi` is available.
- If the overlay does not appear on Wayland, run with `WAYLAND_DEBUG=1` and check for
  `zwlr_layer_shell_v1`; on compositors without layer-shell (e.g. GNOME) the window
  falls back to an ordinary always-on-top tool window with reduced guarantees.
- Values look frozen at 0 % right after launching a game → GPU clients are picked up by
  the periodic scan within ~2 s.

## Project layout

```
src/
  main.cpp                    CLI, wiring
  config/Config.*             INI config + defaults, CLI overrides
  metrics/                    MetricManager (QTimer), CpuMetrics, MemoryMetrics
  sensors/HwmonScanner.*      hwmon enumeration + CPU temp selection
  gpu/                        GpuDiscovery (DRM cards), GpuBackend interface,
                              AmdGpuBackend, IntelGpuBackend, NvidiaGpuBackend, GpuMetrics
  overlay/                    OverlayWindow (QWindow+QBackingStore renderer),
                              WaylandOverlay (LayerShellQt), X11Overlay, OverlayController,
                              TrayIcon (system tray: hide/pause/quit)
resources/system-overlay.desktop.in
contrib/system-overlay.service
```

## License

MIT — see `LICENSE`.
