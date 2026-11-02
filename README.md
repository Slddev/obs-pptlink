# PPTLink

Live PowerPoint capture plugin for OBS Studio on Windows.  
Captures animations, transitions, and embedded video natively using Windows Graphics Capture (WGC). Speaker notes and slide control run through PowerPoint's COM automation API.

---

## Requirements

| | Version |
|---|---|
| Windows | 10 1903 (build 18362) or later |
| OBS Studio | 31.1.1 or later |
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
   - **PPTLink Presenter View** — full presenter layout with current slide, next slide preview, speaker notes, slide counter, and progress bar. It follows the OBS theme by default; fixed colour presets and custom colours are available in its properties.
3. The plugin detects the slideshow window automatically within half a second. No manual configuration needed.

### Docks

Two docks are registered automatically under **View → Docks**:

| Dock | Contents |
|---|---|
| PPTLink: Next Slide | Upcoming slide thumbnail + slide counter |
| PPTLink: Notes & Controls | Current slide speaker notes, First/Prev/Next/Last buttons, go to slide, black/white screen and freeze toggles, and a presentation picker when several slideshows are running |

### Hotkeys

Register under **Settings → Hotkeys**:

| Hotkey | Action |
|---|---|
| PPTLink: First Slide | Jump to slide 1 |
| PPTLink: Next Slide | Advance the slideshow |
| PPTLink: Previous Slide | Go back one slide |
| PPTLink: Last Slide | Jump to the last slide |
| PPTLink: Toggle Black Screen | Black out the slideshow, or return to the slide |
| PPTLink: Toggle White Screen | White out the slideshow, or return to the slide |
| PPTLink: Toggle Slide Freeze | Hold the Slide source on the current frame while the presenter view stays live |

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

### Tests

Unit tests cover the logic that runs without PowerPoint or OBS (presenter layout, thumbnail decoding).

```powershell
cmake --preset windows-x64 -DENABLE_TESTS=ON
cmake --build --preset windows-x64
ctest --test-dir build_x64 -C RelWithDebInfo --output-on-failure
```

---

## Architecture

```
┌──────────────────────────────────────────────────────────────────────┐
│ COM STA thread -- ppt::ComBridge                                     │
│                                                                      │
│   EApplication events: SlideShowNextSlide, SlideShowEnd              │
│     + state poll every 1 s (every 100 ms if events are unavailable)  │
│   CurrentShowPosition, notes, PageSetup aspect, SlideShowWindow.HWND │
│   View.Next() / Previous() / GotoSlide() from docks and hotkeys      │
│                                                                      │
│   ppt::ThumbCache                                                    │
│     Slide.Export() -> %TEMP%\obs-pptlink-<pid>\slide_<n>.png         │
│     decoded once with GDI+, shared via SlideInfo::nextThumb          │
│     slide after next is exported and decoded ahead of time           │
└──────────────────────────────────────────────────────────────────────┘
                    │
                    │ GetSlideInfo() -- HWND, slide numbers, notes, next thumbnail
                    ▼
┌──────────────────────────────────────────────────────────────────────┐
│ OBS graphics thread                                                  │
│                                                                      │
│   PPTLink Slide (source-slide)                                       │
│   PPTLink Presenter View (source-presenter)                          │
│     live slide, next-slide thumbnail, notes, counter, progress       │
│                                                                      │
│   wgc::SharedCapture -- one capture shared by every source           │
│     tick: picks up the slideshow HWND, starts/stops the session      │
└──────────────────────────────────────────────────────────────────────┘
                    ▲
                    │ AcquireFrame() -- ID3D11Texture2D wrapped as gs_texture_t
                    │
┌──────────────────────────────────────────────────────────────────────┐
│ WGC thread pool -- wgc::CaptureSession                               │
│                                                                      │
│   Direct3D11CaptureFramePool (free-threaded, OBS's D3D11 device)     │
│   MDIClient crop removes title bar / ribbon / status bar             │
│   ComputeLetterboxCrop strips black bars using the slide aspect      │
│   CopySubresourceRegion into a BGRA texture under the graphics lock  │
└──────────────────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────────────────┐
│ Qt UI thread                                                         │
│                                                                      │
│   Next Slide / Notes & Controls docks -- GetSlideInfo() every 200 ms │
│   theme::Colors -- Qt palette of the active OBS theme, refreshed on  │
│     theme change, read by the presenter view                         │
└──────────────────────────────────────────────────────────────────────┘
```

---

## Known Limitations

**WGC capture border** — Windows 10 draws a highlight border around the captured window. On Windows 11 this is suppressed automatically. On Windows 10, use full-screen slideshow mode to hide it behind the presentation.

**Multi-GPU (Optimus / hybrid GPU)** — ensure OBS and PowerPoint run on the same GPU: **Windows Settings → Display → Graphics** → set OBS to use the dedicated GPU.

---

## License

GNU General Public License v2.0 or later (GPL-2.0+)