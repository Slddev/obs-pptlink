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
	void FreezeLastFrame();
	void SetSlideAspect(float aspect);

	bool IsRunning() const { return m_running.load(); }
	HWND GetHwnd() const { return m_hwnd; }
	uint32_t Width() const
	{
		return m_running.load() ? m_lastWidth : (m_frozenWidth ? m_frozenWidth : m_lastWidth);
	}
	uint32_t Height() const
	{
		return m_running.load() ? m_lastHeight : (m_frozenHeight ? m_frozenHeight : m_lastHeight);
	}

	std::function<void()> OnFrameArrived;
	std::function<bool()> ShouldAcceptFrame;

private:
	void OnFrameArrivedInternal(winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool const &,
				    winrt::Windows::Foundation::IInspectable const &);

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

	gs_texture_t *m_frozenTex = nullptr;
	winrt::com_ptr<ID3D11Texture2D> m_frozenD3DTex;
	uint32_t m_frozenWidth = 0;
	uint32_t m_frozenHeight = 0;

	HWND m_hwnd = nullptr;
	HWND m_mdiClient = nullptr;
	float m_slideAspect = 16.0f / 9.0f;

	std::atomic<bool> m_running{false};
	std::atomic<bool> m_freezing{false};
	std::atomic<bool> m_newFrame{false};
	std::atomic<bool> m_textureDirty{false};
	bool m_useFallbackCopy = false;
	uint32_t m_lastWidth = 0;
	uint32_t m_lastHeight = 0;
	std::mutex m_frameMutex;
};

HWND FindSlidePaneChild(HWND frameHwnd);
HWND FindSlideshowWindow(HWND pptHwnd = nullptr);
RECT GetMdiClientCropRect(HWND pptFrameHwnd, HWND mdiClient);
bool IsWGCSupported();

} // namespace wgc