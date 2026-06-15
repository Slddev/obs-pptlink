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

	QLabel *hdr = new QLabel(obs_module_text("PPT.NotesDock.Title"), this);
	{
		QFont f = hdr->font();
		f.setPointSize(8);
		f.setLetterSpacing(QFont::AbsoluteSpacing, 1.5);
		hdr->setFont(f);
		hdr->setStyleSheet("color: #888;");
	}
	vbox->addWidget(hdr);

	m_notes = new QTextEdit(this);
	m_notes->setReadOnly(true);
	m_notes->setPlaceholderText(obs_module_text("PPT.NotesDock.NoNotes"));
	m_notes->setMinimumHeight(60);
	m_notes->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
	m_notes->setStyleSheet("QTextEdit {"
			       "  background:#1e1e1e; color:#ccc;"
			       "  border:1px solid #333; border-radius:3px;"
			       "  font-size:11px; padding:4px;"
			       "}");
	vbox->addWidget(m_notes, 1);

	QHBoxLayout *btnRow = new QHBoxLayout();
	btnRow->setSpacing(6);

	m_btnPrev = new QPushButton(obs_module_text("PPT.NotesDock.Prev"), this);
	m_btnNext = new QPushButton(obs_module_text("PPT.NotesDock.Next"), this);

	m_btnNext->setDefault(true);
	m_btnNext->setStyleSheet("QPushButton{background:#2d6a9f;color:#fff;border:none;"
				 "border-radius:3px;padding:5px 16px;font-weight:bold;}"
				 "QPushButton:hover{background:#3a7fbc;}"
				 "QPushButton:pressed{background:#1d5a8f;}"
				 "QPushButton:disabled{background:#333;color:#666;}");
	m_btnPrev->setStyleSheet("QPushButton{background:#2a2a2a;color:#ccc;"
				 "border:1px solid #444;border-radius:3px;padding:5px 16px;}"
				 "QPushButton:hover{background:#383838;}"
				 "QPushButton:pressed{background:#1c1c1c;}"
				 "QPushButton:disabled{background:#222;color:#555;border-color:#333;}");

	btnRow->addWidget(m_btnPrev);
	btnRow->addWidget(m_btnNext);
	vbox->addLayout(btnRow);

	connect(m_btnPrev, &QPushButton::clicked, this, &NotesDock::onPrevClicked);
	connect(m_btnNext, &QPushButton::clicked, this, &NotesDock::onNextClicked);
}

void NotesDock::updateFromInfo(const ppt::SlideInfo &info, bool connected)
{
	m_btnPrev->setEnabled(connected);
	m_btnNext->setEnabled(connected);
	m_notes->setEnabled(connected);

	if (!connected) {
		m_notes->clear();
		return;
	}

	QString text;
	if (!info.notes.empty()) {
		size_t len = os_wcs_to_utf8(info.notes.c_str(), info.notes.length(), nullptr, 0);
		std::string utf8(len, '\0');
		os_wcs_to_utf8(info.notes.c_str(), info.notes.length(), &utf8[0], len + 1);
		text = QString::fromUtf8(utf8.c_str());
	}
	m_notes->setPlainText(text);
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