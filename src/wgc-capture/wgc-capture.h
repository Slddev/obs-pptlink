/*
obs-pptlink
Copyright (C) 2026 Slddev me@sappy.eu.org

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#pragma once

#include <functional>
#include <mutex>
#include <atomic>
#include <cstdint>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <obs.h>
#include <graphics/graphics.h>

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>

#include <d3d11.h>
#include <dxgi.h>

namespace wgc {

class CaptureSession {
public:
	CaptureSession() = default;
	~CaptureSession() { Destroy(); }

	CaptureSession(const CaptureSession &) = delete;
	CaptureSession &operator=(const CaptureSession &) = delete;

	bool Init(gs_device_t *obsDevice);
	bool StartCapture(HWND hwnd);
	void StopCapture();
	void Destroy();
	bool AcquireLatestFrame(gs_texture_t *&texture);

	// Called from source-slide.cpp's tick to feed the real slide aspect ratio
	// (width/height) so ComputeLetterboxCrop strips PPT's black bars correctly.
	// Safe to call from any thread.
	void SetSlideAspect(float aspect);

	bool IsRunning() const { return m_running.load(); }
	HWND GetHwnd() const { return m_hwnd; }
	uint32_t Width() const { return m_lastWidth; }
	uint32_t Height() const { return m_lastHeight; }

	std::function<void()> OnFrameArrived;

private:
	void OnFrameArrivedInternal(winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool const &,
				    winrt::Windows::Foundation::IInspectable const &);

	// Compute the slide-only sub-rect within mdiInFrame (WGC frame coords)
	// by fitting the slide aspect ratio into the MDIClient area.
	// Caller must hold m_frameMutex.
	RECT ComputeLetterboxCrop(RECT mdiInFrame) const;

	winrt::Windows::Graphics::Capture::GraphicsCaptureItem m_item{nullptr};
	winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool m_pool{nullptr};
	winrt::Windows::Graphics::Capture::GraphicsCaptureSession m_session{nullptr};
	winrt::event_token m_frameToken{};

	ID3D11Device *m_obsDevice = nullptr;
	ID3D11DeviceContext *m_obsContext = nullptr;
	winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice m_wrtDevice{nullptr};

	winrt::com_ptr<ID3D11Texture2D> m_renderTex;
	gs_texture_t *m_obsTexture = nullptr;

	HWND m_hwnd = nullptr;

	// PPTFrameClass windowed-slideshow crop:
	//   m_mdiClient — MDIClient child: removes title bar / ribbon / bottom bar.
	//                 Queried live on every frame so resizes are handled without
	//                 restarting the session.
	// The slide is letterboxed inside MDIClient by PPT (black bars are painted
	// directly into the mdiClass window — there is no separate child window for
	// the slide surface).  ComputeLetterboxCrop strips those bars using m_slideAspect.
	HWND m_mdiClient = nullptr;

	// Slide aspect ratio (width/height).  Updated from the COM bridge via
	// SetSlideAspect() each tick.  Protected by m_frameMutex.
	float m_slideAspect = 16.0f / 9.0f;

	std::atomic<bool> m_running{false};
	std::atomic<bool> m_newFrame{false};
	std::atomic<bool> m_textureDirty{false};
	bool m_useFallbackCopy = false;
	uint32_t m_lastWidth = 0;
	uint32_t m_lastHeight = 0;
	std::mutex m_frameMutex;
};

// Window-finding utilities used across compilation units.
HWND FindSlidePaneChild(HWND frameHwnd);
HWND FindSlideshowWindow(HWND pptHwnd = nullptr);

// Returns the MDIClient rect in WGC frame coordinates (DWM-frame-relative
// screen pixels).  Returns {0,0,0,0} on failure.
RECT GetMdiClientCropRect(HWND pptFrameHwnd, HWND mdiClient);

bool IsWGCSupported();

} // namespace wgc