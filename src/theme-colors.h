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

namespace theme {

// Colours of the active OBS theme as 0xAARRGGBB, read from the Qt palette
struct Colors {
	uint32_t bg = 0xFF1D1F26;
	uint32_t panel = 0xFF272A33;
	uint32_t text = 0xFFFEFEFE;
	uint32_t accent = 0xFF284CB8;
};

// Init and Shutdown must be called on the UI thread
void Init();
void Shutdown();

// Thread-safe
Colors Get();

} // namespace theme
