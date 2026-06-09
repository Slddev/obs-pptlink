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

namespace sources {

// Called once by plugin-main to register the source type with OBS.
void RegisterSlideSource();

// ------------------------------------------------------------------
//  SlideSource — internal context struct
// ------------------------------------------------------------------
struct SlideSource {
	// OBS handles
	obs_source_t *source = nullptr;
	obs_hotkey_id hotkeyNext = OBS_INVALID_HOTKEY_ID;
	obs_hotkey_id hotkeyPrev = OBS_INVALID_HOTKEY_ID;

	// WGC capture session (lives for the lifetime of this source)
	wgc::CaptureSession capture;

	// COM bridge (shared across sources — pointer into the global instance)
	ppt::ComBridge *bridge = nullptr;

	// Current slide texture (owned by capture session)
	gs_texture_t *texture = nullptr;

	// Settings
	bool showBorder = false; // draw a border around the slide

	// Last known slideshow HWND (used to detect window changes)
	HWND lastHwnd = nullptr;

	// Polling accumulator — we only call FindSlideshowWindow at ~2Hz
	float pollAccum = 0.5f; // start at 0.5 so first tick fires immediately

	// Helpers
	void TryStartCapture();
	void RenderFrame(gs_effect_t *effect);
};

} // namespace sources