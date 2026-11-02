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

#include "ppt-slides.h"

namespace ppt {

static bool IsSlideHidden(IDispatch *pSlide)
{
	IDispatch *pTrans = DispGetObj(pSlide, L"SlideShowTransition");
	if (!pTrans)
		return false;
	bool hidden = DispGetBool(pTrans, L"Hidden");
	pTrans->Release();
	return hidden;
}

IDispatch *FindNextVisibleSlide(IDispatch *pSlides, int fromIndex, int total, int &outIndex)
{
	outIndex = 0;
	for (int idx = fromIndex + 1; pSlides && idx <= total; ++idx) {
		IDispatch *pSlide = DispGetItem(pSlides, idx);
		if (!pSlide)
			continue;
		if (!IsSlideHidden(pSlide)) {
			outIndex = idx;
			return pSlide;
		}
		pSlide->Release();
	}
	return nullptr;
}

std::wstring GetNotesText(IDispatch *pSlide)
{
	constexpr int ppPlaceholderBody = 2;

	IDispatch *pNP = DispGetObj(pSlide, L"NotesPage");
	if (!pNP)
		return {};
	IDispatch *pSh = DispGetObj(pNP, L"Shapes");
	pNP->Release();
	if (!pSh)
		return {};
	IDispatch *pPh = DispGetObj(pSh, L"Placeholders");
	pSh->Release();
	if (!pPh)
		return {};

	IDispatch *pBody = nullptr;
	int count = DispGetInt(pPh, L"Count");
	for (int i = 1; i <= count && !pBody; ++i) {
		IDispatch *pShape = DispGetItem(pPh, i);
		if (!pShape)
			continue;

		IDispatch *pFmt = DispGetObj(pShape, L"PlaceholderFormat");
		if (pFmt) {
			if (DispGetInt(pFmt, L"Type", -1) == ppPlaceholderBody)
				pBody = pShape;
			pFmt->Release();
		}
		if (pBody != pShape)
			pShape->Release();
	}
	pPh->Release();

	if (!pBody)
		return {};

	IDispatch *pTF = DispGetObj(pBody, L"TextFrame");
	pBody->Release();
	if (!pTF)
		return {};

	IDispatch *pTR = DispGetObj(pTF, L"TextRange");
	pTF->Release();
	if (!pTR)
		return {};

	std::wstring text = DispGetStr(pTR, L"Text");
	pTR->Release();
	return text;
}

float ReadSlideAspect(IDispatch *pPres)
{
	IDispatch *pPS = DispGetObj(pPres, L"PageSetup");
	if (!pPS)
		return 0.0f;
	double w = DispGetDouble(pPS, L"SlideWidth");
	double h = DispGetDouble(pPS, L"SlideHeight");
	pPS->Release();
	return w > 0 && h > 0 ? static_cast<float>(w / h) : 0.0f;
}

HWND GetShowHwnd(IDispatch *pShowWindow)
{
	return reinterpret_cast<HWND>(static_cast<uintptr_t>(DispGetInt64(pShowWindow, L"HWND")));
}

} // namespace ppt
