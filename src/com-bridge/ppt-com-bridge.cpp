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

#include "ppt-com-bridge.h"
#include <obs-module.h>
#include <plugin-support.h>

#include <oleauto.h>
#include <ocidl.h>

namespace {

HRESULT DispGetProp(IDispatch *pDisp, const wchar_t *name, VARIANT *pVar)
{
	if (!pDisp || !name || !pVar)
		return E_POINTER;
	VariantInit(pVar);

	DISPID dispid;
	BSTR bname = SysAllocString(name);
	HRESULT hr = pDisp->GetIDsOfNames(IID_NULL, &bname, 1, LOCALE_USER_DEFAULT, &dispid);
	SysFreeString(bname);
	if (FAILED(hr))
		return hr;

	DISPPARAMS dp{};
	return pDisp->Invoke(dispid, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_PROPERTYGET, &dp, pVar, nullptr, nullptr);
}

HRESULT DispCall0(IDispatch *pDisp, const wchar_t *name, VARIANT *pResult = nullptr)
{
	if (!pDisp)
		return E_POINTER;

	DISPID dispid;
	BSTR bname = SysAllocString(name);
	HRESULT hr = pDisp->GetIDsOfNames(IID_NULL, &bname, 1, LOCALE_USER_DEFAULT, &dispid);
	SysFreeString(bname);
	if (FAILED(hr))
		return hr;

	DISPPARAMS dp{};
	VARIANT result;
	VariantInit(&result);
	hr = pDisp->Invoke(dispid, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_METHOD, &dp, pResult ? pResult : &result,
			   nullptr, nullptr);
	if (!pResult)
		VariantClear(&result);
	return hr;
}

HRESULT DispCall1Int(IDispatch *pDisp, const wchar_t *name, int val)
{
	if (!pDisp)
		return E_POINTER;

	DISPID dispid;
	BSTR bname = SysAllocString(name);
	HRESULT hr = pDisp->GetIDsOfNames(IID_NULL, &bname, 1, LOCALE_USER_DEFAULT, &dispid);
	SysFreeString(bname);
	if (FAILED(hr))
		return hr;

	VARIANT arg;
	VariantInit(&arg);
	arg.vt = VT_I4;
	arg.lVal = val;

	DISPPARAMS dp{};
	dp.cArgs = 1;
	dp.rgvarg = &arg;
	return pDisp->Invoke(dispid, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_METHOD, &dp, nullptr, nullptr, nullptr);
}

HRESULT DispCallExport(IDispatch *pSlide, const wchar_t *path, int width, int height)
{
	if (!pSlide)
		return E_POINTER;
	DISPID dispid;
	BSTR bname = SysAllocString(L"Export");
	HRESULT hr = pSlide->GetIDsOfNames(IID_NULL, &bname, 1, LOCALE_USER_DEFAULT, &dispid);
	SysFreeString(bname);
	if (FAILED(hr))
		return hr;

	VARIANT args[4];
	for (int i = 0; i < 4; ++i)
		VariantInit(&args[i]);

	args[0].vt = VT_I4;
	args[0].lVal = height;
	args[1].vt = VT_I4;
	args[1].lVal = width;
	args[2].vt = VT_BSTR;
	args[2].bstrVal = SysAllocString(L"PNG");
	args[3].vt = VT_BSTR;
	args[3].bstrVal = SysAllocString(path);

	DISPPARAMS dp{};
	dp.cArgs = 4;
	dp.rgvarg = args;

	hr = pSlide->Invoke(dispid, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_METHOD, &dp, nullptr, nullptr, nullptr);

	SysFreeString(args[2].bstrVal);
	SysFreeString(args[3].bstrVal);
	return hr;
}

IDispatch *DispGetObj(IDispatch *pDisp, const wchar_t *name)
{
	VARIANT v;
	VariantInit(&v);
	if (FAILED(DispGetProp(pDisp, name, &v)))
		return nullptr;
	if (v.vt == VT_DISPATCH)
		return v.pdispVal;
	VariantClear(&v);
	return nullptr;
}

int DispGetInt(IDispatch *pDisp, const wchar_t *name, int def = 0)
{
	VARIANT v;
	VariantInit(&v);
	if (FAILED(DispGetProp(pDisp, name, &v)))
		return def;

	HRESULT hr = VariantChangeType(&v, &v, 0, VT_I4);
	int r = (SUCCEEDED(hr) && v.vt == VT_I4) ? v.lVal : def;
	VariantClear(&v);
	return r;
}

std::wstring DispGetStr(IDispatch *pDisp, const wchar_t *name)
{
	VARIANT v;
	VariantInit(&v);
	if (FAILED(DispGetProp(pDisp, name, &v)))
		return {};

	HRESULT hr = VariantChangeType(&v, &v, 0, VT_BSTR);
	std::wstring r = (SUCCEEDED(hr) && v.vt == VT_BSTR && v.bstrVal) ? v.bstrVal : L"";
	VariantClear(&v);
	return r;
}

IDispatch *FindNextVisibleSlide(IDispatch *pSlides, int fromIndex, int total, int &outIndex)
{
	outIndex = 0;
	if (!pSlides || fromIndex >= total)
		return nullptr;

	DISPID itemId;
	BSTR bname = SysAllocString(L"Item");
	HRESULT hr = pSlides->GetIDsOfNames(IID_NULL, &bname, 1, LOCALE_USER_DEFAULT, &itemId);
	SysFreeString(bname);
	if (FAILED(hr))
		return nullptr;

	for (int idx = fromIndex + 1; idx <= total; ++idx) {
		VARIANT arg, res;
		VariantInit(&arg);
		VariantInit(&res);
		arg.vt = VT_I4;
		arg.lVal = idx;
		DISPPARAMS dp{&arg, nullptr, 1, 0};
		pSlides->Invoke(itemId, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_METHOD, &dp, &res, nullptr, nullptr);

		if (res.vt != VT_DISPATCH || !res.pdispVal) {
			VariantClear(&res);
			continue;
		}

		IDispatch *pSlide = res.pdispVal;

		bool hidden = false;
		IDispatch *pTrans = DispGetObj(pSlide, L"SlideShowTransition");
		if (pTrans) {
			VARIANT vh;
			VariantInit(&vh);
			if (SUCCEEDED(DispGetProp(pTrans, L"Hidden", &vh))) {
				if (vh.vt == VT_BOOL)
					hidden = (vh.boolVal != VARIANT_FALSE);
				else if (SUCCEEDED(VariantChangeType(&vh, &vh, 0, VT_BOOL)))
					hidden = (vh.boolVal != VARIANT_FALSE);
				VariantClear(&vh);
			}
			pTrans->Release();
		}

		if (!hidden) {
			outIndex = idx;
			return pSlide;
		}

		pSlide->Release();
	}
	return nullptr;
}

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

} // namespace

namespace ppt {

class AppEventSink : public IDispatch {
public:
	enum Event : unsigned { SlideChanged = 1, ShowEnded = 2 };

	AppEventSink(const IID &eventsIid, DISPID slideChangedId, DISPID showEndedId)
		: m_eventsIid(eventsIid),
		  m_slideChangedId(slideChangedId),
		  m_showEndedId(showEndedId)
	{
	}

	bool HasPending() const { return m_pending != 0; }

	unsigned TakePending()
	{
		unsigned pending = m_pending;
		m_pending = 0;
		return pending;
	}

	STDMETHODIMP QueryInterface(REFIID riid, void **ppv) override
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

	STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_refs); }

	STDMETHODIMP_(ULONG) Release() override
	{
		ULONG refs = InterlockedDecrement(&m_refs);
		if (refs == 0)
			delete this;
		return refs;
	}

	STDMETHODIMP GetTypeInfoCount(UINT *pctinfo) override
	{
		if (pctinfo)
			*pctinfo = 0;
		return S_OK;
	}

	STDMETHODIMP GetTypeInfo(UINT, LCID, ITypeInfo **) override { return E_NOTIMPL; }

	STDMETHODIMP GetIDsOfNames(REFIID, LPOLESTR *, UINT, LCID, DISPID *) override { return E_NOTIMPL; }

	STDMETHODIMP Invoke(DISPID dispIdMember, REFIID, LCID, WORD, DISPPARAMS *, VARIANT *, EXCEPINFO *,
			    UINT *) override
	{
		if (dispIdMember == m_slideChangedId)
			m_pending |= SlideChanged;
		else if (dispIdMember == m_showEndedId)
			m_pending |= ShowEnded;
		return S_OK;
	}

private:
	IID m_eventsIid;
	DISPID m_slideChangedId;
	DISPID m_showEndedId;
	unsigned m_pending = 0;
	ULONG m_refs = 1;
};

class BusyRetryFilter : public IMessageFilter {
public:
	STDMETHODIMP QueryInterface(REFIID riid, void **ppv) override
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

	STDMETHODIMP_(ULONG) AddRef() override { return 1; }

	STDMETHODIMP_(ULONG) Release() override { return 1; }

	STDMETHODIMP_(DWORD) HandleInComingCall(DWORD, HTASK, DWORD, LPINTERFACEINFO) override
	{
		return SERVERCALL_ISHANDLED;
	}

	STDMETHODIMP_(DWORD) RetryRejectedCall(HTASK, DWORD dwTickCount, DWORD dwRejectType) override
	{
		if (dwRejectType == SERVERCALL_RETRYLATER && dwTickCount < 5000)
			return 100;
		return static_cast<DWORD>(-1);
	}

	STDMETHODIMP_(DWORD) MessagePending(HTASK, DWORD, DWORD) override { return PENDINGMSG_WAITDEFPROCESS; }
};

std::wstring GetNextSlideThumbPath()
{
	wchar_t tempPath[MAX_PATH];
	GetTempPathW(MAX_PATH, tempPath);
	return std::wstring(tempPath) + L"obs_ppt_next_" + std::to_wstring(GetCurrentProcessId()) + L".png";
}

ComBridge::ComBridge()
{
	m_wakeEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
}

ComBridge::~ComBridge()
{
	Stop();
	if (m_wakeEvent)
		CloseHandle(m_wakeEvent);
}

bool ComBridge::Start()
{
	if (!m_wakeEvent)
		return false;
	m_stopRequested.store(false);
	m_thread = std::thread([this] { ThreadProc(); });
	return true;
}

void ComBridge::Stop()
{
	m_stopRequested.store(true);
	if (m_wakeEvent)
		SetEvent(m_wakeEvent);
	if (m_thread.joinable())
		m_thread.join();
}

SlideInfo ComBridge::GetSlideInfo() const
{
	std::lock_guard<std::mutex> lk(m_mutex);
	return m_state;
}

HWND ComBridge::GetSlideshowHwnd() const
{
	std::lock_guard<std::mutex> lk(m_mutex);
	return m_state.slideshowHwnd;
}

void ComBridge::PostCommand(Command cmd)
{
	{
		std::lock_guard<std::mutex> lk(m_cmdMutex);
		if (!m_connected.load())
			return;
		m_pendingCmds.push_back(cmd);
	}
	SetEvent(m_wakeEvent);
}

void ComBridge::NextSlide()
{
	PostCommand({CmdType::Next});
}
void ComBridge::PrevSlide()
{
	PostCommand({CmdType::Prev});
}
void ComBridge::EndShow()
{
	PostCommand({CmdType::End});
}
void ComBridge::GotoSlide(int i)
{
	if (i < 1)
		return;
	PostCommand({CmdType::Goto, i});
}

void ComBridge::ThreadProc()
{
	HRESULT hrCo = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	if (FAILED(hrCo) && hrCo != RPC_E_CHANGED_MODE) {
		obs_log(LOG_ERROR, "CoInitializeEx failed: 0x%08X", hrCo);
		return;
	}

	BusyRetryFilter filter;
	IMessageFilter *pPrevFilter = nullptr;
	if (FAILED(CoRegisterMessageFilter(&filter, &pPrevFilter)))
		obs_log(LOG_WARNING, "CoRegisterMessageFilter failed, busy PowerPoint calls will not be retried");

	while (!m_stopRequested.load()) {
		if (!m_connected.load()) {
			if (!TryConnect()) {
				PumpFor(1000);
				continue;
			}
		}

		DWORD pollInterval = m_pSink ? 1000 : 100;
		if (!m_pSink || !m_pSink->HasPending())
			WaitForWork(pollInterval);
		if (m_stopRequested.load())
			break;

		RunCommands();

		unsigned events = m_pSink ? m_pSink->TakePending() : 0;
		if (events & AppEventSink::ShowEnded) {
			Disconnect();
			PumpFor(1000);
			continue;
		}

		ULONGLONG now = GetTickCount64();
		if ((events & AppEventSink::SlideChanged) || now - m_lastPollTick >= pollInterval) {
			m_lastPollTick = now;
			PollState();
			if (!m_connected.load())
				PumpFor(1000);
		}
	}

	Disconnect();
	CoRegisterMessageFilter(pPrevFilter, nullptr);
	if (pPrevFilter)
		pPrevFilter->Release();
	CoUninitialize();
}

void ComBridge::RunCommands()
{
	std::deque<Command> cmds;
	{
		std::lock_guard<std::mutex> lk(m_cmdMutex);
		cmds.swap(m_pendingCmds);
	}

	for (const Command &cmd : cmds) {
		if (!m_pView)
			break;

		switch (cmd.type) {
		case CmdType::Next:
			DispCall0(m_pView, L"Next");
			break;
		case CmdType::Prev:
			DispCall0(m_pView, L"Previous");
			break;
		case CmdType::Goto:
			DispCall1Int(m_pView, L"GotoSlide", cmd.gotoIndex);
			break;
		case CmdType::End:
			DispCall0(m_pView, L"Exit");
			break;
		default:
			break;
		}
	}
}

void ComBridge::WaitForWork(DWORD timeoutMs)
{
	MsgWaitForMultipleObjectsEx(1, &m_wakeEvent, timeoutMs, QS_ALLINPUT, MWMO_INPUTAVAILABLE);

	MSG msg;
	while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}
}

void ComBridge::PumpFor(DWORD durationMs)
{
	ULONGLONG deadline = GetTickCount64() + durationMs;
	while (!m_stopRequested.load()) {
		ULONGLONG now = GetTickCount64();
		if (now >= deadline)
			break;
		WaitForWork(static_cast<DWORD>(deadline - now));
	}
}

bool ComBridge::AdviseEvents()
{
	IID eventsIid;
	DISPID slideChangedId = DISPID_UNKNOWN;
	DISPID showEndedId = DISPID_UNKNOWN;
	if (!GetAppEventInfo(m_pApp, eventsIid, slideChangedId, showEndedId)) {
		obs_log(LOG_WARNING, "AdviseEvents: event type info lookup failed");
		return false;
	}

	IConnectionPointContainer *pContainer = nullptr;
	HRESULT hr = m_pApp->QueryInterface(IID_IConnectionPointContainer, reinterpret_cast<void **>(&pContainer));
	if (FAILED(hr)) {
		obs_log(LOG_WARNING, "AdviseEvents: IConnectionPointContainer failed: 0x%08X", hr);
		return false;
	}

	hr = pContainer->FindConnectionPoint(eventsIid, &m_pConnPoint);
	pContainer->Release();
	if (FAILED(hr)) {
		obs_log(LOG_WARNING, "AdviseEvents: FindConnectionPoint failed: 0x%08X", hr);
		m_pConnPoint = nullptr;
		return false;
	}

	m_pSink = new AppEventSink(eventsIid, slideChangedId, showEndedId);
	hr = m_pConnPoint->Advise(m_pSink, &m_adviseCookie);
	if (FAILED(hr)) {
		obs_log(LOG_WARNING, "AdviseEvents: Advise failed: 0x%08X", hr);
		UnadviseEvents();
		return false;
	}
	return true;
}

void ComBridge::UnadviseEvents()
{
	if (m_pConnPoint) {
		if (m_adviseCookie)
			m_pConnPoint->Unadvise(m_adviseCookie);
		m_pConnPoint->Release();
		m_pConnPoint = nullptr;
	}
	m_adviseCookie = 0;

	if (m_pSink) {
		m_pSink->Release();
		m_pSink = nullptr;
	}
}

bool ComBridge::TryConnect()
{
	IUnknown *pUnk = nullptr;
	CLSID clsid;
	if (FAILED(CLSIDFromProgID(L"PowerPoint.Application", &clsid)))
		return false;

	HRESULT hr = GetActiveObject(clsid, nullptr, &pUnk);
	if (FAILED(hr) || !pUnk)
		return false;

	hr = pUnk->QueryInterface(IID_IDispatch, reinterpret_cast<void **>(&m_pApp));
	pUnk->Release();
	if (FAILED(hr) || !m_pApp)
		return false;

	IDispatch *pShows = DispGetObj(m_pApp, L"SlideShowWindows");
	if (!pShows) {
		Disconnect();
		return false;
	}

	int count = DispGetInt(pShows, L"Count");
	if (count < 1) {
		pShows->Release();
		Disconnect();
		return false;
	}

	{
		DISPID dispid;
		BSTR bname = SysAllocString(L"Item");
		pShows->GetIDsOfNames(IID_NULL, &bname, 1, LOCALE_USER_DEFAULT, &dispid);
		SysFreeString(bname);

		VARIANT arg, result;
		VariantInit(&arg);
		VariantInit(&result);
		arg.vt = VT_I4;
		arg.lVal = 1;
		DISPPARAMS dp{&arg, nullptr, 1, 0};
		pShows->Invoke(dispid, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_METHOD, &dp, &result, nullptr, nullptr);
		pShows->Release();

		if (result.vt == VT_DISPATCH)
			m_pShow = result.pdispVal;
		else {
			VariantClear(&result);
			Disconnect();
			return false;
		}
	}

	m_pView = DispGetObj(m_pShow, L"View");
	if (!m_pView) {
		Disconnect();
		return false;
	}

	m_connected.store(true);
	m_firstFailureTick = 0;
	m_lastPollTick = 0;
	obs_log(LOG_INFO, "Connected to PowerPoint slideshow");

	if (!AdviseEvents())
		obs_log(LOG_WARNING, "PowerPoint events unavailable, falling back to polling");

	if (OnConnectionChanged)
		OnConnectionChanged(true);
	return true;
}

void ComBridge::Disconnect()
{
	UnadviseEvents();

	if (m_pView) {
		m_pView->Release();
		m_pView = nullptr;
	}
	if (m_pShow) {
		m_pShow->Release();
		m_pShow = nullptr;
	}
	if (m_pApp) {
		m_pApp->Release();
		m_pApp = nullptr;
	}
	bool wasConnected = m_connected.exchange(false);
	{
		std::lock_guard<std::mutex> lk(m_cmdMutex);
		m_pendingCmds.clear();
	}
	m_lastExportedIndex = -1;

	{
		std::lock_guard<std::mutex> lk(m_mutex);
		m_state = {};
	}

	if (!wasConnected)
		return;

	DeleteFileW(GetNextSlideThumbPath().c_str());
	obs_log(LOG_INFO, "Disconnected from PowerPoint");
	if (OnConnectionChanged)
		OnConnectionChanged(false);
}

void ComBridge::PollState()
{
	if (!m_pView) {
		obs_log(LOG_WARNING, "PollState: m_pView is null!");
		Disconnect();
		return;
	}

	VARIANT v;
	VariantInit(&v);
	HRESULT hr = DispGetProp(m_pView, L"CurrentShowPosition", &v);
	int cur = -1;
	if (SUCCEEDED(hr)) {
		if (SUCCEEDED(VariantChangeType(&v, &v, 0, VT_I4)) && v.vt == VT_I4)
			cur = v.lVal;
		VariantClear(&v);
	}

	if (cur < 0) {
		ULONGLONG now = GetTickCount64();
		if (!m_firstFailureTick)
			m_firstFailureTick = now;
		if (now - m_firstFailureTick >= 2000) {
			obs_log(LOG_INFO, "PollState: CurrentShowPosition failing since %llu ms (0x%08X)",
				now - m_firstFailureTick, hr);
			Disconnect();
		}
		return;
	}
	m_firstFailureTick = 0;

	if (cur == 0)
		return;

	int total = 0;
	HWND showHwnd = nullptr;
	int nextSlide = 0;
	std::wstring notes;

	IDispatch *pPres = DispGetObj(m_pShow, L"Presentation");
	if (pPres) {
		IDispatch *pPS = DispGetObj(pPres, L"PageSetup");
		if (pPS) {
			VARIANT vs;
			VariantInit(&vs);
			double sw = 0, sh = 0;
			if (SUCCEEDED(DispGetProp(pPS, L"SlideWidth", &vs))) {
				if (SUCCEEDED(VariantChangeType(&vs, &vs, 0, VT_R8)))
					sw = vs.dblVal;
				VariantClear(&vs);
			}
			if (SUCCEEDED(DispGetProp(pPS, L"SlideHeight", &vs))) {
				if (SUCCEEDED(VariantChangeType(&vs, &vs, 0, VT_R8)))
					sh = vs.dblVal;
				VariantClear(&vs);
			}
			if (sw > 0 && sh > 0)
				m_slideAspect.store(static_cast<float>(sw / sh));
			pPS->Release();
		}

		IDispatch *pSlides = DispGetObj(pPres, L"Slides");
		if (pSlides) {
			total = DispGetInt(pSlides, L"Count");

			auto getSlide = [&](int idx) -> IDispatch * {
				DISPID dispid;
				BSTR bname = SysAllocString(L"Item");
				if (FAILED(pSlides->GetIDsOfNames(IID_NULL, &bname, 1, LOCALE_USER_DEFAULT, &dispid))) {
					SysFreeString(bname);
					return nullptr;
				}
				SysFreeString(bname);
				VARIANT arg, res;
				VariantInit(&arg);
				VariantInit(&res);
				arg.vt = VT_I4;
				arg.lVal = idx;
				DISPPARAMS dp{&arg, nullptr, 1, 0};
				pSlides->Invoke(dispid, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_METHOD, &dp, &res,
						nullptr, nullptr);
				return (res.vt == VT_DISPATCH) ? res.pdispVal : nullptr;
			};

			IDispatch *pSlide = getSlide(cur);
			if (pSlide) {
				notes = GetNotesText(pSlide);
				pSlide->Release();
			}

			IDispatch *pNext = FindNextVisibleSlide(pSlides, cur, total, nextSlide);

			if (cur != m_lastExportedIndex) {
				std::wstring outPath = GetNextSlideThumbPath();
				DeleteFileW(outPath.c_str());
				constexpr int thumbWidth = 1920;
				int thumbHeight = static_cast<int>(thumbWidth / m_slideAspect.load() + 0.5f);
				if (!pNext ||
				    SUCCEEDED(DispCallExport(pNext, outPath.c_str(), thumbWidth, thumbHeight)))
					m_lastExportedIndex = cur;
			}

			if (pNext)
				pNext->Release();
			pSlides->Release();
		}
		pPres->Release();
	}

	{
		VARIANT vh;
		VariantInit(&vh);
		if (SUCCEEDED(DispGetProp(m_pShow, L"HWND", &vh))) {
			if (vh.vt == VT_I8) {
				showHwnd = reinterpret_cast<HWND>(static_cast<uintptr_t>(vh.llVal));
			} else if (vh.vt == VT_I4) {
				showHwnd = reinterpret_cast<HWND>(static_cast<uintptr_t>(vh.lVal));
			} else if (SUCCEEDED(VariantChangeType(&vh, &vh, 0, VT_I8)) && vh.vt == VT_I8) {
				showHwnd = reinterpret_cast<HWND>(static_cast<uintptr_t>(vh.llVal));
			}
			VariantClear(&vh);
		}
	}

	SlideInfo info;
	info.currentSlide = cur;
	info.totalSlides = total;
	info.nextSlide = nextSlide;
	info.notes = notes;
	info.slideshowHwnd = showHwnd;
	info.slideshowActive = true;

	{
		std::lock_guard<std::mutex> lk(m_mutex);
		m_state = info;
	}
}

std::wstring ComBridge::GetNotesText(IDispatch *pSlide)
{
	if (!pSlide)
		return {};

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

	DISPID did;
	BSTR bn = SysAllocString(L"Item");
	HRESULT hr = pPh->GetIDsOfNames(IID_NULL, &bn, 1, LOCALE_USER_DEFAULT, &did);
	SysFreeString(bn);
	if (FAILED(hr)) {
		pPh->Release();
		return {};
	}

	IDispatch *pBody = nullptr;
	int count = DispGetInt(pPh, L"Count");
	for (int i = 1; i <= count && !pBody; ++i) {
		VARIANT a, r;
		VariantInit(&a);
		VariantInit(&r);
		a.vt = VT_I4;
		a.lVal = i;
		DISPPARAMS d{&a, nullptr, 1, 0};
		pPh->Invoke(did, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_METHOD, &d, &r, nullptr, nullptr);
		if (r.vt != VT_DISPATCH || !r.pdispVal) {
			VariantClear(&r);
			continue;
		}

		IDispatch *pShape = r.pdispVal;
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

} // namespace ppt