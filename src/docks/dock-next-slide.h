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

#pragma once

#include <obs-module.h>
#include <obs-frontend-api.h>
#include "../com-bridge/ppt-com-bridge.h"

#include <QWidget>
#include <QLabel>
#include <QTimer>
#include <QTextEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>

namespace dock {

// ── Shared preview label (unchanged) ────────────────────────
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

// ── Dock 1: preview + counter ────────────────────────────────
class NextSlideDock : public QWidget {
	Q_OBJECT
public:
	explicit NextSlideDock(ppt::ComBridge *bridge, QWidget *parent = nullptr);
	~NextSlideDock();

	// Called by NotesDock so both docks share one timer
	void poll();

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

	bool m_lastConnected = false;
	int m_lastSlide = -1;
	int m_lastTotal = -1;
	QString m_lastThumbPath;
};

// ── Dock 2: notes + navigation buttons ──────────────────────
class NotesDock : public QWidget {
	Q_OBJECT
public:
	explicit NotesDock(ppt::ComBridge *bridge, QWidget *parent = nullptr);
	~NotesDock();

	void updateFromInfo(const ppt::SlideInfo &info, bool connected);

private slots:
	void onPrevClicked();
	void onNextClicked();

private:
	void buildUi();

	ppt::ComBridge *m_bridge = nullptr;
	QTextEdit *m_notes = nullptr;
	QPushButton *m_btnPrev = nullptr;
	QPushButton *m_btnNext = nullptr;
};

// ── Registration ─────────────────────────────────────────────
void RegisterDocks(ppt::ComBridge *bridge);

} // namespace dock