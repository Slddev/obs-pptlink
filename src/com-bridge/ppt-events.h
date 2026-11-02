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
#include <vector>

namespace ppt {

// Looks up the EApplication event interface and the DISPIDs of SlideShowNextSlide and SlideShowEnd
// in PowerPoint's type library
bool GetAppEventInfo(IDispatch *pApp, IID &eventsIid, DISPID &slideChangedId, DISPID &showEndedId);

// EApplication sink that records which events fired; drained by the bridge's STA thread
class AppEventSink : public IDispatch {
public:
	enum Event : unsigned { SlideChanged = 1, ShowEnded = 2 };

	AppEventSink(const IID &eventsIid, DISPID slideChangedId, DISPID showEndedId);
	~AppEventSink();

	bool HasPending() const { return m_pending != 0; }
	unsigned TakePending();

	// True if pres is among the presentations whose slideshow ended, or if the event didn't say which
	bool TakeEnded(IUnknown *pres);

	STDMETHODIMP QueryInterface(REFIID riid, void **ppv) override;
	STDMETHODIMP_(ULONG) AddRef() override;
	STDMETHODIMP_(ULONG) Release() override;
	STDMETHODIMP GetTypeInfoCount(UINT *pctinfo) override;
	STDMETHODIMP GetTypeInfo(UINT, LCID, ITypeInfo **) override { return E_NOTIMPL; }
	STDMETHODIMP GetIDsOfNames(REFIID, LPOLESTR *, UINT, LCID, DISPID *) override { return E_NOTIMPL; }
	STDMETHODIMP Invoke(DISPID dispIdMember, REFIID, LCID, WORD, DISPPARAMS *pParams, VARIANT *, EXCEPINFO *,
			    UINT *) override;

private:
	void ReleaseEnded();

	IID m_eventsIid;
	DISPID m_slideChangedId;
	DISPID m_showEndedId;
	unsigned m_pending = 0;
	std::vector<IUnknown *> m_ended;
	ULONG m_refs = 1;
};

// Retries calls PowerPoint rejects while busy (e.g. a dialog is open) for up to 5 s
class BusyRetryFilter : public IMessageFilter {
public:
	STDMETHODIMP QueryInterface(REFIID riid, void **ppv) override;
	STDMETHODIMP_(ULONG) AddRef() override { return 1; }
	STDMETHODIMP_(ULONG) Release() override { return 1; }
	STDMETHODIMP_(DWORD) HandleInComingCall(DWORD, HTASK, DWORD, LPINTERFACEINFO) override;
	STDMETHODIMP_(DWORD) RetryRejectedCall(HTASK, DWORD dwTickCount, DWORD dwRejectType) override;
	STDMETHODIMP_(DWORD) MessagePending(HTASK, DWORD, DWORD) override { return PENDINGMSG_WAITDEFPROCESS; }
};

} // namespace ppt
