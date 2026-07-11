/* Copyright (c) 2013-2025 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "DisassemblyView.h"
#include "moc_DisassemblyView.cpp"

#include "CoreController.h"
#include "GBAApp.h"
#include "utils.h"

#include <QAction>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>

#include <mgba/internal/debugger/symbols.h>

#ifdef M_CORE_GBA
#include "ArmThumbDecode.h"

#include <mgba/internal/arm/arm.h>
#include <mgba/internal/arm/decoder.h>
#endif

using namespace QGBA;

static const QColor _breakpointLineColor(244, 67, 54, 40);
static const QColor _breakpointDotColor(220, 40, 40);
static const QColor _pcLineColor(66, 133, 244, 70);

static QPointF _mouseEventPos(QMouseEvent* event) {
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
	return event->position();
#else
	return event->localPos();
#endif
}

DisassemblyView::DisassemblyView(QWidget* parent)
	: QAbstractScrollArea(parent)
	, m_font(GBAApp::app()->monospaceFont())
{
	m_font.setStyleHint(QFont::Monospace);
	QFontMetrics metrics(m_font);
	m_lineHeight = metrics.height() + 2;
	m_charWidth = metrics.horizontalAdvance(QChar('0'));

	setFocusPolicy(Qt::StrongFocus);
	setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
	connect(verticalScrollBar(), &QScrollBar::valueChanged, this, &DisassemblyView::scrollValueChanged);
}

void DisassemblyView::setController(std::shared_ptr<CoreController> controller) {
	m_controller = controller;
	m_core = controller->thread()->core;
	m_isGba = false;
#ifdef M_CORE_GBA
	m_isGba = controller->platform() == mPLATFORM_GBA;
#endif
	m_blocks.clear();
	const mCoreMemoryBlock* blocks;
	size_t nBlocks = m_core->listMemoryBlocks(m_core, &blocks);
	for (size_t i = 0; i < nBlocks; ++i) {
		if (!(blocks[i].flags & mCORE_MEMORY_MAPPED)) {
			continue;
		}
		m_blocks.append(blocks[i]);
	}
	refreshAutoMode();
	updateScrollbar();
	viewport()->update();
}

void DisassemblyView::setBreakpoints(const QSet<uint32_t>& breakpoints) {
	m_breakpoints = breakpoints;
	viewport()->update();
}

void DisassemblyView::setAddressFolder(std::function<uint32_t(uint32_t)> folder) {
	m_foldAddress = std::move(folder);
	viewport()->update();
}

void DisassemblyView::jumpToAddress(uint32_t address) {
	setRegion(address);
	m_cursorAddress = address & ~(instructionWidth() - 1);
	m_topAddress = m_cursorAddress;
	int context = visibleLines() / 4;
	uint32_t offset = context * instructionWidth();
	if (m_topAddress - m_regionBase >= offset) {
		m_topAddress -= offset;
	} else {
		m_topAddress = m_regionBase;
	}
	updateScrollbar();
	viewport()->update();
}

void DisassemblyView::jumpToPC(uint32_t pc) {
	m_pcAddress = pc;
	m_pcValid = true;
	refreshAutoMode();
	jumpToAddress(pc);
}

void DisassemblyView::setDisassemblyMode(Mode mode) {
	m_mode = mode;
	refreshAutoMode();
	m_topAddress &= ~(instructionWidth() - 1);
	m_cursorAddress &= ~(instructionWidth() - 1);
	updateScrollbar();
	viewport()->update();
}

bool DisassemblyView::effectiveThumb() const {
	// m_autoThumb is sampled at pause/step, not live: the live mode would reflow the listing every frame
	return m_mode == Mode::THUMB || (m_mode == Mode::AUTO && m_autoThumb);
}

void DisassemblyView::refreshAutoMode() {
#ifdef M_CORE_GBA
	if (m_isGba && m_core && m_controller) {
		CoreController::Interrupter interrupter(m_controller);
		const ARMCore* cpu = static_cast<const ARMCore*>(m_core->cpu);
		m_autoThumb = cpu->executionMode == MODE_THUMB;
	}
#endif
}

int DisassemblyView::instructionWidth() const {
#ifdef M_CORE_GBA
	return effectiveThumb() ? WORD_SIZE_THUMB : WORD_SIZE_ARM;
#else
	return 2;
#endif
}

uint32_t DisassemblyView::instructionSpanEnd(uint32_t address) const {
#ifdef M_CORE_GBA
	if (!effectiveThumb()) {
		return address + (WORD_SIZE_ARM - WORD_SIZE_THUMB);
	}
#endif
	return address;
}

uint32_t DisassemblyView::lineSpanEnd(uint32_t address) {
	// Decode-aware, unlike instructionSpanEnd: a Thumb BL pair line spans 4 bytes
	Line line = disassembleAt(address);
	return line.address + line.width - 2;
}

DisassemblyView::Line DisassemblyView::disassembleAt(uint32_t address) {
	Line line;
	line.width = instructionWidth();
	if (!m_core) {
		line.address = address;
		return line;
	}
#ifdef M_CORE_GBA
	if (m_isGba) {
		char buffer[64]{};
		// Per-frame repaints have no Interrupter: rawRead only, and a null ARMCore* so
		// ARMDisassemble can't dereference literal pools through the bus (I/O, EEPROM, GPIO)
		if (effectiveThumb()) {
			address &= ~static_cast<uint32_t>(WORD_SIZE_THUMB - 1);
			ThumbInstruction instr = decodeThumbAt(m_core, address, static_cast<uint32_t>(qMin<uint64_t>(regionEnd(), UINT32_MAX)));
			ARMDisassemble(&instr.info, nullptr, m_core->symbolTable, address + WORD_SIZE_THUMB * 2, buffer, sizeof(buffer));
			if (instr.isPair) {
				line.width = WORD_SIZE_THUMB * 2;
				line.bytes = formatHex16(instr.opcode) + ' ' + formatHex16(instr.opcode2);
			} else {
				line.width = WORD_SIZE_THUMB;
				line.bytes = formatHex16(instr.opcode);
			}
		} else {
			address &= ~static_cast<uint32_t>(WORD_SIZE_ARM - 1);
			ArmInstruction instr = decodeArmAt(m_core, address);
			ARMDisassemble(&instr.info, nullptr, m_core->symbolTable, address + WORD_SIZE_ARM * 2, buffer, sizeof(buffer));
			line.width = WORD_SIZE_ARM;
			line.bytes = formatHex32(instr.opcode);
		}
		line.disassembly = QString::fromUtf8(buffer);
	} else
#endif
	{
		line.disassembly = tr("(disassembly unavailable)");
	}
	line.address = address;
	if (m_core->symbolTable) {
		const char* name = mDebuggerSymbolReverseLookup(m_core->symbolTable, address, -1);
		if (name) {
			line.symbol = QString::fromUtf8(name);
		}
	}
	return line;
}

uint32_t DisassemblyView::advance(uint32_t address) {
	Line line = disassembleAt(address);
	return line.address + line.width;
}

uint32_t DisassemblyView::retreat(uint32_t address) {
	uint32_t width = instructionWidth();
	address &= ~(width - 1);
	if (address < width) {
		return address;
	}
#ifdef M_CORE_GBA
	if (effectiveThumb() && address >= WORD_SIZE_THUMB * 2) {
		uint32_t pairAddress = address - WORD_SIZE_THUMB * 2;
		if (pairAddress >= m_regionBase) {
			Line line = disassembleAt(pairAddress);
			if (line.width == WORD_SIZE_THUMB * 2) {
				return line.address;
			}
		}
	}
#endif
	return address - width;
}

uint32_t DisassemblyView::regionAdvance(uint32_t address) {
	// Crossing a region end would re-base the view onto an unrelated block
	uint32_t next = advance(address);
	if (next <= address || (m_regionSize && next >= regionEnd())) {
		return address;
	}
	return next;
}

uint32_t DisassemblyView::regionRetreat(uint32_t address) {
	uint32_t prev = retreat(address);
	if (prev >= address || prev < m_regionBase) {
		return address;
	}
	return prev;
}

int DisassemblyView::visibleLines() const {
	int lines = viewport()->height() / m_lineHeight;
	if (lines < 1) {
		lines = 1;
	}
	return lines;
}

uint32_t DisassemblyView::addressAtY(int y) {
	int row = y / m_lineHeight;
	uint32_t address = m_topAddress;
	for (int i = 0; i < row; ++i) {
		address = advance(address);
	}
	return address;
}

void DisassemblyView::updateScrollbar() {
	m_inScrollUpdate = true;
	int width = instructionWidth();
	int max = m_regionSize / width - 1;
	verticalScrollBar()->setRange(0, max);
	verticalScrollBar()->setPageStep(visibleLines());
	verticalScrollBar()->setSingleStep(1);
	verticalScrollBar()->setValue((m_topAddress - m_regionBase) / width);
	m_inScrollUpdate = false;
}

void DisassemblyView::scrollValueChanged(int value) {
	if (m_inScrollUpdate) {
		return;
	}
	int width = instructionWidth();
	m_topAddress = m_regionBase + static_cast<uint32_t>(value) * width;
	viewport()->update();
}

void DisassemblyView::setRegion(uint32_t address) {
	uint32_t oldBase = m_regionBase;
	m_regionBase = address & ~0xFFFFu;
	m_regionSize = 0x10000;
	for (const mCoreMemoryBlock& block : m_blocks) {
		if (address >= block.start && address < block.end) {
			m_regionBase = block.start;
			m_regionSize = block.end - block.start;
			break;
		}
	}
	if (m_regionBase != oldBase) {
		emit regionChanged(m_regionBase);
	}
}

void DisassemblyView::ensureCursorVisible() {
	if (m_cursorAddress < m_regionBase || m_cursorAddress >= m_regionBase + m_regionSize) {
		setRegion(m_cursorAddress);
		m_topAddress = m_cursorAddress;
		updateScrollbar();
		return;
	}
	if (m_cursorAddress < m_topAddress) {
		m_topAddress = m_cursorAddress;
		updateScrollbar();
		return;
	}
	uint32_t address = m_topAddress;
	int lines = visibleLines();
	for (int i = 0; i < lines; ++i) {
		if (address == m_cursorAddress) {
			return;
		}
		address = advance(address);
	}
	uint32_t width = instructionWidth();
	uint32_t top = m_cursorAddress - qMin<uint32_t>(m_cursorAddress - m_regionBase, (lines - 1) * width);
	m_topAddress = top;
	updateScrollbar();
}

void DisassemblyView::moveCursor(uint32_t address) {
	m_cursorAddress = address;
	ensureCursorVisible();
	viewport()->update();
}

void DisassemblyView::resizeEvent(QResizeEvent*) {
	updateScrollbar();
}

void DisassemblyView::paintEvent(QPaintEvent*) {
	QPainter painter(viewport());
	painter.setFont(m_font);
	QPalette palette = viewport()->palette();
	painter.fillRect(viewport()->rect(), palette.base());

	const int gutter = gutterWidth();
	const int addressX = gutter + m_charWidth;
	const int bytesX = addressX + m_charWidth * 10;
	const int disasmX = bytesX + m_charWidth * 11;
	const int ascent = QFontMetrics(m_font).ascent();

	uint32_t address = m_topAddress;
	int lines = visibleLines();
	for (int row = 0; row < lines; ++row) {
		if (address >= m_regionBase + m_regionSize) {
			break;
		}
		Line line = disassembleAt(address);
		int y = row * m_lineHeight;
		QRect lineRect(0, y, viewport()->width(), m_lineHeight);

		// Probe every halfword in the line span: a single bp can sit at +2 inside an ARM-view word line
		bool lineBreakpoint = false;
		for (uint32_t probe = line.address; probe < line.address + static_cast<uint32_t>(line.width); probe += 2) {
			if (m_breakpoints.contains(m_foldAddress ? m_foldAddress(probe) : probe)) {
				lineBreakpoint = true;
				break;
			}
		}
		if (lineBreakpoint) {
			painter.fillRect(lineRect, _breakpointLineColor);
		}
		// A Thumb PC can sit at +2 inside an ARM-view word line
		bool linePc = m_pcValid && m_pcAddress >= line.address && m_pcAddress < line.address + static_cast<uint32_t>(line.width);
		if (linePc) {
			painter.fillRect(lineRect, _pcLineColor);
		}
		if (line.address == m_cursorAddress) {
			painter.setPen(QPen(palette.color(QPalette::Highlight), 1));
			painter.drawRect(lineRect.adjusted(0, 0, -1, -1));
		}

		if (lineBreakpoint) {
			painter.setPen(Qt::NoPen);
			painter.setBrush(_breakpointDotColor);
			int diameter = m_lineHeight / 2;
			painter.drawEllipse(QPoint(gutter / 2, y + m_lineHeight / 2), diameter / 2, diameter / 2);
			painter.setBrush(Qt::NoBrush);
		}
		if (linePc) {
			painter.setPen(palette.color(QPalette::Text));
			painter.drawText(QRect(0, y, gutter, m_lineHeight), Qt::AlignCenter, QStringLiteral("▶"));
		}

		painter.setPen(palette.color(QPalette::Text));
		painter.drawText(addressX, y + ascent + 1, formatHex32(line.address) + ':');
		painter.setPen(palette.color(QPalette::PlaceholderText));
		painter.drawText(bytesX, y + ascent + 1, line.bytes);
		painter.setPen(palette.color(QPalette::Text));
		QString text = line.disassembly;
		if (!line.symbol.isEmpty()) {
			text += QStringLiteral("  ; ") + line.symbol;
		}
		painter.drawText(disasmX, y + ascent + 1, text);

		address = line.address + line.width;
	}
}

void DisassemblyView::mousePressEvent(QMouseEvent* event) {
	if (event->button() != Qt::LeftButton) {
		QAbstractScrollArea::mousePressEvent(event);
		return;
	}
	uint32_t address = addressAtY(static_cast<int>(_mouseEventPos(event).y()));
	if (_mouseEventPos(event).x() < gutterWidth()) {
		emit breakpointToggleRequested(address);
	} else {
		moveCursor(address);
	}
}

void DisassemblyView::mouseDoubleClickEvent(QMouseEvent* event) {
	if (event->button() != Qt::LeftButton) {
		return;
	}
	uint32_t address = addressAtY(static_cast<int>(_mouseEventPos(event).y()));
	emit breakpointToggleRequested(address);
}

void DisassemblyView::keyPressEvent(QKeyEvent* event) {
	switch (event->key()) {
	case Qt::Key_Up:
		moveCursor(regionRetreat(m_cursorAddress));
		break;
	case Qt::Key_Down:
		moveCursor(regionAdvance(m_cursorAddress));
		break;
	case Qt::Key_PageUp:
	case Qt::Key_PageDown: {
		bool up = event->key() == Qt::Key_PageUp;
		uint32_t address = m_cursorAddress;
		for (int i = 0; i < visibleLines(); ++i) {
			address = up ? regionRetreat(address) : regionAdvance(address);
		}
		moveCursor(address);
		break;
	}
	case Qt::Key_Home:
		if (m_pcValid) {
			jumpToAddress(m_pcAddress);
		}
		break;
	default:
		QAbstractScrollArea::keyPressEvent(event);
		break;
	}
}

void DisassemblyView::contextMenuEvent(QContextMenuEvent* event) {
	uint32_t address = addressAtY(event->pos().y());
	m_cursorAddress = address;
	viewport()->update();

	QMenu menu(this);
	QAction* toggleBreak = menu.addAction(tr("Toggle &breakpoint"));
	connect(toggleBreak, &QAction::triggered, this, [this, address]() {
		emit breakpointToggleRequested(address);
	});
	QAction* runTo = menu.addAction(tr("&Run to here"));
	connect(runTo, &QAction::triggered, this, [this, address]() {
		emit runToRequested(address);
	});
	QAction* setPC = menu.addAction(tr("Set &PC here"));
	connect(setPC, &QAction::triggered, this, [this, address]() {
		emit setPCRequested(address);
	});
	menu.addSeparator();
	QAction* gotoPC = menu.addAction(tr("&Go to PC"));
	gotoPC->setEnabled(m_pcValid);
	connect(gotoPC, &QAction::triggered, this, [this]() {
		jumpToAddress(m_pcAddress);
	});
	menu.exec(event->globalPos());
}
