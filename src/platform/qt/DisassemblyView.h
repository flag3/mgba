/* Copyright (c) 2013-2025 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#pragma once

#include <QAbstractScrollArea>
#include <QFont>
#include <QSet>
#include <QVector>

#include <functional>
#include <memory>

#include <mgba/core/core.h>

namespace QGBA {

class CoreController;

class DisassemblyView : public QAbstractScrollArea {
Q_OBJECT

public:
	enum class Mode {
		AUTO = 0,
		ARM = 1,
		THUMB = 2,
	};

	DisassemblyView(QWidget* parent = nullptr);

	void setController(std::shared_ptr<CoreController> controller);

	uint32_t cursorAddress() const { return m_cursorAddress; }
	uint32_t regionBase() const { return m_regionBase; }
	// Inclusive span end: the 4-byte word in ARM mode, the address itself in Thumb
	uint32_t instructionSpanEnd(uint32_t address) const;
	// Decode-aware inclusive end of the listing line (covers a Thumb BL pair)
	uint32_t lineSpanEnd(uint32_t address);
	// The set must hold folded addresses; lines are folded before lookup
	void setBreakpoints(const QSet<uint32_t>& breakpoints);
	void setAddressFolder(std::function<uint32_t(uint32_t)> folder);
	void setDisassemblyMode(Mode mode);

public slots:
	void jumpToAddress(uint32_t address);
	void jumpToPC(uint32_t pc);

signals:
	void breakpointToggleRequested(uint32_t address);
	void runToRequested(uint32_t address);
	void setPCRequested(uint32_t address);
	void regionChanged(uint32_t base);

protected:
	void resizeEvent(QResizeEvent*) override;
	void paintEvent(QPaintEvent*) override;
	void mousePressEvent(QMouseEvent*) override;
	void mouseDoubleClickEvent(QMouseEvent*) override;
	void keyPressEvent(QKeyEvent*) override;
	void contextMenuEvent(QContextMenuEvent*) override;

private slots:
	void scrollValueChanged(int value);

private:
	struct Line {
		uint32_t address = 0;
		int width = 4;
		QString bytes;
		QString disassembly;
		QString symbol;
	};

	bool effectiveThumb() const;
	void refreshAutoMode();
	int instructionWidth() const;
	int gutterWidth() const { return m_lineHeight + 4; }
	// 64-bit: the 64KiB fallback region at 0xFFFF0000 would wrap a 32-bit end to 0
	uint64_t regionEnd() const { return static_cast<uint64_t>(m_regionBase) + m_regionSize; }
	Line disassembleAt(uint32_t address);
	uint32_t advance(uint32_t address);
	uint32_t retreat(uint32_t address);
	uint32_t regionAdvance(uint32_t address);
	uint32_t regionRetreat(uint32_t address);
	int visibleLines() const;
	uint32_t addressAtY(int y);
	void updateScrollbar();
	void ensureCursorVisible();
	void setRegion(uint32_t address);
	void moveCursor(uint32_t address);

	std::shared_ptr<CoreController> m_controller;
	mCore* m_core = nullptr;
	bool m_isGba = false;
	QFont m_font;
	int m_lineHeight = 14;
	int m_charWidth = 8;
	// GBA ROM0 defaults; replaced by setController/setRegion
	static constexpr uint32_t DEFAULT_REGION_BASE = 0x08000000;
	static constexpr uint32_t DEFAULT_REGION_SIZE = 0x02000000;

	uint32_t m_topAddress = DEFAULT_REGION_BASE;
	uint32_t m_cursorAddress = DEFAULT_REGION_BASE;
	uint32_t m_pcAddress = 0;
	bool m_pcValid = false;
	Mode m_mode = Mode::AUTO;
	bool m_autoThumb = false;
	QSet<uint32_t> m_breakpoints;
	std::function<uint32_t(uint32_t)> m_foldAddress;
	QVector<mCoreMemoryBlock> m_blocks;
	uint32_t m_regionBase = DEFAULT_REGION_BASE;
	uint32_t m_regionSize = DEFAULT_REGION_SIZE;
	bool m_inScrollUpdate = false;
};

}
