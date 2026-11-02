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

#pragma once

#include "wgc-capture.h"
#include "../com-bridge/ppt-com-bridge.h"

#include <atomic>

namespace wgc {

// Single slideshow capture shared by every PPTLink source.
// Driven from an OBS tick callback, so Tick() and the sources' render
// calls both run on the graphics thread.
class SharedCapture {
public:
	explicit SharedCapture(ppt::ComBridge *bridge);
	~SharedCapture();

	SharedCapture(const SharedCapture &) = delete;
	SharedCapture &operator=(const SharedCapture &) = delete;

	void Start();
	void Stop();

	void AddUser();
	void RemoveUser();

	// Returns true only while capturing live; otherwise texture is the
	// frozen last frame (or null).
	bool AcquireFrame(gs_texture_t *&texture);
	uint32_t Width() const { return m_session.Width(); }
	uint32_t Height() const { return m_session.Height(); }

	// Freeze holds the Slide sources on the current frame while the presenter view stays live.
	// The snapshot is taken on the next tick.
	void SetFrozen(bool frozen) { m_frozen.store(frozen); }
	bool IsFrozen() const { return m_frozen.load(); }
	// Frozen snapshot while frozen, otherwise the same as AcquireFrame
	bool AcquireOutputFrame(gs_texture_t *&texture);
	uint32_t OutputWidth() const;
	uint32_t OutputHeight() const;

private:
	static void TickThunk(void *param, float seconds);
	void Tick(float seconds);
	void Shutdown();
	void UpdateFrozenFrame();
	void ReleaseFrozenFrame();

	ppt::ComBridge *m_bridge = nullptr;
	CaptureSession m_session;
	std::atomic<int> m_users{0};
	std::atomic<bool> m_slideshowActive{false};

	std::atomic<bool> m_frozen{false};
	gs_texture_t *m_frozenTex = nullptr;
	std::atomic<uint32_t> m_frozenWidth{0};
	std::atomic<uint32_t> m_frozenHeight{0};

	bool m_started = false;
	bool m_initialized = false;
	bool m_initFailed = false;
	bool m_bridgeConnected = false;
	HWND m_lastHwnd = nullptr;
	float m_pollAccum = 0.5f;
};

} // namespace wgc
