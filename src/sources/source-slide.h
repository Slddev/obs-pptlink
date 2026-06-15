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

#include <obs-module.h>
#include "../wgc-capture/wgc-capture.h"
#include "../com-bridge/ppt-com-bridge.h"
#include <atomic>

namespace sources {

void RegisterSlideSource();

struct SlideSource {
	obs_source_t *source = nullptr;
	obs_hotkey_id hotkeyNext = OBS_INVALID_HOTKEY_ID;
	obs_hotkey_id hotkeyPrev = OBS_INVALID_HOTKEY_ID;

	wgc::CaptureSession capture;
	ppt::ComBridge *bridge = nullptr;
	gs_texture_t *texture = nullptr;

	bool showBorder = false;
	HWND lastHwnd = nullptr;
	float pollAccum = 0.5f;

	std::atomic<bool> slideshowActive{false};

	void TryStartCapture();
	void RenderFrame(gs_effect_t *effect);
};

} // namespace sources