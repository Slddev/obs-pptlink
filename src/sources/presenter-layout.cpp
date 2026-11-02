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

#include "presenter-layout.h"

namespace sources {

PresenterLayout ComputePresenterLayout(LayoutPreset preset, uint32_t width, uint32_t height, float splitX, float splitY,
				       int barHeight)
{
	PresenterLayout layout;
	int W = (int)width;
	int H = (int)height;

	layout.controls = {0, H - barHeight, W, barHeight};

	int availableH = H - barHeight;

	if (preset == LayoutPreset::Classic) {
		int notesH = (int)(availableH * 0.15f);
		int slidesH = availableH - notesH;
		int leftW = (int)(W * 0.65f);

		layout.current = {0, 0, leftW, slidesH};
		layout.next = {leftW, 0, W - leftW, slidesH};
		layout.notes = {0, slidesH, W, notesH};

	} else if (preset == LayoutPreset::TopBottom) {
		int notesH = (int)(availableH * 0.30f);
		int remainingH = availableH - notesH;
		int currentH = (int)(remainingH * 0.60f);
		int nextH = remainingH - currentH;

		layout.current = {0, 0, W, currentH};
		layout.next = {0, currentH, W, nextH};
		layout.notes = {0, remainingH, W, notesH};

	} else if (preset == LayoutPreset::NotesFocused) {
		int notesH = (int)(availableH * 0.60f);
		int slidesH = availableH - notesH;
		int halfW = W / 2;

		layout.current = {0, 0, halfW, slidesH};
		layout.next = {halfW, 0, W - halfW, slidesH};
		layout.notes = {0, slidesH, W, notesH};

	} else if (preset == LayoutPreset::NextDominant) {
		int notesH = (int)(availableH * 0.15f);
		int slidesH = availableH - notesH;
		int leftW = (int)(W * 0.35f);

		layout.current = {0, 0, leftW, slidesH};
		layout.next = {leftW, 0, W - leftW, slidesH};
		layout.notes = {0, slidesH, W, notesH};

	} else {
		int notesH = (int)(availableH * (1.0f - splitY));
		int slidesH = availableH - notesH;
		int leftW = (int)(W * splitX);

		layout.current = {0, 0, leftW, slidesH};
		layout.next = {leftW, 0, W - leftW, slidesH};
		layout.notes = {0, slidesH, W, notesH};
	}

	return layout;
}

} // namespace sources
