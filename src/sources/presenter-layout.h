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

#include <cstdint>

namespace sources {

struct LayoutRect {
	int x = 0;
	int y = 0;
	int cx = 0;
	int cy = 0;
};

enum class LayoutPreset { Classic, TopBottom, NotesFocused, NextDominant, Custom };

struct PresenterLayout {
	LayoutRect current;
	LayoutRect next;
	LayoutRect notes;
	LayoutRect controls;
};

PresenterLayout ComputePresenterLayout(LayoutPreset preset, uint32_t width, uint32_t height, float splitX, float splitY,
				       int barHeight);

} // namespace sources
