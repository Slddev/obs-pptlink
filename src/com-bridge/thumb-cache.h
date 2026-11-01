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
#include <memory>
#include <vector>

struct IDispatch;

namespace ppt {

// Decoded slide thumbnail, 32-bit BGRA pixels
struct SlideThumb {
	uint32_t width = 0;
	uint32_t height = 0;
	std::vector<uint32_t> pixels;
};

// ------------------------------------------------------------------
// ThumbCache -- slide PNGs exported to %TEMP% plus one decoded slot
// for the prefetched slide. Only touched on the bridge's STA thread.
// ------------------------------------------------------------------
class ThumbCache {
public:
	bool IsCached(int index) const;
	bool Export(IDispatch *pSlide, int index, float aspect);
	bool IsDecoded(int index) const { return index == m_decodedSlide; }
	void Prefetch(int index);
	std::shared_ptr<const SlideThumb> Load(int index);
	void Clear();

private:
	std::vector<bool> m_cached;
	std::shared_ptr<const SlideThumb> m_decoded;
	int m_decodedSlide = 0;
};

} // namespace ppt
