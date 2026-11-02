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

#include "ppt-events.h"
#include "ppt-slides.h"
#include <ocidl.h>

namespace ppt {

// PpSlideShowState
constexpr int ppSlideShowRunning = 1;
constexpr int ppSlideShowBlackScreen = 3;
constexpr int ppSlideShowWhiteScreen = 4;

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

std::vector<ShowEntry> ComBridge::GetShows() const
{
	std::lock_guard<std::mutex> lk(m_mutex);
	return m_shows;
}

std::wstring ComBridge::GetPreferredShow() const
{
	std::lock_guard<std::mutex> lk(m_mutex);
	return m_preferredShow;
}

bool ComBridge::HasPreferredShow() const
{
	std::lock_guard<std::mutex> lk(m_mutex);
	return !m_preferredShow.empty();
}

void ComBridge::SetPreferredShow(const std::wstring &fullName)
{
	{
		std::lock_guard<std::mutex> lk(m_mutex);
		if (m_preferredShow == fullName)
			return;
		m_preferredShow = fullName;
	}
	m_preferenceChanged.store(true);
	SetEvent(m_wakeEvent);
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
void ComBridge::FirstSlide()
{
	PostCommand({CmdType::First});
}
void ComBridge::LastSlide()
{
	PostCommand({CmdType::Last});
}
void ComBridge::ToggleBlackScreen()
{
	PostCommand({CmdType::Black});
}
void ComBridge::ToggleWhiteScreen()
{
	PostCommand({CmdType::White});
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
		DWORD timeout = pollInterval;
		if (m_prefetchSlide) {
			ULONGLONG now = GetTickCount64();
			DWORD remaining = now >= m_prefetchDue ? 0 : static_cast<DWORD>(m_prefetchDue - now);
			timeout = remaining < pollInterval ? remaining : pollInterval;
		}
		if (!m_pSink || !m_pSink->HasPending())
			WaitForWork(timeout);
		if (m_stopRequested.load())
			break;

		if (RunCommands())
			m_lastPollTick = 0;

		if (m_preferenceChanged.exchange(false)) {
			std::wstring want = GetPreferredShow();
			if (!want.empty() && want != m_currentShow) {
				Disconnect();
				continue;
			}
		}

		unsigned events = m_pSink ? m_pSink->TakePending() : 0;
		if (events & AppEventSink::ShowEnded) {
			if (m_pSink->TakeEnded(m_pPresUnk)) {
				Disconnect();
				PumpFor(1000);
				continue;
			}
			m_lastShowsTick = 0;
		}

		ULONGLONG now = GetTickCount64();
		if (now - m_lastShowsTick >= 2000) {
			m_lastShowsTick = now;
			RefreshShows();
		}
		if ((events & AppEventSink::SlideChanged) || now - m_lastPollTick >= pollInterval) {
			m_lastPollTick = now;
			PollState();
			if (!m_connected.load()) {
				PumpFor(1000);
				continue;
			}
		}

		if (m_prefetchSlide && GetTickCount64() >= m_prefetchDue && !(m_pSink && m_pSink->HasPending()))
			ExportPrefetch();
	}

	Disconnect();
	CoRegisterMessageFilter(pPrevFilter, nullptr);
	if (pPrevFilter)
		pPrevFilter->Release();
	CoUninitialize();
}

void ComBridge::ExportPrefetch()
{
	int index = m_prefetchSlide;
	m_prefetchSlide = 0;
	if (index < 1 || !m_pShow || m_thumbs.IsDecoded(index))
		return;

	if (!m_thumbs.IsCached(index)) {
		IDispatch *pPres = DispGetObj(m_pShow, L"Presentation");
		if (!pPres)
			return;
		IDispatch *pSlides = DispGetObj(pPres, L"Slides");
		pPres->Release();
		if (!pSlides)
			return;

		IDispatch *pSlide = DispGetItem(pSlides, index);
		pSlides->Release();
		if (!pSlide)
			return;

		bool exported = m_thumbs.Export(pSlide, index, m_slideAspect.load());
		pSlide->Release();
		if (!exported)
			return;
	}

	m_thumbs.Prefetch(index);
}

void ComBridge::UpdateNextThumb(IDispatch *pSlides, IDispatch *pNext, int cur, int nextSlide, int total)
{
	m_nextThumb = pNext && m_thumbs.Export(pNext, nextSlide, m_slideAspect.load()) ? m_thumbs.Load(nextSlide)
										       : nullptr;
	if (pNext && !m_nextThumb)
		return;

	m_lastThumbSlide = cur;

	int afterNext = 0;
	if (pNext) {
		IDispatch *pAfter = FindNextVisibleSlide(pSlides, nextSlide, total, afterNext);
		if (pAfter)
			pAfter->Release();
	}
	m_prefetchSlide = afterNext;
	m_prefetchDue = GetTickCount64() + 500;
}

bool ComBridge::RunCommands()
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
			DispCall(m_pView, L"Next");
			break;
		case CmdType::Prev:
			DispCall(m_pView, L"Previous");
			break;
		case CmdType::First:
			DispCall(m_pView, L"First");
			break;
		case CmdType::Last:
			DispCall(m_pView, L"Last");
			break;
		case CmdType::Black:
		case CmdType::White: {
			int screen = cmd.type == CmdType::Black ? ppSlideShowBlackScreen : ppSlideShowWhiteScreen;
			int state = DispGetInt(m_pView, L"State", ppSlideShowRunning);
			DispPutInt(m_pView, L"State", state == screen ? ppSlideShowRunning : screen);
			break;
		}
		case CmdType::Goto:
			DispCallInt(m_pView, L"GotoSlide", cmd.gotoIndex);
			break;
		case CmdType::End:
			DispCall(m_pView, L"Exit");
			break;
		default:
			break;
		}
	}
	return !cmds.empty();
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

int ComBridge::ScanShows(IDispatch *pShows, const std::wstring &want)
{
	std::vector<ShowEntry> shows;
	int count = DispGetInt(pShows, L"Count");
	int pick = 0;
	for (int i = 1; i <= count; ++i) {
		IDispatch *pWnd = DispGetItem(pShows, i);
		if (!pWnd)
			continue;
		ShowEntry entry;
		IDispatch *pPres = DispGetObj(pWnd, L"Presentation");
		if (pPres) {
			entry.name = DispGetStr(pPres, L"Name");
			entry.fullName = DispGetStr(pPres, L"FullName");
			pPres->Release();
		}
		pWnd->Release();

		if (want.empty() ? (!pick || (!m_lastShow.empty() && entry.fullName == m_lastShow))
				 : entry.fullName == want)
			pick = i;
		shows.push_back(std::move(entry));
	}

	std::lock_guard<std::mutex> lk(m_mutex);
	m_shows = std::move(shows);
	return pick;
}

void ComBridge::RefreshShows()
{
	IDispatch *pShows = m_pApp ? DispGetObj(m_pApp, L"SlideShowWindows") : nullptr;
	if (!pShows)
		return;
	ScanShows(pShows, {});
	pShows->Release();
}

bool ComBridge::TryConnect()
{
	IUnknown *pUnk = nullptr;
	CLSID clsid;
	if (FAILED(CLSIDFromProgID(L"PowerPoint.Application", &clsid)))
		return false;

	HRESULT hr = GetActiveObject(clsid, nullptr, &pUnk);
	if (FAILED(hr) || !pUnk) {
		std::lock_guard<std::mutex> lk(m_mutex);
		m_shows.clear();
		return false;
	}

	hr = pUnk->QueryInterface(IID_IDispatch, reinterpret_cast<void **>(&m_pApp));
	pUnk->Release();
	if (FAILED(hr) || !m_pApp)
		return false;

	IDispatch *pShows = DispGetObj(m_pApp, L"SlideShowWindows");
	if (!pShows) {
		Disconnect();
		return false;
	}

	m_preferenceChanged.store(false);
	int index = ScanShows(pShows, GetPreferredShow());
	m_pShow = index > 0 ? DispGetItem(pShows, index) : nullptr;
	pShows->Release();
	if (!m_pShow) {
		Disconnect();
		return false;
	}

	m_pView = DispGetObj(m_pShow, L"View");
	if (!m_pView) {
		Disconnect();
		return false;
	}

	IDispatch *pPres = DispGetObj(m_pShow, L"Presentation");
	if (pPres) {
		m_currentShow = DispGetStr(pPres, L"FullName");
		m_currentShowName = DispGetStr(pPres, L"Name");
		pPres->QueryInterface(IID_IUnknown, reinterpret_cast<void **>(&m_pPresUnk));
		pPres->Release();
	}
	m_lastShow = m_currentShow;
	m_lastShowsTick = GetTickCount64();

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
	if (m_pPresUnk) {
		m_pPresUnk->Release();
		m_pPresUnk = nullptr;
	}
	m_currentShow.clear();
	m_currentShowName.clear();
	bool wasConnected = m_connected.exchange(false);
	{
		std::lock_guard<std::mutex> lk(m_cmdMutex);
		m_pendingCmds.clear();
	}
	m_lastThumbSlide = -1;
	m_prefetchSlide = 0;
	m_nextThumb.reset();

	{
		std::lock_guard<std::mutex> lk(m_mutex);
		m_state = {};
	}

	if (!wasConnected)
		return;

	m_thumbs.Clear();
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

	int cur = DispGetInt(m_pView, L"CurrentShowPosition", -1);

	if (cur < 0) {
		ULONGLONG now = GetTickCount64();
		if (!m_firstFailureTick)
			m_firstFailureTick = now;
		if (now - m_firstFailureTick >= 2000) {
			obs_log(LOG_INFO, "PollState: CurrentShowPosition failing since %llu ms",
				now - m_firstFailureTick);
			Disconnect();
		}
		return;
	}
	m_firstFailureTick = 0;

	if (cur == 0)
		return;

	int total = 0;
	int nextSlide = 0;
	std::wstring notes;

	IDispatch *pPres = DispGetObj(m_pShow, L"Presentation");
	if (pPres) {
		float aspect = ReadSlideAspect(pPres);
		if (aspect > 0)
			m_slideAspect.store(aspect);

		IDispatch *pSlides = DispGetObj(pPres, L"Slides");
		if (pSlides) {
			total = DispGetInt(pSlides, L"Count");

			IDispatch *pSlide = DispGetItem(pSlides, cur);
			if (pSlide) {
				notes = GetNotesText(pSlide);
				pSlide->Release();
			}

			IDispatch *pNext = FindNextVisibleSlide(pSlides, cur, total, nextSlide);
			if (cur != m_lastThumbSlide)
				UpdateNextThumb(pSlides, pNext, cur, nextSlide, total);
			if (pNext)
				pNext->Release();
			pSlides->Release();
		}
		pPres->Release();
	}

	SlideInfo info;
	info.currentSlide = cur;
	info.totalSlides = total;
	info.nextSlide = nextSlide;
	info.notes = notes;
	info.nextThumb = m_nextThumb;
	info.slideshowHwnd = GetShowHwnd(m_pShow);
	info.slideshowActive = true;
	info.presentation = m_currentShowName;
	int state = DispGetInt(m_pView, L"State");
	info.blackScreen = state == ppSlideShowBlackScreen;
	info.whiteScreen = state == ppSlideShowWhiteScreen;

	{
		std::lock_guard<std::mutex> lk(m_mutex);
		m_state = info;
	}
}

} // namespace ppt