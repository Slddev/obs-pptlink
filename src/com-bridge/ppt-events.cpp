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

#include "ppt-events.h"
#include <obs-module.h>
#include <plugin-support.h>

namespace ppt {

bool GetAppEventInfo(IDispatch *pApp, IID &eventsIid, DISPID &slideChangedId, DISPID &showEndedId)
{
	CLSID clsid;
	if (FAILED(CLSIDFromProgID(L"PowerPoint.Application", &clsid)))
		return false;

	ITypeInfo *pAppInfo = nullptr;
	HRESULT hr = pApp->GetTypeInfo(0, LOCALE_USER_DEFAULT, &pAppInfo);
	if (FAILED(hr)) {
		obs_log(LOG_WARNING, "GetAppEventInfo: GetTypeInfo failed: 0x%08X", hr);
		return false;
	}

	ITypeLib *pLib = nullptr;
	UINT index = 0;
	hr = pAppInfo->GetContainingTypeLib(&pLib, &index);
	pAppInfo->Release();
	if (FAILED(hr)) {
		obs_log(LOG_WARNING, "GetAppEventInfo: GetContainingTypeLib failed: 0x%08X", hr);
		return false;
	}

	ITypeInfo *pClassInfo = nullptr;
	hr = pLib->GetTypeInfoOfGuid(clsid, &pClassInfo);
	pLib->Release();
	if (FAILED(hr)) {
		obs_log(LOG_WARNING, "GetAppEventInfo: GetTypeInfoOfGuid failed: 0x%08X", hr);
		return false;
	}

	ITypeInfo *pEventInfo = nullptr;
	TYPEATTR *pClassAttr = nullptr;
	if (SUCCEEDED(pClassInfo->GetTypeAttr(&pClassAttr))) {
		for (UINT i = 0; i < pClassAttr->cImplTypes && !pEventInfo; ++i) {
			INT flags = 0;
			HREFTYPE ref = 0;
			if (SUCCEEDED(pClassInfo->GetImplTypeFlags(i, &flags)) && (flags & IMPLTYPEFLAG_FDEFAULT) &&
			    (flags & IMPLTYPEFLAG_FSOURCE) && SUCCEEDED(pClassInfo->GetRefTypeOfImplType(i, &ref)))
				pClassInfo->GetRefTypeInfo(ref, &pEventInfo);
		}
		pClassInfo->ReleaseTypeAttr(pClassAttr);
	}
	pClassInfo->Release();
	if (!pEventInfo) {
		obs_log(LOG_WARNING, "GetAppEventInfo: no default source interface found");
		return false;
	}

	bool haveSlideChanged = false;
	bool haveShowEnded = false;
	TYPEATTR *pEventAttr = nullptr;
	if (SUCCEEDED(pEventInfo->GetTypeAttr(&pEventAttr))) {
		eventsIid = pEventAttr->guid;

		for (UINT i = 0; i < pEventAttr->cFuncs; ++i) {
			FUNCDESC *pFunc = nullptr;
			if (FAILED(pEventInfo->GetFuncDesc(i, &pFunc)))
				continue;

			BSTR name = nullptr;
			if (SUCCEEDED(pEventInfo->GetDocumentation(pFunc->memid, &name, nullptr, nullptr, nullptr)) &&
			    name) {
				if (wcscmp(name, L"SlideShowNextSlide") == 0) {
					slideChangedId = pFunc->memid;
					haveSlideChanged = true;
				} else if (wcscmp(name, L"SlideShowEnd") == 0) {
					showEndedId = pFunc->memid;
					haveShowEnded = true;
				}
				SysFreeString(name);
			}
			pEventInfo->ReleaseFuncDesc(pFunc);
		}
		pEventInfo->ReleaseTypeAttr(pEventAttr);
	}
	pEventInfo->Release();

	if (!haveSlideChanged || !haveShowEnded)
		obs_log(LOG_WARNING, "GetAppEventInfo: SlideShowNextSlide/SlideShowEnd not found in event interface");
	return haveSlideChanged && haveShowEnded;
}

AppEventSink::AppEventSink(const IID &eventsIid, DISPID slideChangedId, DISPID showEndedId)
	: m_eventsIid(eventsIid),
	  m_slideChangedId(slideChangedId),
	  m_showEndedId(showEndedId)
{
}

AppEventSink::~AppEventSink()
{
	ReleaseEnded();
}

unsigned AppEventSink::TakePending()
{
	unsigned pending = m_pending;
	m_pending = 0;
	return pending;
}

bool AppEventSink::TakeEnded(IUnknown *pres)
{
	bool ended = m_ended.empty();
	for (IUnknown *unk : m_ended)
		ended = ended || unk == pres;
	ReleaseEnded();
	return ended;
}

void AppEventSink::ReleaseEnded()
{
	for (IUnknown *unk : m_ended)
		unk->Release();
	m_ended.clear();
}

STDMETHODIMP AppEventSink::QueryInterface(REFIID riid, void **ppv)
{
	if (!ppv)
		return E_POINTER;
	if (riid == IID_IUnknown || riid == IID_IDispatch || riid == m_eventsIid) {
		*ppv = static_cast<IDispatch *>(this);
		AddRef();
		return S_OK;
	}
	*ppv = nullptr;
	return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) AppEventSink::AddRef()
{
	return InterlockedIncrement(&m_refs);
}

STDMETHODIMP_(ULONG) AppEventSink::Release()
{
	ULONG refs = InterlockedDecrement(&m_refs);
	if (refs == 0)
		delete this;
	return refs;
}

STDMETHODIMP AppEventSink::GetTypeInfoCount(UINT *pctinfo)
{
	if (pctinfo)
		*pctinfo = 0;
	return S_OK;
}

STDMETHODIMP AppEventSink::Invoke(DISPID dispIdMember, REFIID, LCID, WORD, DISPPARAMS *pParams, VARIANT *, EXCEPINFO *,
				  UINT *)
{
	if (dispIdMember == m_slideChangedId) {
		m_pending |= SlideChanged;
	} else if (dispIdMember == m_showEndedId) {
		m_pending |= ShowEnded;
		IUnknown *unk = nullptr;
		if (pParams && pParams->cArgs >= 1 && pParams->rgvarg[0].vt == VT_DISPATCH &&
		    pParams->rgvarg[0].pdispVal &&
		    SUCCEEDED(
			    pParams->rgvarg[0].pdispVal->QueryInterface(IID_IUnknown, reinterpret_cast<void **>(&unk))))
			m_ended.push_back(unk);
	}
	return S_OK;
}

STDMETHODIMP BusyRetryFilter::QueryInterface(REFIID riid, void **ppv)
{
	if (!ppv)
		return E_POINTER;
	if (riid == IID_IUnknown || riid == IID_IMessageFilter) {
		*ppv = static_cast<IMessageFilter *>(this);
		return S_OK;
	}
	*ppv = nullptr;
	return E_NOINTERFACE;
}

STDMETHODIMP_(DWORD) BusyRetryFilter::HandleInComingCall(DWORD, HTASK, DWORD, LPINTERFACEINFO)
{
	return SERVERCALL_ISHANDLED;
}

STDMETHODIMP_(DWORD) BusyRetryFilter::RetryRejectedCall(HTASK, DWORD dwTickCount, DWORD dwRejectType)
{
	if (dwRejectType == SERVERCALL_RETRYLATER && dwTickCount < 5000)
		return 100;
	return static_cast<DWORD>(-1);
}

} // namespace ppt
