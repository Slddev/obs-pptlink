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

#include <functional>

#ifndef NOMINMAX
#define NOMINMAX
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <string>
#include <atomic>
#include <thread>
#include <mutex>
#include <deque>

// Forward-declare PowerPoint COM types rather than importing the full
// typelib, so the header compiles without Office installed.
// The .cpp uses #import or raw IDispatch.
struct IDispatch;
struct IConnectionPoint;

namespace ppt {

class AppEventSink;

// ------------------------------------------------------------------
// SlideInfo -- snapshot of current PPT state
// ------------------------------------------------------------------
struct SlideInfo {
	int currentSlide = 0; // 1-based, 0 = not in slideshow
	int totalSlides = 0;
	int nextSlide = 0;  // 1-based next visible slide, 0 = none
	std::wstring notes; // current slide speaker notes (plain text)
	HWND slideshowHwnd = nullptr;
	bool slideshowActive = false;
};

// Path of the next-slide thumbnail PNG exported on every slide change.
std::wstring GetNextSlideThumbPath();

// ------------------------------------------------------------------
// ComBridge
// ------------------------------------------------------------------
class ComBridge {
public:
	ComBridge();
	~ComBridge();

	// -- Lifecycle --

	// Starts the STA thread and begins polling for a running PPT.
	bool Start();

	// Stops polling and releases COM.
	void Stop();

	// -- State access (thread-safe) --
	SlideInfo GetSlideInfo() const;
	bool IsConnected() const { return m_connected.load(); }
	// Slide aspect ratio (width/height) read from PageSetup.
	// Returns 16/9 until the bridge connects and reads a presentation.
	float GetSlideAspect() const { return m_slideAspect.load(); }

	// -- Control (posted to STA thread) --
	void NextSlide();
	void PrevSlide();
	void GotoSlide(int oneBasedIndex);
	void EndShow();

	// -- Callbacks (called from STA thread; be fast / non-blocking) --
	// Fired when connection to PPT is established or lost.
	std::function<void(bool connected)> OnConnectionChanged;

private:
	// STA thread entry
	void ThreadProc();

	// COM operations (all called on STA thread)
	bool TryConnect();
	void Disconnect();
	void PollState();
	std::wstring GetNotesText(IDispatch *pSlide);
	bool AdviseEvents();
	void UnadviseEvents();
	void RunCommands();
	void WaitForWork(DWORD timeoutMs);
	void PumpFor(DWORD durationMs);

	// Command queue for cross-thread calls
	enum class CmdType { None, Next, Prev, Goto, End };
	struct Command {
		CmdType type = CmdType::None;
		int gotoIndex = 0;
	};

	void PostCommand(Command cmd);

	// Thread
	std::thread m_thread;
	std::atomic<bool> m_stopRequested{false};
	HANDLE m_wakeEvent = nullptr;

	// Slide aspect ratio (w/h), updated from PageSetup on every PollState.
	// Atomic float so GetSlideAspect() needs no lock.
	std::atomic<float> m_slideAspect{16.0f / 9.0f};

	// State (protected by m_mutex)
	mutable std::mutex m_mutex;
	SlideInfo m_state;
	std::atomic<bool> m_connected{false};

	ULONGLONG m_firstFailureTick = 0;
	int m_lastExportedIndex = -1;
	ULONGLONG m_lastPollTick = 0;

	// Command queue
	std::mutex m_cmdMutex;
	std::deque<Command> m_pendingCmds;

	// COM objects (only touched on STA thread)
	IDispatch *m_pApp = nullptr;  // PowerPoint.Application
	IDispatch *m_pShow = nullptr; // SlideShowWindow
	IDispatch *m_pView = nullptr; // SlideShowView

	// EApplication event subscription (only touched on STA thread)
	AppEventSink *m_pSink = nullptr;
	IConnectionPoint *m_pConnPoint = nullptr;
	DWORD m_adviseCookie = 0;
};

} // namespace ppt