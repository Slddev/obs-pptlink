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

#include "dock-notes.h"
#include <obs-module.h>
#include <util/platform.h>
#include <QSignalBlocker>
#include <algorithm>

namespace dock {

NotesDock::NotesDock(ppt::ComBridge *bridge, QWidget *parent) : QWidget(parent), m_bridge(bridge)
{
	setObjectName("PPTNotesDock");
	buildUi();
}

NotesDock::~NotesDock() {}

void NotesDock::buildUi()
{
	QVBoxLayout *vbox = new QVBoxLayout(this);
	vbox->setContentsMargins(6, 6, 6, 6);
	vbox->setSpacing(6);

	m_showRow = new QWidget(this);
	QHBoxLayout *showLayout = new QHBoxLayout(m_showRow);
	showLayout->setContentsMargins(0, 0, 0, 0);
	showLayout->setSpacing(6);
	showLayout->addWidget(new QLabel(obs_module_text("PPT.NotesDock.Presentation"), m_showRow));
	m_showPicker = new QComboBox(m_showRow);
	m_showPicker->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	showLayout->addWidget(m_showPicker, 1);
	m_showRow->setVisible(false);
	vbox->addWidget(m_showRow);
	connect(m_showPicker, &QComboBox::currentIndexChanged, this, &NotesDock::onShowSelected);

	QLabel *hdr = new QLabel(obs_module_text("PPT.NotesDock.Title"), this);
	{
		QFont f = hdr->font();
		f.setPointSize(8);
		f.setLetterSpacing(QFont::AbsoluteSpacing, 1.5);
		hdr->setFont(f);
		hdr->setProperty("class", "text-muted");
	}
	vbox->addWidget(hdr);

	m_notes = new QTextEdit(this);
	m_notes->setReadOnly(true);
	m_notes->setPlaceholderText(obs_module_text("PPT.NotesDock.NoNotes"));
	m_notes->setMinimumHeight(60);
	m_notes->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
	vbox->addWidget(m_notes, 1);

	QHBoxLayout *btnRow = new QHBoxLayout();
	btnRow->setSpacing(6);

	m_btnFirst = new QPushButton(obs_module_text("PPT.NotesDock.First"), this);
	m_btnPrev = new QPushButton(obs_module_text("PPT.NotesDock.Prev"), this);
	m_btnNext = new QPushButton(obs_module_text("PPT.NotesDock.Next"), this);
	m_btnLast = new QPushButton(obs_module_text("PPT.NotesDock.Last"), this);

	m_btnNext->setDefault(true);

	btnRow->addWidget(m_btnFirst);
	btnRow->addWidget(m_btnPrev);
	btnRow->addWidget(m_btnNext);
	btnRow->addWidget(m_btnLast);
	vbox->addLayout(btnRow);

	QHBoxLayout *gotoRow = new QHBoxLayout();
	gotoRow->setSpacing(6);

	m_gotoSlide = new QSpinBox(this);
	m_gotoSlide->setRange(1, 1);
	m_gotoSlide->setPrefix(QString(obs_module_text("PPT.NotesDock.GoTo")) + " ");
	m_btnGoto = new QPushButton(obs_module_text("PPT.NotesDock.Go"), this);

	gotoRow->addWidget(m_gotoSlide, 1);
	gotoRow->addWidget(m_btnGoto);
	vbox->addLayout(gotoRow);

	QHBoxLayout *screenRow = new QHBoxLayout();
	screenRow->setSpacing(6);

	m_btnBlack = new QPushButton(obs_module_text("PPT.NotesDock.Black"), this);
	m_btnWhite = new QPushButton(obs_module_text("PPT.NotesDock.White"), this);
	m_btnBlack->setCheckable(true);
	m_btnWhite->setCheckable(true);

	screenRow->addWidget(m_btnBlack);
	screenRow->addWidget(m_btnWhite);
	vbox->addLayout(screenRow);

	connect(m_btnFirst, &QPushButton::clicked, this, [this]() {
		if (m_bridge)
			m_bridge->FirstSlide();
	});
	connect(m_btnPrev, &QPushButton::clicked, this, &NotesDock::onPrevClicked);
	connect(m_btnNext, &QPushButton::clicked, this, &NotesDock::onNextClicked);
	connect(m_btnLast, &QPushButton::clicked, this, [this]() {
		if (m_bridge)
			m_bridge->LastSlide();
	});
	connect(m_btnGoto, &QPushButton::clicked, this, &NotesDock::onGotoClicked);
	connect(m_gotoSlide, &QSpinBox::editingFinished, this, [this]() {
		if (m_gotoSlide->hasFocus())
			onGotoClicked();
	});
	connect(m_btnBlack, &QPushButton::clicked, this, [this]() {
		if (m_bridge)
			m_bridge->ToggleBlackScreen();
	});
	connect(m_btnWhite, &QPushButton::clicked, this, [this]() {
		if (m_bridge)
			m_bridge->ToggleWhiteScreen();
	});
}

void NotesDock::updateFromInfo(const ppt::SlideInfo &info, bool connected)
{
	updateShows(info);

	for (QWidget *w : std::initializer_list<QWidget *>{m_btnFirst, m_btnPrev, m_btnNext, m_btnLast, m_gotoSlide,
							   m_btnGoto, m_btnBlack, m_btnWhite, m_notes})
		w->setEnabled(connected);

	{
		QSignalBlocker blockBlack(m_btnBlack);
		QSignalBlocker blockWhite(m_btnWhite);
		m_btnBlack->setChecked(connected && info.blackScreen);
		m_btnWhite->setChecked(connected && info.whiteScreen);
	}

	if (connected && info.totalSlides != m_shownTotal) {
		m_shownTotal = info.totalSlides;
		m_gotoSlide->setRange(1, std::max(1, info.totalSlides));
		m_gotoSlide->setSuffix(QString(" / %1").arg(info.totalSlides));
	}
	if (connected && info.currentSlide > 0 && !m_gotoSlide->hasFocus())
		m_gotoSlide->setValue(info.currentSlide);

	if (!connected) {
		if (!m_lastNotes.empty()) {
			m_lastNotes.clear();
			m_notes->clear();
		}
		return;
	}

	if (info.notes == m_lastNotes)
		return;
	m_lastNotes = info.notes;

	QString text;
	if (!info.notes.empty()) {
		size_t len = os_wcs_to_utf8(info.notes.c_str(), info.notes.length(), nullptr, 0);
		std::string utf8(len, '\0');
		os_wcs_to_utf8(info.notes.c_str(), info.notes.length(), &utf8[0], len + 1);
		text = QString::fromUtf8(utf8.c_str());
	}
	m_notes->setPlainText(text);
}

void NotesDock::updateShows(const ppt::SlideInfo &info)
{
	if (!m_bridge)
		return;

	std::vector<ppt::ShowEntry> shows = m_bridge->GetShows();
	std::wstring preferred = m_bridge->GetPreferredShow();
	if (shows == m_shownShows && preferred == m_shownPreferred && info.presentation == m_shownCurrent)
		return;
	m_shownShows = shows;
	m_shownPreferred = preferred;
	m_shownCurrent = info.presentation;

	QSignalBlocker block(m_showPicker);
	m_showPicker->clear();

	if (preferred.empty() && !info.presentation.empty())
		m_showPicker->addItem(QString(obs_module_text("PPT.NotesDock.AutomaticCurrent"))
					      .arg(QString::fromStdWString(info.presentation)),
				      QString());
	else
		m_showPicker->addItem(obs_module_text("PPT.NotesDock.Automatic"), QString());

	bool preferredRunning = preferred.empty();
	for (const ppt::ShowEntry &show : shows) {
		QString fullName = QString::fromStdWString(show.fullName);
		m_showPicker->addItem(QString::fromStdWString(show.name), fullName);
		m_showPicker->setItemData(m_showPicker->count() - 1, fullName, Qt::ToolTipRole);
		if (show.fullName == preferred) {
			m_showPicker->setCurrentIndex(m_showPicker->count() - 1);
			preferredRunning = true;
		}
	}

	if (!preferredRunning) {
		QString fullName = QString::fromStdWString(preferred);
		QString name = fullName.mid(std::max(fullName.lastIndexOf('\\'), fullName.lastIndexOf('/')) + 1);
		m_showPicker->addItem(QString(obs_module_text("PPT.NotesDock.NotRunning")).arg(name), fullName);
		m_showPicker->setItemData(m_showPicker->count() - 1, fullName, Qt::ToolTipRole);
		m_showPicker->setCurrentIndex(m_showPicker->count() - 1);
	}

	m_showRow->setVisible(shows.size() > 1 || !preferred.empty());
}

void NotesDock::onShowSelected(int index)
{
	if (m_bridge && index >= 0)
		m_bridge->SetPreferredShow(m_showPicker->itemData(index).toString().toStdWString());
}

void NotesDock::onGotoClicked()
{
	if (m_bridge)
		m_bridge->GotoSlide(m_gotoSlide->value());
}

void NotesDock::onPrevClicked()
{
	if (m_bridge)
		m_bridge->PrevSlide();
}

void NotesDock::onNextClicked()
{
	if (m_bridge)
		m_bridge->NextSlide();
}

} // namespace dock