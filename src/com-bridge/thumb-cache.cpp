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

#include "thumb-cache.h"
#include "com-dispatch.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <oleauto.h>
#include <cstring>
#include <gdiplus.h>

namespace {

std::wstring GetThumbDir()
{
	wchar_t tempPath[MAX_PATH];
	GetTempPathW(MAX_PATH, tempPath);
	return std::wstring(tempPath) + L"obs-pptlink-" + std::to_wstring(GetCurrentProcessId());
}

std::wstring GetSlideThumbPath(int slide)
{
	return GetThumbDir() + L"\\slide_" + std::to_wstring(slide) + L".png";
}

HRESULT DispCallExport(IDispatch *pSlide, const wchar_t *path, int width, int height)
{
	VARIANT args[4];
	for (VARIANT &arg : args)
		VariantInit(&arg);

	args[0].vt = VT_I4;
	args[0].lVal = height;
	args[1].vt = VT_I4;
	args[1].lVal = width;
	args[2].vt = VT_BSTR;
	args[2].bstrVal = SysAllocString(L"PNG");
	args[3].vt = VT_BSTR;
	args[3].bstrVal = SysAllocString(path);

	HRESULT hr = ppt::DispInvoke(pSlide, L"Export", DISPATCH_METHOD, args, 4, nullptr);

	SysFreeString(args[2].bstrVal);
	SysFreeString(args[3].bstrVal);
	return hr;
}

} // namespace

namespace ppt {

std::shared_ptr<const SlideThumb> DecodeThumb(const std::wstring &path)
{
	Gdiplus::Bitmap bmp(path.c_str());
	if (bmp.GetLastStatus() != Gdiplus::Ok)
		return nullptr;

	auto thumb = std::make_shared<SlideThumb>();
	thumb->width = bmp.GetWidth();
	thumb->height = bmp.GetHeight();
	thumb->pixels.resize(thumb->width * thumb->height);

	Gdiplus::Rect rect(0, 0, thumb->width, thumb->height);
	Gdiplus::BitmapData data;
	if (bmp.LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &data) != Gdiplus::Ok)
		return nullptr;
	for (uint32_t y = 0; y < thumb->height; ++y)
		std::memcpy(&thumb->pixels[y * thumb->width], static_cast<uint8_t *>(data.Scan0) + y * data.Stride,
			    thumb->width * 4);
	bmp.UnlockBits(&data);
	return thumb;
}

bool ThumbCache::IsCached(int index) const
{
	return index < static_cast<int>(m_cached.size()) && m_cached[index] &&
	       GetFileAttributesW(GetSlideThumbPath(index).c_str()) != INVALID_FILE_ATTRIBUTES;
}

bool ThumbCache::Export(IDispatch *pSlide, int index, float aspect)
{
	if (!pSlide || index < 1)
		return false;
	if (IsCached(index))
		return true;

	CreateDirectoryW(GetThumbDir().c_str(), nullptr);

	constexpr int thumbWidth = 1920;
	int thumbHeight = static_cast<int>(thumbWidth / aspect + 0.5f);
	if (FAILED(DispCallExport(pSlide, GetSlideThumbPath(index).c_str(), thumbWidth, thumbHeight)))
		return false;

	if (index >= static_cast<int>(m_cached.size()))
		m_cached.resize(index + 1, false);
	m_cached[index] = true;
	return true;
}

void ThumbCache::Prefetch(int index)
{
	m_decoded = DecodeThumb(GetSlideThumbPath(index));
	m_decodedSlide = m_decoded ? index : 0;
}

std::shared_ptr<const SlideThumb> ThumbCache::Load(int index)
{
	if (index == m_decodedSlide)
		return m_decoded;
	return DecodeThumb(GetSlideThumbPath(index));
}

void ThumbCache::Clear()
{
	for (size_t i = 1; i < m_cached.size(); ++i) {
		if (m_cached[i])
			DeleteFileW(GetSlideThumbPath(static_cast<int>(i)).c_str());
	}
	m_cached.clear();
	m_decoded.reset();
	m_decodedSlide = 0;
	RemoveDirectoryW(GetThumbDir().c_str());
}

} // namespace ppt
