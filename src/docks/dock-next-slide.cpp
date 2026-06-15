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

#include "dock-next-slide.h"
#include "dock-notes.h"
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <QPainter>
#include <QResizeEvent>
#include <QFont>
#include <windows.h>

namespace dock {

NextSlidePreviewLabel::NextSlidePreviewLabel(QWidget *parent) : QLabel(parent)
{
	setAlignment(Qt::AlignCenter);
	setMinimumSize(160, 90);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	setStyleSheet("background: #000;");
}

void NextSlidePreviewLabel::setSlidePixmap(const QPixmap &px)
{
	m_source = px;
	updateScaled();
	update();
}

void NextSlidePreviewLabel::resizeEvent(QResizeEvent *e)
{
	QLabel::resizeEvent(e);
	updateScaled();
}

void NextSlidePreviewLabel::updateScaled()
{
	if (m_source.isNull())
		return;
	QLabel::setPixmap(m_source.scaled(size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void NextSlidePreviewLabel::paintEvent(QPaintEvent *e)
{
	if (m_source.isNull()) {
		QPainter p(this);
		p.fillRect(rect(), QColor(0, 0, 0));
		p.setPen(QColor(80, 80, 80));
		p.setFont(QFont("Arial", 10));
		p.drawText(rect(), Qt::AlignCenter, tr("No Preview"));
		return;
	}
	QLabel::paintEvent(e);
}

NextSlideDock::NextSlideDock(ppt::ComBridge *bridge, QWidget *parent) : QWidget(parent), m_bridge(bridge)
{
	setObjectName("PPTNextSlideDock");
	buildUi();

	m_timer = new QTimer(this);
	connect(m_timer, &QTimer::timeout, this, &NextSlideDock::onPollTimer);
	m_timer->start(200);
}

NextSlideDock::~NextSlideDock()
{
	m_timer->stop();
}

void NextSlideDock::buildUi()
{
	QVBoxLayout *vbox = new QVBoxLayout(this);
	vbox->setContentsMargins(6, 6, 6, 6);
	vbox->setSpacing(6);

	QLabel *upNext = new QLabel(obs_module_text("PPT.NextSlideDock.UpNext"), this);
	{
		QFont f = upNext->font();
		f.setPointSize(8);
		f.setLetterSpacing(QFont::AbsoluteSpacing, 1.5);
		upNext->setFont(f);
		upNext->setStyleSheet("color: #888;");
	}
	vbox->addWidget(upNext);

	m_preview = new NextSlidePreviewLabel(this);
	m_preview->setMinimumHeight(120);
	m_preview->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	vbox->addWidget(m_preview, 3);

	m_noSignal = new QLabel(obs_module_text("PPT.NextSlideDock.NoSignal"), this);
	m_noSignal->setAlignment(Qt::AlignCenter);
	m_noSignal->setStyleSheet("color:#888; background:#1a1a1a;"
				  "border:1px solid #333; border-radius:4px; padding:8px;");
	m_noSignal->setVisible(false);
	vbox->addWidget(m_noSignal);

	QFrame *div = new QFrame(this);
	div->setFrameShape(QFrame::HLine);
	div->setStyleSheet("color:#333;");
	vbox->addWidget(div);

	m_counter = new QLabel("—", this);
	m_counter->setAlignment(Qt::AlignCenter);
	{
		QFont f = m_counter->font();
		f.setPointSize(11);
		f.setBold(true);
		m_counter->setFont(f);
	}
	vbox->addWidget(m_counter);

	applyConnectedState(false);
}

void NextSlideDock::onPollTimer()
{
	if (!m_bridge)
		return;

	bool connected = m_bridge->IsConnected();
	if (connected != m_lastConnected) {
		m_lastConnected = connected;
		applyConnectedState(connected);
	}
	if (!connected) {
		if (m_notesDock)
			m_notesDock->updateFromInfo({}, false);
		return;
	}

	ppt::SlideInfo info = m_bridge->GetSlideInfo();

	if (m_notesDock)
		m_notesDock->updateFromInfo(info, true);

	if (info.currentSlide != m_lastSlide || info.totalSlides != m_lastTotal) {
		m_lastSlide = info.currentSlide;
		m_lastTotal = info.totalSlides;

		if (info.currentSlide > 0) {
			int next = info.currentSlide + 1;
			if (next <= info.totalSlides)
				m_counter->setText(QString("Next: Slide %1  /  %2").arg(next).arg(info.totalSlides));
			else
				m_counter->setText(QString("Last slide  (%1 / %1)").arg(info.totalSlides));
		} else {
			m_counter->setText("—");
		}

		loadThumbnail(info.currentSlide, info.totalSlides);
	}
}

void NextSlideDock::loadThumbnail(int currentSlide, int totalSlides)
{
	if (currentSlide <= 0) {
		m_preview->setSlidePixmap(QPixmap());
		return;
	}

	wchar_t tempW[MAX_PATH] = {};
	GetTempPathW(MAX_PATH, tempW);
	QString path = QString::fromWCharArray(tempW) + "obs_ppt_next.png";
	m_lastThumbPath = path;
	QPixmap px(path);
	m_preview->setSlidePixmap(px.isNull() ? QPixmap() : px);
	(void)totalSlides;
}

void NextSlideDock::applyConnectedState(bool connected)
{
	m_preview->setVisible(connected);
	m_noSignal->setVisible(!connected);
	m_counter->setEnabled(connected);
	if (!connected) {
		m_counter->setText("—");
		m_lastSlide = m_lastTotal = -1;
		m_lastThumbPath.clear();
	}
}

void RegisterDocks(ppt::ComBridge *bridge)
{
	obs_frontend_push_ui_translation(obs_module_get_string);

	auto *notesDock = new NotesDock(bridge);
	auto *nextDock = new NextSlideDock(bridge);
	nextDock->setNotesDock(notesDock);

	obs_frontend_add_dock_by_id("PPTNextSlideDock", obs_module_text("PPT.NextSlideDock.Title"), nextDock);
	obs_frontend_add_dock_by_id("PPTNotesDock", obs_module_text("PPT.NotesDock.Title"), notesDock);

	obs_frontend_pop_ui_translation();

	blog(LOG_INFO, "[obs-pptlink] Registered docks: PPTNextSlideDock, PPTNotesDock");
}

} // namespace dock