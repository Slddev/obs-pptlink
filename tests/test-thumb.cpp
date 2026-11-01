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

#include "test.h"
#include "com-bridge/thumb-cache.h"

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <string>

namespace {

bool GetPngEncoder(CLSID &clsid)
{
	UINT count = 0, size = 0;
	Gdiplus::GetImageEncodersSize(&count, &size);
	if (!size)
		return false;

	std::vector<uint8_t> buffer(size);
	auto *codecs = reinterpret_cast<Gdiplus::ImageCodecInfo *>(buffer.data());
	Gdiplus::GetImageEncoders(count, size, codecs);
	for (UINT i = 0; i < count; ++i) {
		if (wcscmp(codecs[i].MimeType, L"image/png") == 0) {
			clsid = codecs[i].Clsid;
			return true;
		}
	}
	return false;
}

std::wstring TempPngPath(const wchar_t *name)
{
	wchar_t dir[MAX_PATH];
	GetTempPathW(MAX_PATH, dir);
	return std::wstring(dir) + name;
}

} // namespace

TEST_CASE(thumb_decode_returns_bgra_pixels)
{
	const uint32_t colors[] = {0xFFFF0000, 0xFF00FF00, 0xFF0000FF, 0xFFFFFFFF, 0xFF000000, 0xFF123456};
	const uint32_t width = 3, height = 2;

	std::wstring path = TempPngPath(L"pptlink-test-decode.png");
	{
		Gdiplus::Bitmap bmp(width, height, PixelFormat32bppARGB);
		for (uint32_t y = 0; y < height; ++y)
			for (uint32_t x = 0; x < width; ++x)
				bmp.SetPixel(x, y, Gdiplus::Color(colors[y * width + x]));

		CLSID png;
		CHECK(GetPngEncoder(png));
		CHECK(bmp.Save(path.c_str(), &png) == Gdiplus::Ok);
	}

	auto thumb = ppt::DecodeThumb(path);
	DeleteFileW(path.c_str());

	CHECK(thumb != nullptr);
	if (!thumb)
		return;
	CHECK(thumb->width == width);
	CHECK(thumb->height == height);
	CHECK(thumb->pixels.size() == width * height);
	for (uint32_t i = 0; i < width * height && i < thumb->pixels.size(); ++i)
		CHECK(thumb->pixels[i] == colors[i]);
}

TEST_CASE(thumb_decode_missing_file_returns_null)
{
	CHECK(ppt::DecodeThumb(TempPngPath(L"pptlink-test-missing.png")) == nullptr);
}
