/*
PPTLink
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

#include "shared-capture.h"
#include <plugin-support.h>

namespace wgc {

// The bridge reports SlideShowWindow.HWND, which can be a child of the window that can be captured
static HWND CapturableWindow(HWND hwnd)
{
	HWND root = hwnd ? GetAncestor(hwnd, GA_ROOT) : nullptr;
	return IsSlideshowWindow(root) ? root : nullptr;
}

// Window titles show the presentation name, with or without its extension
static std::wstring TitleStem(const std::wstring &name)
{
	size_t dot = name.find_last_of(L'.');
	return dot == std::wstring::npos || dot == 0 ? name : name.substr(0, dot);
}

SharedCapture::SharedCapture(ppt::ComBridge *bridge) : m_bridge(bridge) {}

SharedCapture::~SharedCapture()
{
	Stop();
}

void SharedCapture::Start()
{
	if (m_started)
		return;
	obs_add_tick_callback(TickThunk, this);
	m_started = true;
}

void SharedCapture::Stop()
{
	if (!m_started)
		return;
	obs_remove_tick_callback(TickThunk, this);
	m_started = false;
	Shutdown();
}

void SharedCapture::AddUser()
{
	m_users.fetch_add(1);
}

void SharedCapture::RemoveUser()
{
	m_users.fetch_sub(1);
}

bool SharedCapture::AcquireFrame(gs_texture_t *&texture)
{
	if (!m_initialized) {
		texture = nullptr;
		return false;
	}
	return m_session.AcquireLatestFrame(texture);
}

bool SharedCapture::AcquireOutputFrame(gs_texture_t *&texture)
{
	if (m_frozenTex) {
		texture = m_frozenTex;
		return true;
	}
	return AcquireFrame(texture);
}

uint32_t SharedCapture::OutputWidth() const
{
	uint32_t w = m_frozenWidth.load();
	return w ? w : Width();
}

uint32_t SharedCapture::OutputHeight() const
{
	uint32_t h = m_frozenHeight.load();
	return h ? h : Height();
}

void SharedCapture::UpdateFrozenFrame()
{
	if (!m_frozen.load()) {
		ReleaseFrozenFrame();
		return;
	}
	if (m_frozenTex || !m_initialized)
		return;

	obs_enter_graphics();
	gs_texture_t *live = nullptr;
	m_session.AcquireLatestFrame(live);
	if (live) {
		uint32_t w = gs_texture_get_width(live);
		uint32_t h = gs_texture_get_height(live);
		m_frozenTex = gs_texture_create(w, h, gs_texture_get_color_format(live), 1, nullptr, 0);
		if (m_frozenTex) {
			gs_copy_texture(m_frozenTex, live);
			m_frozenWidth.store(w);
			m_frozenHeight.store(h);
		}
	}
	obs_leave_graphics();
}

void SharedCapture::ReleaseFrozenFrame()
{
	if (!m_frozenTex)
		return;
	obs_enter_graphics();
	gs_texture_destroy(m_frozenTex);
	obs_leave_graphics();
	m_frozenTex = nullptr;
	m_frozenWidth.store(0);
	m_frozenHeight.store(0);
}

void SharedCapture::TickThunk(void *param, float seconds)
{
	static_cast<SharedCapture *>(param)->Tick(seconds);
}

void SharedCapture::Shutdown()
{
	ReleaseFrozenFrame();
	if (!m_initialized)
		return;
	m_session.Destroy();
	m_initialized = false;
	m_lastHwnd = nullptr;
	m_slideshowActive.store(false);
}

void SharedCapture::Tick(float seconds)
{
	if (m_users.load() <= 0) {
		Shutdown();
		return;
	}

	if (!m_initialized) {
		if (m_initFailed)
			return;

		obs_enter_graphics();
		m_initialized = m_session.Init(nullptr);
		obs_leave_graphics();

		if (!m_initialized) {
			m_initFailed = true;
			obs_log(LOG_ERROR, "SharedCapture: WGC init failed");
			return;
		}
		m_session.ShouldAcceptFrame = [this]() {
			return m_slideshowActive.load() && !(m_bridge && m_bridge->IsHoldingFrames());
		};
		m_pollAccum = 0.5f;
	}

	UpdateFrozenFrame();

	bool connected = m_bridge && m_bridge->IsConnected();
	if (connected != m_bridgeConnected) {
		m_bridgeConnected = connected;
		if (!connected)
			m_slideshowActive.store(false);
	}

	if (m_session.IsRunning() && !m_slideshowActive.load()) {
		m_lastHwnd = nullptr;
		m_session.StopCapture();
	}

	if (connected)
		m_session.SetSlideAspect(m_bridge->GetSlideAspect());

	HWND hwnd = connected ? CapturableWindow(m_bridge->GetSlideshowHwnd()) : nullptr;
	if (!hwnd) {
		bool picked = m_bridge && m_bridge->HasPreferredShow();
		std::wstring title = picked && connected ? TitleStem(m_bridge->GetSlideInfo().presentation) : L"";
		if (!picked || !title.empty()) {
			m_pollAccum += seconds;
			if (m_pollAccum < 0.5f)
				return;
			m_pollAccum = 0.0f;
			hwnd = FindSlideshowWindow(nullptr, title.empty() ? nullptr : title.c_str());
		}
	}

	if (hwnd != m_lastHwnd) {
		m_lastHwnd = hwnd;
		if (hwnd) {
			wchar_t cls[64] = {};
			GetClassNameW(hwnd, cls, 64);
			obs_log(LOG_INFO, "SharedCapture: capturing %ls window %p", cls, hwnd);
			m_slideshowActive.store(true);
			m_session.StartCapture(hwnd);
		} else {
			m_session.StopCapture();
		}
	}
}

} // namespace wgc
