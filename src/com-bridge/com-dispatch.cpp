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

#include "com-dispatch.h"

namespace ppt {

static bool DispGetAs(IDispatch *pDisp, const wchar_t *name, VARTYPE vt, VARIANT *pVar)
{
	if (FAILED(DispGetProp(pDisp, name, pVar)))
		return false;
	if (SUCCEEDED(VariantChangeType(pVar, pVar, 0, vt)) && pVar->vt == vt)
		return true;
	VariantClear(pVar);
	return false;
}

HRESULT DispInvoke(IDispatch *pDisp, const wchar_t *name, WORD flags, VARIANT *args, UINT argCount, VARIANT *pResult)
{
	if (pResult)
		VariantInit(pResult);
	if (!pDisp || !name)
		return E_POINTER;

	DISPID dispid;
	BSTR bname = SysAllocString(name);
	HRESULT hr = pDisp->GetIDsOfNames(IID_NULL, &bname, 1, LOCALE_USER_DEFAULT, &dispid);
	SysFreeString(bname);
	if (FAILED(hr))
		return hr;

	DISPPARAMS dp{args, nullptr, argCount, 0};
	VARIANT ignored;
	VariantInit(&ignored);
	hr = pDisp->Invoke(dispid, IID_NULL, LOCALE_USER_DEFAULT, flags, &dp, pResult ? pResult : &ignored, nullptr,
			   nullptr);
	VariantClear(&ignored);
	return hr;
}

HRESULT DispGetProp(IDispatch *pDisp, const wchar_t *name, VARIANT *pVar)
{
	if (!pVar)
		return E_POINTER;
	return DispInvoke(pDisp, name, DISPATCH_PROPERTYGET, nullptr, 0, pVar);
}

HRESULT DispCall(IDispatch *pDisp, const wchar_t *name)
{
	return DispInvoke(pDisp, name, DISPATCH_METHOD, nullptr, 0, nullptr);
}

HRESULT DispCallInt(IDispatch *pDisp, const wchar_t *name, int value)
{
	VARIANT arg;
	VariantInit(&arg);
	arg.vt = VT_I4;
	arg.lVal = value;
	return DispInvoke(pDisp, name, DISPATCH_METHOD, &arg, 1, nullptr);
}

IDispatch *DispGetObj(IDispatch *pDisp, const wchar_t *name)
{
	VARIANT v;
	if (FAILED(DispGetProp(pDisp, name, &v)))
		return nullptr;
	if (v.vt == VT_DISPATCH)
		return v.pdispVal;
	VariantClear(&v);
	return nullptr;
}

IDispatch *DispGetItem(IDispatch *pCollection, int index)
{
	VARIANT arg;
	VariantInit(&arg);
	arg.vt = VT_I4;
	arg.lVal = index;

	VARIANT res;
	if (FAILED(DispInvoke(pCollection, L"Item", DISPATCH_METHOD, &arg, 1, &res)))
		return nullptr;
	if (res.vt == VT_DISPATCH)
		return res.pdispVal;
	VariantClear(&res);
	return nullptr;
}

int DispGetInt(IDispatch *pDisp, const wchar_t *name, int def)
{
	VARIANT v;
	return DispGetAs(pDisp, name, VT_I4, &v) ? v.lVal : def;
}

long long DispGetInt64(IDispatch *pDisp, const wchar_t *name, long long def)
{
	VARIANT v;
	return DispGetAs(pDisp, name, VT_I8, &v) ? v.llVal : def;
}

double DispGetDouble(IDispatch *pDisp, const wchar_t *name, double def)
{
	VARIANT v;
	return DispGetAs(pDisp, name, VT_R8, &v) ? v.dblVal : def;
}

bool DispGetBool(IDispatch *pDisp, const wchar_t *name, bool def)
{
	VARIANT v;
	return DispGetAs(pDisp, name, VT_BOOL, &v) ? v.boolVal != VARIANT_FALSE : def;
}

std::wstring DispGetStr(IDispatch *pDisp, const wchar_t *name)
{
	VARIANT v;
	if (!DispGetAs(pDisp, name, VT_BSTR, &v))
		return {};
	std::wstring r = v.bstrVal ? v.bstrVal : L"";
	VariantClear(&v);
	return r;
}

} // namespace ppt
