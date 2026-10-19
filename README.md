# UltraXReal

**Open-source Nebula replacement for macOS. Static ultrawide + spatial head-tracked floating display for XReal Air glasses.**

<p align="center">
  <img src="https://img.shields.io/badge/macOS-13.0%2B-blue" alt="macOS">
  <img src="https://img.shields.io/badge/Swift-5.9-orange" alt="Swift">
  <img src="https://img.shields.io/badge/License-MIT-green" alt="License">
  <br><br>
  <a href="https://github.com/catagris/XReal-Ultrawide/releases/tag/v2.1.0-beta.1">
    <img src="https://img.shields.io/badge/Download-v2.1.0--beta.1-orange?style=for-the-badge" alt="Download prerelease">
  </a>
</p>

---

## Two Modes

### Static Ultrawide (v1)
Turn your XReal Air into a 21:9 ultrawide monitor. Creates a virtual 2560x1080 display that mirrors to the glasses. No head tracking — a clean, static HUD.

### Spatial Floating Display (v2) — NEW
The display floats in space. Turn your head and it stays put. Lean forward to zoom in. Uses the glasses' built-in IMU (gyroscope + accelerometer) for 60Hz head tracking with a Metal rendering pipeline.

```
                        STATIC MODE                              SPATIAL MODE
┌──────────────────────────────────┐    ┌──────────────────────────────────────────┐
│  CGVirtualDisplay (2560x1080)    │    │  CGVirtualDisplay (3840x2160 canvas)     │
│         ↓ macOS mirror ↓         │    │         ↓ ScreenCaptureKit ↓             │
│  XReal Air (1920x1080)           │    │  Metal shader (viewport crop + zoom)     │
│                                  │    │         ↓ head tracking ↓                │
│  Head-locked. Simple. Fast.      │    │  XReal Air (1920x1080 viewport)          │
└──────────────────────────────────┘    └──────────────────────────────────────────┘
```

---

## Install

### Fork prerelease (recommended for testing these fixes)

Download **[UltraXReal v2.1.0-beta.1](https://github.com/catagris/XReal-Ultrawide/releases/tag/v2.1.0-beta.1)**. The ZIP contains a universal app for Apple silicon and Intel Macs running macOS 13 or later, plus installation instructions. This prerelease includes independent floating monitors, layout controls, viewport recovery, and stationary gyro drift correction.

1. Quit any running copy of UltraXReal, unzip the download, and drag `UltraXReal.app` to Applications.
2. Open the app. It is **ad hoc signed and not notarized**. If macOS blocks it, open **System Settings > Privacy & Security**, find the blocked-app notice, and choose **Open Anyway**. See [Apple's instructions](https://support.apple.com/en-us/102445).
3. Grant Screen Recording access when prompted, then quit and reopen the app if requested. UltraXReal lives in the menu bar.

The release includes `SHA256SUMS.txt` for verifying the download. Long-term drift, sleep/wake recovery, and different glasses/macOS combinations still need wider testing. Tracking is rotational; positional tracking is not included.

### Upstream Homebrew release

These commands install the upstream release rather than this fork's prerelease:

```bash
brew tap DannyDesert/ultraxreal
brew install --cask ultraxreal
```

### Upstream Direct Download

**[Download UltraXReal-v2.0.0.dmg](https://github.com/DannyDesert/XReal-Ultrawide/releases/latest/download/UltraXReal-v2.0.0.dmg)** — Open the DMG, drag to Applications, launch.

> **Gatekeeper warning?** For an app you trust, use **System Settings > Privacy & Security > Open Anyway** after attempting to open it. See [Apple's instructions](https://support.apple.com/en-us/102445).

---

## Features

**Menu Bar App**
- Lives in your menu bar — no dock icon clutter
- Green icon = static mode active, cyan 3D icon = spatial mode active
- One-click toggle between modes

**Static Ultrawide Mode**
- Virtual 2560x1080, 3440x1440, or 3840x1080 display
- Auto-mirrors to XReal Air on enable
- Sleep/wake aware — recreates display after wake

**Spatial Floating Mode**
- 3840x2160 virtual canvas (larger than what you see)
- 1920x1080 viewport moves with your head orientation
- Lean-to-zoom: tilt forward to magnify content (1x–2.5x)
- Recenter: **Cmd+Shift+R** global hotkey
- Configurable sensitivity (Low / Medium / High)
- Configurable smoothing (Responsive / Balanced / Smooth)
- Auto-detects IMU — grays out spatial option if not available

**General**
- Launch at login
- Clean teardown on quit
- macOS 13.0+ (Ventura through Tahoe)

---

## Usage

### Static Mode
1. Connect XReal Air via USB-C
2. Click the menu bar icon → **Enable Ultrawide (Static)**
3. A virtual ultrawide display appears and auto-mirrors to the glasses
4. Drag windows onto it — you have an ultrawide desktop in your glasses

### Spatial Mode
1. Connect XReal Air via USB-C
2. Click the menu bar icon → **Enable Spatial (Floating)**
3. macOS prompts for screen recording permission (needed for capture) — approve it
4. The display floats in space — turn your head, it stays anchored
5. Press **Cmd+Shift+R** to recenter
6. Adjust sensitivity and smoothing in **Spatial Settings** submenu

---

## How It Works

### Virtual Display
Uses Apple's private `CGVirtualDisplay` API — the same API used by [BetterDisplay](https://github.com/waydabber/BetterDisplay) and [FluffyDisplay](https://github.com/tml1024/FluffyDisplay). Stable across 4+ major macOS releases.

### Head Tracking (Spatial Mode)
The XReal Air glasses contain an ICM-42688-P IMU (gyroscope + accelerometer). UltraXReal reads this sensor data over USB HID using a vendored C driver from [xrealair-sdk-macos](https://github.com/adidoes/xrealair-sdk-macos), with [Fusion](https://github.com/xioTechnologies/Fusion) Madgwick filter for sensor fusion.

### Rendering Pipeline (Spatial Mode)
```
XReal Air IMU → USB HID → hidapi → Madgwick AHRS → quaternion (60Hz)
                                                         ↓
CGVirtualDisplay → ScreenCaptureKit → IOSurface → Metal viewport shader
                                                         ↓
                                           Fullscreen NSWindow on XReal Air
```

The Metal fragment shader crops a 1920x1080 viewport from the 3840x2160 canvas based on head orientation. The viewport position updates every frame, independent of capture frame rate, so head tracking always feels responsive.

---

## Requirements

- **macOS 13.0 (Ventura)** or later
- **XReal Air** glasses (original, Air 2, Air 2 Pro, or Air 2 Ultra) via USB-C
- **Screen recording permission** (for spatial mode — macOS will prompt on first use)
- **Xcode 15+** to build from source

## Build from Source

```bash
git clone https://github.com/DannyDesert/XReal-Ultrawide.git
cd XReal-Ultrawide
open UltraXReal/UltraXReal.xcodeproj
```

1. In Xcode, select **"Sign to Run Locally"** under Signing & Capabilities
2. Build and run (`Cmd + R`)
3. The UltraXReal icon appears in your menu bar

All C dependencies (hidapi, Fusion, xreal-imu) are vendored — no external packages to install.

---

## Project Structure

```
UltraXReal/
├── UltraXReal.xcodeproj
└── UltraXReal/
    ├── UltraXRealApp.swift              # @main entry point
    ├── AppDelegate.swift                # Menu bar, mode toggle, lifecycle
    ├── VirtualDisplayManager.swift      # CGVirtualDisplay lifecycle
    ├── DisplayMirrorHelper.swift        # Display mirroring (static mode)
    ├── DisplayResolution.swift          # Resolution presets
    ├── Settings.swift                   # UserDefaults persistence
    ├── CGVirtualDisplay-Bridge.h        # Bridging header for private API
    │
    ├── Spatial/                         # Spatial floating display
    │   ├── XRealIMUService.swift        # USB HID IMU driver wrapper
    │   ├── SpatialTracker.swift         # Orientation → viewport + zoom
    │   ├── SpatialRenderer.swift        # SCK → Metal → output pipeline
    │   └── SpatialShaders.metal         # Viewport crop fragment shader
    │
    └── Vendor/                          # Vendored C dependencies
        ├── hidapi/                      # USB HID library (macOS IOKit)
        ├── fusion/                      # Madgwick AHRS sensor fusion
        └── xreal-imu/                  # XReal Air IMU protocol driver
```

## Known Limitations

- **Private API** — `CGVirtualDisplay` is undocumented by Apple. Could break in a future macOS update (stable for 4+ releases so far).
- **No App Store** — Private API + USB HID access means distribution is via GitHub/Homebrew only.
- **Spatial mode requires screen recording permission** — macOS will prompt on first use. If denied, spatial mode won't work.
- **Lean-to-zoom is a heuristic** — It uses pitch angle as a depth proxy, not true positional tracking. Works well at a desk, less so standing/walking.
- **No 6DoF** — The original XReal Air only has 3DoF (rotation). True positional tracking would require the Air 2 Ultra's SLAM cameras.

## Contributing

Contributions welcome! Some ideas:

- [ ] Auto-detect XReal Air connect/disconnect via IOKit USB monitoring
- [ ] Frame prediction (extrapolate 1 frame ahead to reduce perceived latency)
- [ ] CGDisplayStream fallback for macOS 13 ScreenCaptureKit compatibility
- [ ] Custom canvas size input for spatial mode
- [ ] Calibration UI for IMU sensor offsets
- [ ] Window management helpers (snap to left/right halves)
- [ ] DMG background image

## References

- [xrealair-sdk-macos](https://github.com/adidoes/xrealair-sdk-macos) — C driver for XReal Air IMU on macOS
- [Fusion](https://github.com/xioTechnologies/Fusion) — Madgwick AHRS sensor fusion library
- [sidecar-portrait-macos](https://github.com/toho-stdio/sidecar-portrait-macos) — ScreenCaptureKit → Metal → external display pattern
- [Breezy Desktop](https://github.com/wheaney/breezy-desktop) — Linux spatial desktop for XR glasses
- [FluffyDisplay](https://github.com/tml1024/FluffyDisplay) — Simple CGVirtualDisplay wrapper
- [BetterDisplay](https://github.com/waydabber/BetterDisplay) — Production virtual display manager
- [macOS Headers — CGVirtualDisplay](https://github.com/w0lfschild/macOS_headers/blob/master/macOS/Frameworks/CoreGraphics/1336/CGVirtualDisplay.h) — Private API headers

## License

[MIT](LICENSE) — do whatever you want with it.
