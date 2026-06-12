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

#include "wgc-capture.h"

#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <shellscalingapi.h>
#include <dwmapi.h>

#include <graphics/graphics.h>

#pragma comment(lib, "windowsapp")
#pragma comment(lib, "d3d11")
#pragma comment(lib, "dxgi")
#pragma comment(lib, "dwmapi")
#pragma comment(lib, "shcore")

namespace wgc {

struct SlidePaneCtx {
	HWND result = nullptr;
	DWORD bestArea = 0;
};

static BOOL CALLBACK FindMdiClassEnum(HWND hwnd, LPARAM lParam)
{
	wchar_t cls[64] = {};
	GetClassNameW(hwnd, cls, 64);
	if (wcscmp(cls, L"mdiClass") != 0)
		return TRUE;
	if (!IsWindowVisible(hwnd))
		return TRUE;

	RECT r = {};
	GetWindowRect(hwnd, &r);
	DWORD area = (DWORD)((r.right - r.left) * (r.bottom - r.top));

	auto *ctx = reinterpret_cast<SlidePaneCtx *>(lParam);
	if (area > ctx->bestArea) {
		ctx->bestArea = area;
		ctx->result = hwnd;
	}
	return TRUE;
}

HWND FindSlidePaneChild(HWND frameHwnd)
{
	SlidePaneCtx ctx;
	EnumChildWindows(frameHwnd, FindMdiClassEnum, reinterpret_cast<LPARAM>(&ctx));
	return ctx.result;
}

static bool IsSlideshowTitle(HWND hwnd)
{
	wchar_t title[512] = {};
	GetWindowTextW(hwnd, title, 512);
	return wcsstr(title, L"PowerPoint Slide Show") != nullptr;
}

static winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice DeviceToWinRT(ID3D11Device *device)
{
	winrt::com_ptr<IDXGIDevice> dxgi;
	winrt::check_hresult(device->QueryInterface(IID_PPV_ARGS(dxgi.put())));

	winrt::com_ptr<::IInspectable> insp;
	winrt::check_hresult(::CreateDirect3D11DeviceFromDXGIDevice(dxgi.get(), insp.put()));

	return insp.as<winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice>();
}

bool CaptureSession::Init(gs_device_t * /*obsDevice*/)
{
	if (!IsWGCSupported()) {
		blog(LOG_ERROR, "[obs-pptlink] WGC not supported on this OS version.");
		return false;
	}

	m_obsDevice = static_cast<ID3D11Device *>(gs_get_device_obj());
	if (!m_obsDevice) {
		blog(LOG_ERROR, "[obs-pptlink] gs_get_device_obj returned null");
		return false;
	}
	m_obsDevice->GetImmediateContext(&m_obsContext);

	try {
		m_wrtDevice = DeviceToWinRT(m_obsDevice);
	} catch (winrt::hresult_error const &e) {
		blog(LOG_ERROR, "[obs-pptlink] DeviceToWinRT failed: %ls", e.message().c_str());
		return false;
	}

	return true;
}

bool CaptureSession::StartCapture(HWND hwnd)
{
	if (!hwnd || !IsWindow(hwnd)) {
		blog(LOG_WARNING, "[obs-pptlink] StartCapture: invalid HWND %p", hwnd);
		return false;
	}
	if (!m_obsDevice) {
		blog(LOG_ERROR, "[obs-pptlink] StartCapture: not initialised");
		return false;
	}

	StopCapture();
	m_freezing.store(false);
	m_hwnd = hwnd;

	m_mdiClient = nullptr;
	{
		wchar_t cls[64] = {};
		GetClassNameW(hwnd, cls, 64);
		if (wcscmp(cls, L"PPTFrameClass") == 0) {
			HWND mdi = FindWindowEx(hwnd, nullptr, L"MDIClient", nullptr);
			if (mdi && IsWindowVisible(mdi))
				m_mdiClient = mdi;
		}
	}

	try {
		auto factory = winrt::get_activation_factory<winrt::Windows::Graphics::Capture::GraphicsCaptureItem,
							     IGraphicsCaptureItemInterop>();

		winrt::Windows::Graphics::Capture::GraphicsCaptureItem item{nullptr};
		winrt::check_hresult(factory->CreateForWindow(
			hwnd, winrt::guid_of<winrt::Windows::Graphics::Capture::GraphicsCaptureItem>(),
			winrt::put_abi(item)));
		m_item = item;

		auto size = m_item.Size();

		{
			RECT mdi = m_mdiClient ? GetMdiClientCropRect(m_hwnd, m_mdiClient) : RECT{};
			if (mdi.right > mdi.left && mdi.bottom > mdi.top) {
				RECT slide = ComputeLetterboxCrop(mdi);
				m_lastWidth = static_cast<uint32_t>(slide.right - slide.left);
				m_lastHeight = static_cast<uint32_t>(slide.bottom - slide.top);
			} else {
				m_lastWidth = static_cast<uint32_t>(size.Width);
				m_lastHeight = static_cast<uint32_t>(size.Height);
			}
		}

		m_pool = winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool::Create(
			m_wrtDevice, winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized, 2,
			size);

		m_frameToken = m_pool.FrameArrived({this, &CaptureSession::OnFrameArrivedInternal});
		m_session = m_pool.CreateCaptureSession(m_item);

		try {
			m_session.IsBorderRequired(false);
		} catch (...) {
		}
		try {
			m_session.IsCursorCaptureEnabled(false);
		} catch (...) {
		}

		m_session.StartCapture();
		m_running.store(true);

		return true;

	} catch (winrt::hresult_error const &e) {
		blog(LOG_ERROR, "[obs-pptlink] StartCapture failed: %ls", e.message().c_str());
		StopCapture();
		return false;
	}
}

void CaptureSession::FreezeLastFrame()
{
	std::lock_guard<std::mutex> lk(m_frameMutex);

	if (!m_renderTex || !m_lastWidth || !m_lastHeight)
		return;

	if (m_frozenTex) {
		gs_texture_destroy(m_frozenTex);
		m_frozenTex = nullptr;
	}
	m_frozenD3DTex = nullptr;

	D3D11_TEXTURE2D_DESC desc = {};
	desc.Width = m_lastWidth;
	desc.Height = m_lastHeight;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

	HRESULT hr = m_obsDevice->CreateTexture2D(&desc, nullptr, m_frozenD3DTex.put());
	if (FAILED(hr)) {
		blog(LOG_WARNING, "[obs-pptlink] FreezeLastFrame: CreateTexture2D failed 0x%08X", hr);
		return;
	}

	m_obsContext->CopyResource(m_frozenD3DTex.get(), m_renderTex.get());

	m_frozenTex = gs_texture_wrap_obj(m_frozenD3DTex.get());
	if (!m_frozenTex) {
		blog(LOG_WARNING, "[obs-pptlink] FreezeLastFrame: gs_texture_wrap_obj failed");
		m_frozenD3DTex = nullptr;
		return;
	}

	m_frozenWidth = m_lastWidth;
	m_frozenHeight = m_lastHeight;
}

void CaptureSession::StopCapture()
{
	m_freezing.store(true);

	if (m_running.load() && m_renderTex) {
		obs_enter_graphics();
		FreezeLastFrame();
		obs_leave_graphics();
	}

	m_running.store(false);
	m_freezing.store(false);

	if (m_pool && m_frameToken.value != 0) {
		m_pool.FrameArrived(m_frameToken);
		m_frameToken = {};
	}
	if (m_session) {
		m_session.Close();
		m_session = nullptr;
	}
	if (m_pool) {
		m_pool.Close();
		m_pool = nullptr;
	}
	m_item = nullptr;

	if (m_obsTexture) {
		obs_enter_graphics();
		gs_texture_destroy(m_obsTexture);
		obs_leave_graphics();
		m_obsTexture = nullptr;
	}
	m_renderTex = nullptr;
	m_hwnd = nullptr;
	m_mdiClient = nullptr;
}

void CaptureSession::Destroy()
{
	StopCapture();
	if (m_frozenTex) {
		obs_enter_graphics();
		gs_texture_destroy(m_frozenTex);
		obs_leave_graphics();
		m_frozenTex = nullptr;
		m_frozenWidth = 0;
		m_frozenHeight = 0;
	}
	m_frozenD3DTex = nullptr;
	m_wrtDevice = nullptr;
	m_obsContext = nullptr;
	m_obsDevice = nullptr;
}

void CaptureSession::SetSlideAspect(float aspect)
{
	if (aspect <= 0.0f)
		return;
	std::lock_guard<std::mutex> lk(m_frameMutex);
	if (aspect == m_slideAspect)
		return;
	m_slideAspect = aspect;
	m_renderTex = nullptr;
	m_textureDirty.store(true);
	m_lastWidth = 0;
	m_lastHeight = 0;
}

RECT CaptureSession::ComputeLetterboxCrop(RECT mdiInFrame) const
{
	LONG mdiW = mdiInFrame.right - mdiInFrame.left;
	LONG mdiH = mdiInFrame.bottom - mdiInFrame.top;
	if (mdiW <= 0 || mdiH <= 0 || m_slideAspect <= 0.0f)
		return mdiInFrame;

	double fitW = static_cast<double>(mdiW);
	double fitH = fitW / static_cast<double>(m_slideAspect);
	if (fitH > static_cast<double>(mdiH)) {
		fitH = static_cast<double>(mdiH);
		fitW = fitH * static_cast<double>(m_slideAspect);
	}

	LONG offX = static_cast<LONG>((mdiW - fitW) / 2.0 + 0.5);
	LONG offY = static_cast<LONG>((mdiH - fitH) / 2.0 + 0.5);

	return RECT{mdiInFrame.left + offX, mdiInFrame.top + offY,
		    mdiInFrame.left + offX + static_cast<LONG>(fitW + 0.5),
		    mdiInFrame.top + offY + static_cast<LONG>(fitH + 0.5)};
}

void CaptureSession::OnFrameArrivedInternal(winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool const &pool,
					    winrt::Windows::Foundation::IInspectable const &)
{
	auto frame = pool.TryGetNextFrame();
	if (!frame)
		return;

	if (m_freezing.load())
		return;

	if (m_mdiClient && !IsSlideshowTitle(m_hwnd))
		return;

	if (ShouldAcceptFrame && !ShouldAcceptFrame())
		return;

	auto contentSize = frame.ContentSize();
	uint32_t fw = static_cast<uint32_t>(contentSize.Width);
	uint32_t fh = static_cast<uint32_t>(contentSize.Height);

	RECT crop = {};
	bool hasCrop = false;

	if (m_mdiClient) {
		RECT mdi = GetMdiClientCropRect(m_hwnd, m_mdiClient);
		if (mdi.right > mdi.left && mdi.bottom > mdi.top) {
			RECT slide;
			{
				std::lock_guard<std::mutex> lk(m_frameMutex);
				slide = ComputeLetterboxCrop(mdi);
			}
			crop.left = (slide.left < (LONG)fw) ? slide.left : (LONG)fw;
			crop.top = (slide.top < (LONG)fh) ? slide.top : (LONG)fh;
			crop.right = (slide.right < (LONG)fw) ? slide.right : (LONG)fw;
			crop.bottom = (slide.bottom < (LONG)fh) ? slide.bottom : (LONG)fh;
			hasCrop = (crop.right > crop.left && crop.bottom > crop.top);
		}
	}

	uint32_t w = hasCrop ? static_cast<uint32_t>(crop.right - crop.left) : fw;
	uint32_t h = hasCrop ? static_cast<uint32_t>(crop.bottom - crop.top) : fh;

	auto surface = frame.Surface();
	auto access = surface.as<Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
	winrt::com_ptr<ID3D11Texture2D> frameTex;
	winrt::check_hresult(access->GetInterface(IID_PPV_ARGS(frameTex.put())));

	bool sizeChanged = false;
	{
		std::lock_guard<std::mutex> lk(m_frameMutex);
		sizeChanged = (w != m_lastWidth || h != m_lastHeight);

		if (sizeChanged || !m_renderTex) {
			D3D11_TEXTURE2D_DESC desc = {};
			desc.Width = w;
			desc.Height = h;
			desc.MipLevels = 1;
			desc.ArraySize = 1;
			desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
			desc.SampleDesc.Count = 1;
			desc.Usage = D3D11_USAGE_DEFAULT;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
			desc.MiscFlags = 0;

			m_renderTex = nullptr;
			m_textureDirty.store(true);

			HRESULT hr = m_obsDevice->CreateTexture2D(&desc, nullptr, m_renderTex.put());
			if (FAILED(hr)) {
				blog(LOG_ERROR, "[obs-pptlink] CreateTexture2D failed: 0x%08X", hr);
				return;
			}

			m_lastWidth = w;
			m_lastHeight = h;
		}
	}

	{
		D3D11_BOX box = {};
		box.left = hasCrop ? static_cast<UINT>(crop.left) : 0;
		box.top = hasCrop ? static_cast<UINT>(crop.top) : 0;
		box.right = hasCrop ? static_cast<UINT>(crop.right) : fw;
		box.bottom = hasCrop ? static_cast<UINT>(crop.bottom) : fh;
		box.front = 0;
		box.back = 1;
		m_obsContext->CopySubresourceRegion(m_renderTex.get(), 0, 0, 0, 0, frameTex.get(), 0, &box);
	}

	m_newFrame.store(true);
	if (OnFrameArrived)
		OnFrameArrived();

	if (sizeChanged) {
		winrt::Windows::Graphics::SizeInt32 newSize;
		newSize.Width = static_cast<int32_t>(fw);
		newSize.Height = static_cast<int32_t>(fh);
		m_pool.Recreate(m_wrtDevice,
				winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized, 2,
				newSize);
	}
}

bool CaptureSession::AcquireLatestFrame(gs_texture_t *&texture)
{
	if (!m_running.load()) {
		texture = m_frozenTex;
		return false;
	}

	std::lock_guard<std::mutex> lk(m_frameMutex);

	if (m_textureDirty.exchange(false) && m_obsTexture) {
		gs_texture_destroy(m_obsTexture);
		m_obsTexture = nullptr;
		m_useFallbackCopy = false;
	}

	if (!m_renderTex) {
		texture = nullptr;
		return false;
	}

	if (!m_obsTexture) {
		m_obsTexture = gs_texture_wrap_obj(m_renderTex.get());
		if (!m_obsTexture) {
			blog(LOG_ERROR,
			     "[obs-pptlink] gs_texture_wrap_obj failed -- falling back to gs_texture_create");
			m_obsTexture = gs_texture_create(m_lastWidth, m_lastHeight, GS_BGRA, 1, nullptr, 0);
			if (!m_obsTexture) {
				texture = nullptr;
				return false;
			}
			m_useFallbackCopy = true;
		}
	}

	if (m_useFallbackCopy) {
		ID3D11Texture2D *dst = static_cast<ID3D11Texture2D *>(gs_texture_get_obj(m_obsTexture));
		if (dst)
			m_obsContext->CopyResource(dst, m_renderTex.get());
	}

	texture = m_obsTexture;
	return texture != nullptr;
}

struct FindSlideshowCtx {
	HWND result = nullptr;
	HWND pptHwnd = nullptr;
};

static BOOL CALLBACK FindSlideshowEnum(HWND hwnd, LPARAM lParam)
{
	auto *ctx = reinterpret_cast<FindSlideshowCtx *>(lParam);
	if (!IsWindowVisible(hwnd))
		return TRUE;

	wchar_t cls[256] = {};
	GetClassNameW(hwnd, cls, 256);

	bool isPPT = wcscmp(cls, L"screenClass") == 0 || wcscmp(cls, L"PPTFrameClass") == 0;
	if (!isPPT)
		return TRUE;

	if (ctx->pptHwnd) {
		DWORD pid1, pid2;
		GetWindowThreadProcessId(ctx->pptHwnd, &pid1);
		GetWindowThreadProcessId(hwnd, &pid2);
		if (pid1 != pid2)
			return TRUE;
	}

	HWND candidate = nullptr;
	if (wcscmp(cls, L"screenClass") == 0) {
		candidate = hwnd;
	} else if (wcscmp(cls, L"PPTFrameClass") == 0) {
		if (!IsSlideshowTitle(hwnd))
			return TRUE;
		candidate = hwnd;
	}

	if (!candidate)
		return TRUE;

	if (!ctx->result) {
		ctx->result = candidate;
	} else {
		wchar_t existingCls[256] = {};
		GetClassNameW(ctx->result, existingCls, 256);
		bool newScreen = (wcscmp(cls, L"screenClass") == 0);
		bool oldScreen = (wcscmp(existingCls, L"screenClass") == 0);

		if (newScreen && !oldScreen) {
			ctx->result = candidate;
		} else if (newScreen == oldScreen) {
			RECT rN = {}, rO = {};
			GetWindowRect(candidate, &rN);
			GetWindowRect(ctx->result, &rO);
			LONG aN = (rN.right - rN.left) * (rN.bottom - rN.top);
			LONG aO = (rO.right - rO.left) * (rO.bottom - rO.top);
			if (aN > aO)
				ctx->result = candidate;
		}
	}
	return TRUE;
}

HWND FindSlideshowWindow(HWND pptHwnd)
{
	FindSlideshowCtx ctx;
	ctx.pptHwnd = pptHwnd;
	EnumWindows(FindSlideshowEnum, reinterpret_cast<LPARAM>(&ctx));
	return ctx.result;
}

RECT GetMdiClientCropRect(HWND pptFrameHwnd, HWND mdiClient)
{
	if (!pptFrameHwnd || !mdiClient)
		return {};

	RECT cli = {};
	if (!GetClientRect(mdiClient, &cli))
		return {};
	LONG w = cli.right - cli.left;
	LONG h = cli.bottom - cli.top;
	if (w <= 0 || h <= 0)
		return {};

	POINT origin = {0, 0};
	ClientToScreen(mdiClient, &origin);

	RECT dwmFrame = {};
	if (FAILED(DwmGetWindowAttribute(pptFrameHwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &dwmFrame, sizeof(dwmFrame))))
		GetWindowRect(pptFrameHwnd, &dwmFrame);

	LONG ox = origin.x - dwmFrame.left;
	LONG oy = origin.y - dwmFrame.top;
	return RECT{ox, oy, ox + w, oy + h};
}

bool IsWGCSupported()
{
	return winrt::Windows::Graphics::Capture::GraphicsCaptureSession::IsSupported();
}

} // namespace wgc