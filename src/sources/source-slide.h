#pragma once

#include <obs-module.h>
#include "../wgc-capture/wgc-capture.h"
#include "../com-bridge/ppt-com-bridge.h"
#include <atomic>

namespace sources {

void RegisterSlideSource();

struct SlideSource {
	obs_source_t *source = nullptr;
	obs_hotkey_id hotkeyNext = OBS_INVALID_HOTKEY_ID;
	obs_hotkey_id hotkeyPrev = OBS_INVALID_HOTKEY_ID;

	wgc::CaptureSession capture;
	ppt::ComBridge *bridge = nullptr;
	gs_texture_t *texture = nullptr;

	bool showBorder = false;
	HWND lastHwnd = nullptr;
	float pollAccum = 0.5f;

	std::atomic<bool> slideshowActive{false};

	void TryStartCapture();
	void RenderFrame(gs_effect_t *effect);
};

} // namespace sources