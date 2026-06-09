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

#include "wgc-frame-pool.h"

namespace wgc {

bool FramePoolManager::HandleSizeChange(winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool &pool,
					winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice const &device,
					int32_t newW, int32_t newH)
{
	if (static_cast<uint32_t>(newW) == m_w && static_cast<uint32_t>(newH) == m_h)
		return false;

	m_w = static_cast<uint32_t>(newW);
	m_h = static_cast<uint32_t>(newH);

	winrt::Windows::Graphics::SizeInt32 sz;
	sz.Width = newW;
	sz.Height = newH;

	pool.Recreate(device, winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, sz);

	return true;
}

} // namespace wgc