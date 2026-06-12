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

#include <oleauto.h>
#include <thread>
#include <chrono>

#pragma comment(lib, "shcore")

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

} // namespace

namespace ppt {

ComBridge::ComBridge() = default;
ComBridge::~ComBridge()
{
	Stop();
}

bool ComBridge::Start()
{
	m_stopRequested.store(false);
	m_thread = std::thread([this] { ThreadProc(); });
	return true;
}

void ComBridge::Stop()
{
	m_stopRequested.store(true);
	if (m_thread.joinable())
		m_thread.join();
}

SlideInfo ComBridge::GetSlideInfo() const
{
	std::lock_guard<std::mutex> lk(m_mutex);
	return m_state;
}

SlidePaneRect ComBridge::GetSlidePaneRect() const
{
	std::lock_guard<std::mutex> lk(m_mutex);
	return m_slidePaneRect;
}

void ComBridge::PostCommand(Command cmd)
{
	std::lock_guard<std::mutex> lk(m_cmdMutex);
	m_pendingCmd = cmd;
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
	PostCommand({CmdType::Goto, i});
}

void ComBridge::ThreadProc()
{
	HRESULT hrCo = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	if (FAILED(hrCo) && hrCo != RPC_E_CHANGED_MODE) {
		blog(LOG_ERROR, "[obs-pptlink] CoInitializeEx failed: 0x%08X", hrCo);
		return;
	}

	while (!m_stopRequested.load()) {
		if (!m_connected.load()) {
			if (!TryConnect()) {
				std::this_thread::sleep_for(std::chrono::milliseconds(1000));
				continue;
			}
		}

		{
			std::lock_guard<std::mutex> lk(m_cmdMutex);
			if (m_pendingCmd.has_value()) {
				auto cmd = *m_pendingCmd;
				m_pendingCmd.reset();

				if (m_pView) {
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
		}

		PollState();
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}

	Disconnect();
	CoUninitialize();
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
	blog(LOG_INFO, "[obs-pptlink] Connected to PowerPoint slideshow");

	if (OnConnectionChanged)
		OnConnectionChanged(true);
	return true;
}

void ComBridge::Disconnect()
{
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
	m_connected.store(false);
	m_lastSlideIndex = -1;
	m_lastExportedIndex = -1;

	{
		std::lock_guard<std::mutex> lk(m_mutex);
		m_state = {};
	}

	blog(LOG_INFO, "[obs-pptlink] Disconnected from PowerPoint");
	if (OnConnectionChanged)
		OnConnectionChanged(false);
}

void ComBridge::PollState()
{
	if (!m_pView) {
		blog(LOG_WARNING, "[obs-pptlink] PollState: m_pView is null!");
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
		if (++m_consecutiveFailures >= 15) {
			m_consecutiveFailures = 0;
			Disconnect();
		}
		return;
	}
	m_consecutiveFailures = 0;

	if (cur == 0) {
		if (m_lastSlideIndex > 0) {
			m_lastSlideIndex = 0;
			SlideInfo inactive;
			inactive.slideshowActive = false;
			{
				std::lock_guard<std::mutex> lk(m_mutex);
				m_state = inactive;
			}
			if (OnSlideChanged)
				OnSlideChanged(inactive);
		}
		return;
	}

	int total = 0;
	HWND showHwnd = nullptr;
	std::wstring notes, nextNotes;

	IDispatch *pPres = DispGetObj(m_pShow, L"Presentation");
	if (pPres) {
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

			if (cur < total) {
				IDispatch *pNext = getSlide(cur + 1);
				if (pNext) {
					nextNotes = GetNotesText(pNext);
					if (cur != m_lastExportedIndex) {
						wchar_t tempPath[MAX_PATH];
						GetTempPathW(MAX_PATH, tempPath);
						std::wstring outPath = std::wstring(tempPath) + L"obs_ppt_next.png";
						if (SUCCEEDED(DispCallExport(pNext, outPath.c_str(), 960, 540)))
							m_lastExportedIndex = cur;
					}
					pNext->Release();
				}
			}
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

	SlidePaneRect paneRect;
	if (showHwnd) {
		HWND mdiClient = FindWindowEx(showHwnd, nullptr, L"MDIClient", nullptr);
		if (mdiClient && IsWindowVisible(mdiClient)) {
			RECT mdiScr = {};
			GetWindowRect(mdiClient, &mdiScr);
			double mdiW = static_cast<double>(mdiScr.right - mdiScr.left);
			double mdiH = static_cast<double>(mdiScr.bottom - mdiScr.top);

			double slideAspect = 16.0 / 9.0;
			IDispatch *pPres2 = DispGetObj(m_pShow, L"Presentation");
			if (pPres2) {
				IDispatch *pPS = DispGetObj(pPres2, L"PageSetup");
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
						slideAspect = sw / sh;
					pPS->Release();
				}
				pPres2->Release();
			}

			double fitW = mdiW, fitH = mdiW / slideAspect;
			if (fitH > mdiH) {
				fitH = mdiH;
				fitW = fitH * slideAspect;
			}
			double offX = (mdiW - fitW) / 2.0;
			double offY = (mdiH - fitH) / 2.0;

			paneRect.x = static_cast<int>(mdiScr.left + offX + 0.5);
			paneRect.y = static_cast<int>(mdiScr.top + offY + 0.5);
			paneRect.w = static_cast<int>(fitW + 0.5);
			paneRect.h = static_cast<int>(fitH + 0.5);
			paneRect.valid = (paneRect.w > 0 && paneRect.h > 0);
		}
	}

	if (paneRect.valid)
		m_slideAspect.store(static_cast<float>(paneRect.w) / static_cast<float>(paneRect.h));

	bool changed = (cur != m_lastSlideIndex);
	m_lastSlideIndex = cur;

	SlideInfo info;
	info.currentSlide = cur;
	info.totalSlides = total;
	info.notes = notes;
	info.nextNotes = nextNotes;
	info.slideshowHwnd = showHwnd;
	info.slideshowActive = true;

	{
		std::lock_guard<std::mutex> lk(m_mutex);
		m_state = info;
		m_slidePaneRect = paneRect;
	}

	if (changed && OnSlideChanged)
		OnSlideChanged(info);
}

std::wstring ComBridge::GetNotesText(IDispatch *pSlide)
{
	if (!pSlide)
		return {};

	IDispatch *pSlideShapes = nullptr;
	IDispatch *pNP = DispGetObj(pSlide, L"NotesPage");
	if (!pNP)
		return {};
	IDispatch *pSh = DispGetObj(pNP, L"Shapes");
	pNP->Release();
	if (!pSh)
		return {};

	DISPID did;
	BSTR bn = SysAllocString(L"Item");
	HRESULT hr = pSh->GetIDsOfNames(IID_NULL, &bn, 1, LOCALE_USER_DEFAULT, &did);
	SysFreeString(bn);
	if (FAILED(hr)) {
		pSh->Release();
		return {};
	}

	VARIANT a, r;
	VariantInit(&a);
	VariantInit(&r);
	a.vt = VT_I4;
	a.lVal = 2;
	DISPPARAMS d{&a, nullptr, 1, 0};
	pSh->Invoke(did, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_METHOD, &d, &r, nullptr, nullptr);
	pSh->Release();

	if (r.vt == VT_DISPATCH) {
		pSlideShapes = r.pdispVal;
	} else {
		VariantClear(&r);
		return {};
	}

	IDispatch *pTF = DispGetObj(pSlideShapes, L"TextFrame");
	pSlideShapes->Release();
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