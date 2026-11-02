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

#ifndef NOMINMAX
#define NOMINMAX
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <oleauto.h>
#include <string>

namespace ppt {

// Late-bound IDispatch helpers. args are in DISPPARAMS order (last argument first).
HRESULT DispInvoke(IDispatch *pDisp, const wchar_t *name, WORD flags, VARIANT *args, UINT argCount, VARIANT *pResult);
HRESULT DispGetProp(IDispatch *pDisp, const wchar_t *name, VARIANT *pVar);
HRESULT DispCall(IDispatch *pDisp, const wchar_t *name);
HRESULT DispCallInt(IDispatch *pDisp, const wchar_t *name, int value);

// Returned objects are owned by the caller
IDispatch *DispGetObj(IDispatch *pDisp, const wchar_t *name);
IDispatch *DispGetItem(IDispatch *pCollection, int index);

int DispGetInt(IDispatch *pDisp, const wchar_t *name, int def = 0);
long long DispGetInt64(IDispatch *pDisp, const wchar_t *name, long long def = 0);
double DispGetDouble(IDispatch *pDisp, const wchar_t *name, double def = 0.0);
bool DispGetBool(IDispatch *pDisp, const wchar_t *name, bool def = false);
std::wstring DispGetStr(IDispatch *pDisp, const wchar_t *name);

} // namespace ppt
