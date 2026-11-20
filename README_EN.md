# NekoMote

<div align="center">
  <img src="./docs/branding/nekomote_logo.svg" alt="NekoMote" />
</div>

NekoMote is an independent native C++20 runtime for E-mote 2D character models, purpose-built for Linux desktop live wallpapers.

Unlike traditional WebGL or embedded-browser solutions, NekoMote implements model evaluation, timeline animations, 4×4 bicubic Bézier mesh deformation, masking synthesis, soft-spring physics simulation, and GPU rendering as a **complete native pipeline built entirely from scratch**. It has zero dependency on browser engines, JavaScript, asm.js, or proprietary closed-source cores, enabling it to run continuously on Linux desktops with exceptionally low CPU, VRAM, and power consumption.

NekoMote currently integrates seamlessly with the unpacked assets of *NEKOPARA* (unpacked via FreeMote and normalized by this project's offline importer). It can be deployed across various Linux desktop environments via native KDE Plasma plugins, standalone Wayland layer-shell processes, standalone X11 processes, or a lightweight standalone player.

<div align="center">
  <img width="960" height="540" alt="demo" src="https://github.com/user-attachments/assets/b3ccfd23-35d0-459f-b272-4cdc56304f28" />
</div>

Demo Video:

https://github.com/user-attachments/assets/3df1f1a2-87f2-4c4a-a4eb-06ccd43b34a8

License: **GNU AGPL-3.0** (see [LICENSE](LICENSE)).

---

## Core Features

- **Independent Native Evaluation Core**: Complete implementation of parameterization, timelines, delta pose blending, mesh deformation, nested masking, soft-spring physics, gaze tracking, and touch reactions.
- **Modern Dual Rendering Backends**: Native support for **OpenGL 3.3 Core** and **Vulkan 1.0**, configured before launch via CLI arguments or plugin settings, with PCI GPU selection (`--gpu pci:...`). **Hot-switching at runtime is not supported**; changing backends requires restarting the process or reloading the plugin.
- **Mainstream Linux Desktop Shell Support**:
  - **KDE Plasma 6 / 5 Plugins**: Seamlessly embedded into the Plasma Shell scenegraph with native graphical configuration and multi-monitor management.
  - **Wayland layer-shell Standalone Shell**: For modern Wayland compositors (Sway, Hyprland, niri, river, etc.), supporting full-screen background anchoring and global gaze tracking.
  - **GNOME Shell 45+ (Wayland)**: A Shell extension displays the live wallpaper on the desktop and in the Activities overview, with desktop interaction. [Build and setup](docs/gnome45.md)
  - **X11 / EWMH Standalone Shell**: For EWMH-compliant desktop environments and window managers (GNOME on X11, XFCE, i3, bspwm, Openbox, etc.), persisting as a standard desktop root window.
  - **Standalone Window Player**: Lightweight GLFW window, ideal for out-of-the-box testing, model diagnostics, or desktop widget usage.
- **Open & Customizable Asset Pipeline**: The asset library is completely decoupled and not hardcoded to a single game. In theory, character live portraits from other Galgame titles built with the E-mote engine can also be imported and played (see Asset Preparation).
- **Audio & Lifelike Lip-Sync**: Low-latency playback via PulseAudio, with real-time RMS extraction driving mouth movements and full mute support.
- **Time-Based Dynamic Background Rotation**: Supports static PNG backgrounds as well as dynamic background groups that smoothly transition according to local system time. An open scheduling interface allows configuring arbitrary time phases (e.g., day/dusk/night presets) and transition durations.
- **Shared UI Control Panel**: Backend-agnostic embedded control panel supporting character/background switching, multi-language localization (EN / ZH-CN / ZH-TW / JA), FPS overlay, position locking, and model reset.
- **High Performance & Low Power Consumption**: Zero regular heap allocations on the steady-state frame path after warmup. Achieves a steady 60 FPS on decade-old 2nd-gen Core i5 integrated graphics (i5-2410M / HD 3000) using only ~80 MiB of RAM.
- **Disciplined Configuration Architecture**: Startup options and runtime panel preferences are stored separately following the XDG base directory specification; asset directories remain strictly read-only.

---

## Desktop Environments & Wallpaper Shells

NekoMote decouples the application layer from desktop adapters: all core wallpaper logic, UI rendering, and state management reside in shared C++ libraries and are exposed to host shells via a stable C ABI (0.8.0).

| Shell Application | Target Desktops / Compositors | Graphics Backends | Execution Model | Interaction & Gaze | Configuration |
| --- | --- | --- | --- | --- | --- |
| **KDE Plasma 6 Plugin** | KDE Plasma 6 (Wayland / X11) | OpenGL / Vulkan (configured) | In-process (plasmashell) | Touch clicks, drag, and UI panel | Desktop context menu "Configure Wallpaper" |
| **KDE Plasma 5 Adapter** | KDE Plasma 5 (X11 / Wayland) | OpenGL / Vulkan (configured) | In-process (plasmashell) | Touch clicks, drag, and UI panel | Desktop context menu "Configure Wallpaper" |
| **`nekomote_layer_shell`** | Sway, Hyprland, niri, river, etc. | OpenGL / Vulkan (at launch) | Standalone background process | Local interaction, input passthrough & global gaze | CLI flags + XDG per-instance config |
| **GNOME Shell extension** | GNOME Shell 45+ (Wayland) | OpenGL / Vulkan (configured) | Extension + standalone renderer | Desktop clicks, dragging, scrolling and shared controls | Extension preferences + XDG per-instance config |
| **`nekomote_x11`** | GNOME (X11), XFCE, i3, bspwm, Openbox, etc. | OpenGL / Vulkan (at launch) | Standalone background process | Local interaction, input passthrough & global gaze | CLI flags + XDG per-instance config |
| **`nekomote_player`** | Any desktop environment (window mode) | OpenGL / Vulkan (at launch) | Standalone foreground window | Keyboard shortcuts, mouse drag & UI panel | CLI flags + XDG config |

> [!NOTE]
> **Note on the GNOME Desktop Environment**:
> - **X11 Session (GNOME on X11)**: **Fully supported**. GNOME on X11 adheres to the standard EWMH specification and has been verified to run reliably with `nekomote_x11`.
> - **Wayland Session (GNOME on Wayland)**: Use the [GNOME Shell extension](docs/gnome45.md). Tested in isolated GNOME 45.2 and 48.7 sessions.

---

## User Guide

### 1. KDE Plasma Wallpaper Plugin

GitHub Releases provides pre-built distribution archives:
- Plasma 6: `nekomote-plasma6-debian12-<arch>.tar.gz` (`x86_64` / `aarch64`)
- Plasma 5: `nekomote-plasma5-ubuntu20-<arch>.tar.gz` (`x86_64` / `aarch64`)

Each archive contains the matching `.wallpaper.zip`, a SHA256 checksum file, and a standalone interactive `installer.py`.

#### Installation & Management

```bash
# Extract the archive (e.g. Plasma 6 / x86_64)
tar -xzf nekomote-plasma6-debian12-x86_64.tar.gz
cd nekomote-plasma6-debian12-x86_64

# Interactive management menu (Install / Reinstall / Uninstall / Status)
python3 installer.py

# Or use non-interactive commands:
python3 installer.py install      # Installs the package to ~/.local/share/plasma/wallpapers/
python3 installer.py reinstall    # Overwrite reinstall and clean legacy split files
python3 installer.py uninstall    # Uninstalls the plugin (preserves assets and preferences)
python3 installer.py status       # Inspects installation status and shared library dependencies
```

*Note: The installer requires Python ≥ 3.8 and the system's `kpackagetool6` (or `kpackagetool5`). After updating or installing, log out and back in to ensure the desktop loads the latest native modules.*

#### Enabling the Wallpaper

1. Right-click on an empty desktop area → select **Configure Desktop and Wallpaper**.
2. Select **NekoMote** from the wallpaper plugin dropdown.
3. In the settings tab, set your **Assets Directory** (e.g., `~/Pictures/NekoMoteAssets`).
4. (Optional) Select the rendering backend (OpenGL or Vulkan), toggle automatic pause on screen-off / lock, etc.

---

### 2. Wayland layer-shell Standalone Shell (`nekomote_layer_shell`)

For Wayland compositors that support `wlr-layer-shell` (Sway, Hyprland, niri, etc.), running as an anchored background surface.

#### Quick Start

```bash
# 1. Enumerate detected monitor outputs
nekomote_layer_shell --list-outputs

# 2. Run with the default OpenGL backend
nekomote_layer_shell --assets /path/to/assets --output DP-1

# 3. Run with the Vulkan backend
nekomote_layer_shell --assets /path/to/assets --output DP-1 --backend vulkan
```

#### Multi-Monitor Setups

For multiple monitors, launch a dedicated process for each output. It is recommended to add `--mute` on secondary outputs to avoid overlapping audio:

```bash
# Primary monitor: interactive with sound
nekomote_layer_shell --config ~/.config/nekomote/main.json --assets /path/to/assets --output DP-1 &

# Secondary monitor: muted
nekomote_layer_shell --config ~/.config/nekomote/sub.json --assets /path/to/assets --output HDMI-A-1 --mute &
```

#### Input Passthrough & Global Gaze

- `--input interactive` (default): Pointer events on the wallpaper are forwarded to the model and UI panel.
- `--input passthrough`: Mouse events pass straight through the wallpaper to desktop windows below. When combined with `--global-gaze on` (default), **character gaze continues following the mouse cursor across the entire screen while clicks pass through to other applications**.

---

### 3. X11 / EWMH Standalone Shell (`nekomote_x11`)

For any X11 desktop environment or window manager implementing EWMH (including GNOME on X11, XFCE, i3, bspwm, Openbox, Awesome, etc.), persisting as a `_NET_WM_WINDOW_TYPE_DESKTOP` window.

```bash
# Enumerate outputs
nekomote_x11 --list-outputs

# Launch wallpaper (default OpenGL)
nekomote_x11 --assets /path/to/assets --output HDMI-1

# Launch with Vulkan backend and dedicated configuration file
nekomote_x11 --config ~/.config/nekomote/x11.json --assets /path/to/assets --output HDMI-1 --backend vulkan
```

*Known Limitation Note: On XFCE desktops, if desktop icons are enabled (`xfdesktop` manages both icons and background in a single window), the wallpaper window may cover desktop icons. See [X11 Wallpaper Shell Notes](docs/x11.md).*

---

### 4. Standalone Window Player (`nekomote_player`)

A standard GLFW window that automatically selects Wayland or X11, perfect for out-of-the-box asset validation, model diagnostics, or running as a desktop widget.

```bash
# Directory mode: loads full assets, UI allows switching characters and backgrounds
nekomote_player --assets assets/ --character chocola/date-a

# Single-model standalone mode
nekomote_player --model assets/characters/chocola/date-a/model.nkm \
                --session assets/characters/chocola/date-a/session.nks.json --fit

# Headless / muted audio environment
nekomote_player --assets assets/ --mute
```

#### Keyboard & Mouse Controls

| Control | Action |
| --- | --- |
| **Mouse Move** | Real-time gaze tracking |
| **Left-Click (Short)** | Trigger character touch reaction and voice |
| **Left-Click & Drag** | Move character position (hold >300ms or drag >4px) |
| **Mouse Wheel** | Zoom character around pointer |
| **Top-Right Gear Icon** | Toggle UI control panel drawer |
| **Space** | Pause / Resume playback and physics |
| **F Key** | Fit model into current view |
| **R Key** | Reset character position and zoom |
| **Tab / Number Keys 1–9** | Switch characters |
| **B / N Keys** | Switch to previous / next background |
| **C Key** | Clear current background (transparent) |
| **Esc Key** | Close player |

---

### 5. Explicit Rendering GPU Selection

The standalone player, X11 shell, and layer-shell shell support selecting the physical GPU via PCI address at launch:

```bash
# 1. Enumerate local GPU devices (no display server required)
nekomote_player --list-gpus

# 2. Launch with a specific GPU (e.g. PCI address 0000:03:00.0)
nekomote_player --assets assets/ --gpu pci:0000:03:00.0
nekomote_layer_shell --assets /path/to/assets --backend vulkan --gpu pci:0000:03:00.0
```

*Note: If the selected GPU is missing or cannot present to the target output, the program exits with an explicit error rather than silently falling back to another device. The Plasma plugin always uses the compositor's host GPU. See [GPU Selection Guide](docs/gpu-selection.md).*

---

### 6. Configuration Persistence

NekoMote uses a dual-file persistence model separated under the XDG config directory (`~/.config/nekomote/`), and **never writes into the asset directory**:
- **Startup Configuration (`.json`)**: Stores asset directory path, chosen display output, rendering backend, GPU address, window size, etc. Specified via `--config FILE`, and saved only when passed `--save-config`.
- **Panel Preferences (`.panel.json`)**: Stores active character, active background, position offset, scale, FPS limit, mute state, lock transform, and language preference. **Saved atomically only when modified by user actions via the UI panel**, avoiding unnecessary disk writes on standard runs.

---

## Asset Preparation & Import

The NekoMote runtime strictly consumes **schema 6** compressed binary models (`model.nkm`) and **version 1** interaction sessions (`session.nks.json`).

> [!NOTE]
> **Open Asset Library & Custom Model Support**:
> NekoMote's core is a generic and decoupled E-mote 2D model evaluation runtime; **the asset system is completely open and not hardcoded to *NEKOPARA***.
> - **Other Titles**: In theory, dynamic character portraits from any Galgame developed using the E-mote engine can be imported and rendered, provided their model JSON, `.resx.json` resource mappings, and PNG textures are unpacked and converted using this project's offline tools (`nekomote_import` and `import_session.py`).
> - **Validation Scope**: Please note that this project's current regression tests, geometric accuracy baselines, and physics tuning are calibrated against all 195 models from the *NEKOPARA* series. **We have not conducted broad compatibility testing on E-mote models from other Galgame titles**. Models from other games that rely on unverified inheritance bits, custom extensions, or unsupported mesh constructs may be rejected during import or evaluation. Community tests and feedback are warmly welcomed.

### Method 1: Ready-to-Use Asset Bundle (Recommended)

You can directly clone or download pre-imported, pre-cropped asset packages from [NekoMoteAssets](https://github.com/Chocola-X/NekoMoteAssets). Point `--assets` or the wallpaper settings directly to the extracted directory.

### Method 2: Offline Import from Unpacked Game Files

1. **Import a single model**:
   ```bash
   build/release/bin/nekomote_import model.json model.resx.json --output assets/characters/<group>/<variant>
   ```
2. **Import interaction sessions (reaction rules, audio, touch areas)**:
   ```bash
   python3 tools/import_session/import_session.py \
     --reference-root <unpacked_root> \
     --model <model_name> \
     --converter build/release/bin/nekomote_audio_convert \
     --character-root assets/characters/<group> \
     --output assets/characters/<group>/<variant>/session.nks.json
   ```
3. **Batch import all characters and backgrounds**:
   ```bash
   python3 tools/import_all_assets.py --reference-root <unpacked_root> \
     --importer build/release/bin/nekomote_import \
     --session-tool tools/import_session/import_session.py \
     --output assets/characters --report assets/import-report.json
   python3 tools/import_backgrounds.py --reference-root <unpacked_root> --dest assets
   ```

### Asset Directory Structure

```text
assets/
├── characters/
│   ├── characters.json               # Optional: localized display name dictionary (EN/ZH-CN/ZH-TW/JA)
│   └── <character_group>/            # Top-level: group (e.g. chocola)
│       ├── audio/main/*.flac         # Voice audio (lossless FLAC; little/ for young voice)
│       └── <variant>/                # Second-level: model variant (e.g. date-a)
│           ├── model.nkm             # schema 6 compressed model (MessagePack + zlib)
│           ├── session.nks.json      # Reaction session rules
│           ├── textures/             # Cropped and optimized PNG atlases
│           └── audio -> ../audio/main
├── backgrounds/
│   ├── background.json               # Optional: localized background name dictionary
│   ├── BG_07A.png                    # Static background (identified by file stem)
│   └── dynamic/
│       └── <group_name>/             # Dynamic background group (e.g. BG_01)
│           ├── config.json           # Dynamic phase configuration (arbitrary phases & fade duration)
│           ├── A.png                 # Phase images (custom file names allowed)
│           ├── B.png
│           └── C.png
└── icons/
    └── toggle.png                    # UI toggle icon
```

#### Time-Based Dynamic Background Configuration (`config.json`)

The engine provides an open time-based scheduling mechanism that is not limited to three phases. You can define any number of phases in `config.json`:

```json
{
  "phases": [
    {"image": "A.png", "start": "06:00"},
    {"image": "B.png", "start": "12:00"},
    {"image": "C.png", "start": "16:00"},
    {"image": "D.png", "start": "18:00"}
  ],
  "transition_seconds": 1.8
}
```

- `start`: 24-hour time string `HH:MM`. The engine sorts phases by minute-of-day and smoothly transitions to the matching phase based on local system time.
- `transition_seconds`: Duration (in seconds) of the crossfade transition between phases.

---

## Building from Source

### System Requirements

- **Build Tools**: CMake ≥ 3.25, Ninja, C++20-compatible compiler (GCC ≥ 11 or Clang ≥ 14), Python 3 + Jinja2
- **Core Dependencies**: libpng 1.6, zlib, Fontconfig (text rendering & font discovery), PulseAudio (libpulse ≥ 13.99)
- **Windowing & Graphics Backends**:
  - OpenGL 3.3 Core driver; Vulkan 1.0 requires Vulkan SDK (headers + `glslangValidator`) and ICD loader
  - X11 shell requires `libX11`, `libXrandr`, `libXext` development headers
  - Wayland layer-shell requires `wayland-client ≥ 1.20`, `wayland-scanner`, `wayland-egl`, `wayland-cursor`, and `EGL ≥ 1.5`
  - Plasma plugins require Qt Quick (Plasma 6 needs Qt ≥ 6.4; Plasma 5 needs Qt ≥ 5.12)

### CMake Presets Summary

| Preset | Type | Characteristics | Included Components |
| --- | --- | --- | --- |
| `release` | Release | LTO optimization, OpenGL + Vulkan 1.0, warnings as errors | Standalone player, import/conversion tools, ABI diagnostic host |
| `system` | Release | Strict distro system packages (disables network downloads) | Same as release |
| `dev` | Debug | Fast incremental build, debug symbols, unit tests | All tools and test suites |
| `plasma` | Release | LTO, Qt6 + Plasma 6 QML plugin | Plasma 6 wallpaper bundle staging target |
| `plasma5` | Release | LTO, Qt5 + Plasma 5 QML plugin | Plasma 5 wallpaper bundle staging target |
| `layer-shell`| Release | LTO, lightweight standalone Wayland shell, no Qt/GLFW | `nekomote_layer_shell` binary |
| `x11` | Release | LTO, lightweight standalone X11 shell, no Qt/GLFW | `nekomote_x11` binary |
| `asan` | Debug | Address & undefined behavior sanitizers (ASan + UBSan) | Windowless core and C ABI test suite |

### Build Commands

```bash
# 1. Build standard Release binaries (uses pinned dependencies via FetchContent)
cmake --preset release
cmake --build --preset release -j$(nproc)

# 2. Build standalone Wayland layer-shell shell
cmake --preset layer-shell
cmake --build --preset layer-shell -j$(nproc)

# 3. Build standalone X11 shell
cmake --preset x11
cmake --build --preset x11 -j$(nproc)

# 4. Build KDE Plasma 6 plugin bundle
cmake --preset plasma -DNEKOMOTE_BUILD_PLASMA_PROBE=OFF
cmake --build --preset plasma --target nekomote_plasma_stage

# 5. Build with system dependencies (offline mode)
cmake --preset system
cmake --build --preset system -j$(nproc)
```

### Running Unit Tests (No Display Server Required)

```bash
cmake --preset dev -DNEKOMOTE_BUILD_PREVIEW=OFF -DNEKOMOTE_BUILD_PLAYER=OFF
cmake --build --preset dev
ctest --test-dir build/dev -LE gpu --output-on-failure
```

---

## Performance & Benchmarks

NekoMote is architected from day one for **ultra-low resource consumption and long-term background efficiency**.

### 1. Low-End Hardware: i5-2410M / Intel HD 3000

Tested on 2026-11-13 on a legacy reference laptop:
- **Hardware**: Intel Core i5-2410M (2 cores / 4 threads @ 2.3 GHz), Intel HD 3000 GPU, 3.7 GiB RAM
- **Environment**: Debian 12, Mesa 22.3.6, OpenGL 3.3 Core, headless X11 session
- **Conditions**: 1920×1080, 60 FPS target, full dynamic evaluation, 120-frame warmup, 600-frame sampling (median of 3 runs):

| Model | Measured Loop FPS | Player CPU (4-Thread Total) | Peak Player RAM (RSS) |
| --- | ---: | ---: | ---: |
| Chocola `chocola/date-a` | **59.05 FPS** | **6.43%** (~25.7% of one core) | **79.66 MiB** |
| Chocola `chocola/wintermaid` | **58.98 FPS** | **7.62%** (~30.5% of one core) | **79.32 MiB** |
| Coconut `coconut/casual` | **58.89 FPS** | **6.49%** (~26.0% of one core) | **79.70 MiB** |

At 1080p / 30 FPS, total player CPU usage drops to **3.5%–3.9%**. Independent CPU probes measure median per-frame evaluation and frame generation time (P50) at just **0.44–0.79 ms**, with **zero heap allocations** on the tested path post-warmup. Full benchmark data is available in the [Low-End Hardware Test Report](docs/headless-lowend-test-2026-11-13.md).

### 2. Wayland Desktop: Plasma 6 Plugin and layer-shell

On 2026-11-20, the Debian 12 CI builds were tested with the same character on a Plasma 6.3.6 Wayland desktop with a Xeon E5-1680 v4 and RTX 3080 Ti. The display ran at 2560×1440 @ 180 Hz with the wallpaper capped at 60 FPS. The table shows the mean increase over a static wallpaper across two runs.

| Mode | Desktop CPU increase (one logical CPU = 100%) | GPU-wide VRAM increase | Board power increase |
| --- | ---: | ---: | ---: |
| Plasma OpenGL | +28.0 percentage points | +69.0 MiB | +10.3 W |
| Plasma Vulkan | +27.7 percentage points | +92.5 MiB | +11.6 W |
| layer-shell OpenGL | +18.5 percentage points | +164.4 MiB | +6.5 W |
| layer-shell Vulkan | +14.6 percentage points | +123.5 MiB | +6.3 W |

CPU totals include Plasma Shell, KWin, and the standalone shell when running. Plasma Shell RSS increased by about 35 MiB in the Plasma OpenGL test. See the [desktop benchmark report](docs/wayland-desktop-performance-2026-11-20.md) for per-run results and frame-rate measurements.

### 3. X11 Desktop: Plasma 6 Plugin and Standalone X11 Shell

The same machine was tested in a native X11 session with the same character, 2560×1440 @ 180 Hz display, and 60 FPS cap. The table shows the mean increase over a static wallpaper across two runs. CPU totals include Plasma Shell, KWin, Xorg, and the standalone shell when running.

| Mode | Desktop CPU increase (one logical CPU = 100%) | GPU-wide VRAM increase | Board power increase |
| --- | ---: | ---: | ---: |
| Plasma OpenGL | +21.1 percentage points | +87.8 MiB | +8.1 W |
| Plasma Vulkan | +20.5 percentage points | +115.3 MiB | +11.8 W |
| X11 shell OpenGL | +19.4 percentage points | +186.1 MiB | +6.2 W |
| X11 shell Vulkan | +18.7 percentage points | +180.4 MiB | +10.1 W |

The Plasma plugin drew and swapped at about 58.8 frames per second. The standalone shell submitted about 57.7–58.4 frames per second. GPU-wide VRAM differed by about 65 MiB between the two static baselines. See the [X11 desktop benchmark report](docs/x11-desktop-performance-2026-11-20.md) for per-run results and process attribution.

---

## Project Structure

```text
src/model/            Strongly typed immutable model asset definitions, geometry & math primitives
src/runtime/          Parameterization, scenegraph, timeline animation, mesh deformation, physics
src/asset_io/         Compact format loading, asset validation, atlas decoding
src/render_gl/        OpenGL 3.3 Core backend executor & GPU state management
src/render_vk/        Vulkan 1.0 backend executor & command buffer recording
src/application/      Wallpaper Controller, session scheduling, configuration, audio & interaction
src/ui/               Backend-agnostic UI layout, Hit-Testing, localization & rendering protocols
src/abi/              Stable high-level C ABI (0.8.0) exposed to host shells
src/host_qt_quick/    Qt Quick / Plasma host integration glue
src/host_vulkan/      Standalone Vulkan WSI swapchain & presentation helpers
adapters/plasma/      KDE Plasma 6 wallpaper QML plugin
adapters/plasma5/     KDE Plasma 5 adapter plugin
adapters/layer_shell/ Standalone layer-shell wallpaper executable for Wayland
adapters/x11/         Standalone desktop wallpaper executable for X11/EWMH
apps/player/          GLFW-based standalone interactive player
apps/abi_host/        Pure C ABI deterministic diagnostics & regression test host
apps/preview/         Part-level mesh & atlas diagnostic preview tool
tools/                Model import, audio conversion, PNG optimization & packaging scripts
docs/                 Architecture specifications, module contracts, guides & benchmark reports
```

---

## Documentation Navigation

- **Architecture & Conventions**: [AGENTS.md](AGENTS.md)
- **Development Roadmap**: [ROADMAP.md](ROADMAP.md)
- **C ABI Contract Specification**: [docs/abi-contract.md](docs/abi-contract.md)
- **Configuration Guide**: [docs/configuration.md](docs/configuration.md)
- **GPU Selection Guide**: [docs/gpu-selection.md](docs/gpu-selection.md)
- **Building Guide**: [docs/building.md](docs/building.md)
- **Dependencies & Packaging**: [docs/dependencies.md](docs/dependencies.md)
- **KDE Plasma Distribution & Installation**: [docs/plasma-distribution.md](docs/plasma-distribution.md)
- **Wayland layer-shell Guide**: [docs/layer-shell.md](docs/layer-shell.md)
- **X11 Wallpaper Shell Guide**: [docs/x11.md](docs/x11.md)
- **Full Documentation Index**: [docs/README.md](docs/README.md)
