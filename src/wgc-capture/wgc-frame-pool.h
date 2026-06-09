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

#include "wgc-capture.h"
#include <cstdint>

namespace wgc {

// FramePoolManager watches for size changes reported by WGC
// and calls pool.Recreate() on the correct thread.
// This is separated from CaptureSession so it can be unit-tested
// and swapped out without touching the core capture logic.
class FramePoolManager {
public:
	// Call inside FrameArrived handler after getting frameSize.
	// Returns true if the pool was recreated.
	bool HandleSizeChange(winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool &pool,
			      winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice const &device,
			      int32_t newW, int32_t newH);

	uint32_t LastWidth() const { return m_w; }
	uint32_t LastHeight() const { return m_h; }

private:
	uint32_t m_w = 0;
	uint32_t m_h = 0;
};

} // namespace wgc