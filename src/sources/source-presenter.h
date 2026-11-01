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

#include <obs-module.h>
#include "../wgc-capture/shared-capture.h"
#include "../com-bridge/ppt-com-bridge.h"
#include <string>

namespace sources {

void RegisterPresenterSource();

struct PresenterSource {
	obs_source_t *source = nullptr;
	ppt::ComBridge *bridge = nullptr;
	wgc::SharedCapture *capture = nullptr;

	// Next-slide static thumbnail
	gs_texture_t *nextTex = nullptr;
	int nextTexSlide = -1;

	// Private text handles for native OBS font processing
	obs_source_t *notes_source = nullptr;
	obs_source_t *counter_source = nullptr;
	std::wstring shownNotes;
	std::wstring shownCounter;
	uint32_t shownTextColor = 0;
	uint32_t shownWrapWidth = 0;

	std::wstring currentNotes;
	int currentSlide = 0;
	int totalSlides = 0;

	uint32_t outWidth = 1920;
	uint32_t outHeight = 1080;

	struct Layout {
		gs_rect current = {};
		gs_rect next = {};
		gs_rect notes = {};
		gs_rect controls = {};
	} layout;

	enum class LayoutPreset { Classic, TopBottom, NotesFocused, NextDominant, Custom };

	std::string themePreset = "yami";
	LayoutPreset layoutPreset = LayoutPreset::Classic;

	float splitX = 0.65f;
	float splitY = 0.85f;

	uint32_t colBg = 0xFF1A1A1A;     // Yami bg
	uint32_t colPanel = 0xFF2D2D2D;  // Yami panel
	uint32_t colBorder = 0xFF3D3D3D; // Yami border
	uint32_t colText = 0xFFE0E0E0;   // Yami text

	void ComputeLayout();
	void RenderCurrentSlide();
	void RenderNextSlide();
	void RenderNotes(gs_rect area);
	void RenderControls();
};

} // namespace sources