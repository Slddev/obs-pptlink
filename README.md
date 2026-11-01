# PPTLink

Live PowerPoint capture plugin for OBS Studio on Windows.  
Captures animations, transitions, and embedded video natively using Windows Graphics Capture (WGC). Speaker notes and slide control run through PowerPoint's COM automation API.

---

## Requirements

| | Version |
|---|---|
| Windows | 10 1903 (build 18362) or later |
| OBS Studio | 31.0 or later |
| Microsoft PowerPoint | 2016 or later (Microsoft 365 recommended) |

---

## Installation

1. Download the latest `obs-pptlink-*-windows-x64-Installer.exe` from the [Releases](https://github.com/Slddev/obs-pptlink/releases/latest) page.
2. Close OBS Studio if it is running.
3. Run the installer — it detects your OBS version, places the plugin files in the correct OBS directory and upgrades any previous installation automatically.
4. Start OBS Studio.

Alternatively, download the `.zip` archive and extract it into `C:\ProgramData\obs-studio\plugins`, so the plugin ends up at `C:\ProgramData\obs-studio\plugins\obs-pptlink\obs-pptlink.dll`. On OBS 31 and 32, move `obs-pptlink.dll` and `obs-pptlink.pdb` into `C:\ProgramData\obs-studio\plugins\obs-pptlink\bin\64bit\` instead.

---

## Usage

1. Open PowerPoint and start a slideshow (F5 or **Slide Show → From Beginning**).
2. In OBS, **Add Source** and choose:
   - **PPTLink Slide** — live slideshow feed for your main scene, including animations, transitions, and embedded video.
   - **PPTLink Presenter View** — full presenter layout with current slide, next slide preview, speaker notes, and slide counter.
3. The plugin detects the slideshow window automatically within half a second. No manual configuration needed.

### Docks

Two docks are registered automatically under **View → Docks**:

| Dock | Contents |
|---|---|
| PPTLink: Next Slide | Upcoming slide thumbnail + slide counter |
| PPTLink: Notes & Controls | Next slide speaker notes + Prev/Next buttons |

### Hotkeys

Register under **Settings → Hotkeys**:

| Hotkey | Action |
|---|---|
| PPTLink: First Slide | Jump to slide 1 |
| PPTLink: Next Slide | Advance the slideshow |
| PPTLink: Previous Slide | Go back one slide |
| PPTLink: Last Slide | Jump to the last slide |

---

## Building from Source

### Prerequisites

- Visual Studio 2022 with **Desktop development with C++** and **Windows 11 SDK (10.0.22621.0)**
- CMake 3.28+

### Steps

```powershell
# Configure
cmake --preset windows-x64

# Build
cmake --build --preset windows-x64

# Install to C:\ProgramData\obs-studio\plugins
cmake --install build_x64 --config RelWithDebInfo
```

Or open the folder in Visual Studio 2022 and select the preset from the toolbar.

---

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│  OBS Studio (render thread)                                      │
│                                                                  │
│  ┌──────────────────┐    ┌─────────────────────────────────┐    │
│  │  PPTLink Slide   │    │  PPTLink Presenter View          │    │
│  │  (source-slide)  │    │  (source-presenter)              │    │
│  │  AcquireLatest   │    │  WGC + notes + counter overlay   │    │
│  └────────┬─────────┘    └────────────────┬────────────────┘    │
│           └──────────┬────────────────────┘                      │
│                      ▼                                           │
│           wgc::CaptureSession                                    │
│           (gs_texture_t* via shared NT handle)                   │
└──────────────────────┼──────────────────────────────────────────┘
                       │ DX11 shared texture
┌──────────────────────┼──────────────────────────────────────────┐
│  WGC thread          ▼                                           │
│  GraphicsCaptureSession → Direct3D11CaptureFramePool             │
│    MDIClient crop removes title bar / ribbon / bottom bar        │
│    ComputeLetterboxCrop strips black bars using slide aspect      │
│  ID3D11Texture2D (BGRA) ← CopyResource each frame               │
└──────────────────────────────────────────────────────────────────┘
                       ▲
                       │ HWND + slide info (polled every 100ms)
┌──────────────────────┼──────────────────────────────────────────┐
│  COM STA thread — ppt::ComBridge                                 │
│    CurrentShowPosition, Notes, PageSetup aspect ratio            │
│    SlideShowWindow.HWND                                          │
│    View.Next() / Previous() / GotoSlide()                        │
│    Slide(n+1).Export() → obs_ppt_next.png thumbnail              │
└──────────────────────────────────────────────────────────────────┘
```

---

## Known Limitations

**WGC capture border** — Windows 10 draws a highlight border around the captured window. On Windows 11 this is suppressed automatically. On Windows 10, use full-screen slideshow mode to hide it behind the presentation.

**Multi-GPU (Optimus / hybrid GPU)** — ensure OBS and PowerPoint run on the same GPU: **Windows Settings → Display → Graphics** → set OBS to use the dedicated GPU.

---

## License

GNU General Public License v2.0 or later (GPL-2.0+)