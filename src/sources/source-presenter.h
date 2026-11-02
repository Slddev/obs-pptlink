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
#include "presenter-layout.h"
#include <memory>
#include <string>

namespace sources {

void RegisterPresenterSource();

struct PresenterSource {
	obs_source_t *source = nullptr;
	ppt::ComBridge *bridge = nullptr;
	wgc::SharedCapture *capture = nullptr;

	// Next-slide static thumbnail
	gs_texture_t *nextTex = nullptr;
	std::shared_ptr<const ppt::SlideThumb> nextThumb;

	// Private text source, only updated when its content changes
	struct Text {
		obs_source_t *source = nullptr;
		std::wstring text;
		uint32_t color = 0;
		uint32_t size = 0;
		bool bold = false;
		uint32_t cx = 0;
		uint32_t cy = 0;

		void Set(const std::wstring &text, uint32_t color, uint32_t size, bool bold, uint32_t cx = 0,
			 uint32_t cy = 0);
		void Draw(float x, float y) const;
		uint32_t Width() const;
		uint32_t Height() const;
	};

	Text notes;
	Text counter;
	Text labelCurrent;
	Text labelNext;
	Text labelNotes;
	Text statusCurrent;
	Text statusNext;

	std::wstring currentNotes;
	int currentSlide = 0;
	int totalSlides = 0;
	int nextSlide = 0;
	float slideAspect = 16.0f / 9.0f;

	uint32_t outWidth = 1920;
	uint32_t outHeight = 1080;
	float scale = 1.0f;

	PresenterLayout layout;

	bool followTheme = true;
	LayoutPreset layoutPreset = LayoutPreset::Classic;

	float splitX = 0.65f;
	float splitY = 0.85f;

	// 0xAARRGGBB
	uint32_t colBg = 0xFF1D1F26;
	uint32_t colPanel = 0xFF272A33;
	uint32_t colText = 0xFFFEFEFE;
	uint32_t colAccent = 0xFF284CB8;

	void ComputeLayout();
	int Px(float value) const;
	LayoutRect PanelBody(const LayoutRect &panel) const;
	void RenderCard(const LayoutRect &panel, const Text &label);
	void RenderSlide(const LayoutRect &panel, const Text &label, gs_texture_t *tex, const Text *status);
	void RenderNotes();
	void RenderControls();
};

} // namespace sources
