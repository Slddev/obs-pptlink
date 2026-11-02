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

#include "com-dispatch.h"

namespace ppt {

// First visible slide after fromIndex (1-based). Returns the slide (caller releases) and its index, or nullptr and 0.
IDispatch *FindNextVisibleSlide(IDispatch *pSlides, int fromIndex, int total, int &outIndex);

// Plain text of the slide's speaker notes body placeholder
std::wstring GetNotesText(IDispatch *pSlide);

// Slide width / height from the presentation's PageSetup, 0 if unavailable
float ReadSlideAspect(IDispatch *pPres);

// Window handle of a SlideShowWindow
HWND GetShowHwnd(IDispatch *pShowWindow);

} // namespace ppt
