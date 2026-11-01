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

void SharedCapture::TickThunk(void *param, float seconds)
{
	static_cast<SharedCapture *>(param)->Tick(seconds);
}

void SharedCapture::Shutdown()
{
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
			return m_slideshowActive.load();
		};
		m_pollAccum = 0.5f;
	}

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

	HWND hwnd = connected ? m_bridge->GetSlideshowHwnd() : nullptr;
	if (!hwnd) {
		m_pollAccum += seconds;
		if (m_pollAccum < 0.5f)
			return;
		m_pollAccum = 0.0f;
		hwnd = FindSlideshowWindow(nullptr);
	}

	if (hwnd != m_lastHwnd) {
		m_lastHwnd = hwnd;
		if (hwnd) {
			m_slideshowActive.store(true);
			m_session.StartCapture(hwnd);
		} else {
			m_session.StopCapture();
		}
	}
}

} // namespace wgc
