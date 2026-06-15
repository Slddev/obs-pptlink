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

#include <obs-module.h>
#include <obs-frontend-api.h>
#include "../com-bridge/ppt-com-bridge.h"
#include "dock-notes.h"

#include <QWidget>
#include <QLabel>
#include <QTimer>
#include <QVBoxLayout>
#include <QFrame>

namespace dock {

class NextSlidePreviewLabel : public QLabel {
	Q_OBJECT
public:
	explicit NextSlidePreviewLabel(QWidget *parent = nullptr);
	void setSlidePixmap(const QPixmap &px);

protected:
	void resizeEvent(QResizeEvent *) override;
	void paintEvent(QPaintEvent *) override;

private:
	void updateScaled();
	QPixmap m_source;
};

class NextSlideDock : public QWidget {
	Q_OBJECT
public:
	explicit NextSlideDock(ppt::ComBridge *bridge, QWidget *parent = nullptr);
	~NextSlideDock();

	void setNotesDock(NotesDock *notes) { m_notesDock = notes; }

private slots:
	void onPollTimer();

private:
	void buildUi();
	void loadThumbnail(int currentSlide, int totalSlides);
	void applyConnectedState(bool connected);

	ppt::ComBridge *m_bridge = nullptr;
	QTimer *m_timer = nullptr;
	NextSlidePreviewLabel *m_preview = nullptr;
	QLabel *m_noSignal = nullptr;
	QLabel *m_counter = nullptr;
	NotesDock *m_notesDock = nullptr;

	bool m_lastConnected = false;
	int m_lastSlide = -1;
	int m_lastTotal = -1;
	QString m_lastThumbPath;
};

void RegisterDocks(ppt::ComBridge *bridge);

} // namespace dock