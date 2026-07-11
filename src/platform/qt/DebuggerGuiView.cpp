/* Copyright (c) 2013-2025 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "DebuggerGuiView.h"
#include "moc_DebuggerGuiView.cpp"

#include "BreakpointPanel.h"
#include "CoreController.h"
#include "DebuggerGuiController.h"
#include "DisassemblyView.h"
#include "GBAApp.h"
#include "MemoryModel.h"
#include "RegisterPanel.h"
#include "utils.h"

#include <QAction>
#include <QComboBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QToolBar>
#include <QVBoxLayout>

#include <mgba/core/core.h>

#ifdef M_CORE_GBA
#include <mgba/internal/arm/arm.h>
#endif

using namespace QGBA;

enum {
	STACK_ROWS = 64
};

DebuggerGuiView::DebuggerGuiView(DebuggerGuiController* debugger, std::shared_ptr<CoreController> controller,
                                 QWidget* parent)
	: QWidget(parent)
	, m_debugger(debugger)
	, m_controller(controller)
{
	setWindowTitle(tr("Debugger"));
	resize(1100, 720);

	QVBoxLayout* mainLayout = new QVBoxLayout(this);
	mainLayout->setContentsMargins(4, 4, 4, 4);
	mainLayout->setSpacing(4);

	mainLayout->addWidget(createToolBar());

	QSplitter* vSplitter = new QSplitter(Qt::Vertical);

	QSplitter* hSplitter = new QSplitter(Qt::Horizontal);
	hSplitter->addWidget(createDisassemblyPanel());

	QWidget* rightPanel = new QWidget;
	QVBoxLayout* rightLayout = new QVBoxLayout(rightPanel);
	rightLayout->setContentsMargins(0, 0, 0, 0);
	m_registerPanel = new RegisterPanel(m_debugger, m_controller);
	rightLayout->addWidget(m_registerPanel);
	rightLayout->addWidget(createStackPanel(), 1);
	hSplitter->addWidget(rightPanel);
	hSplitter->setStretchFactor(0, 3);
	hSplitter->setStretchFactor(1, 1);

	vSplitter->addWidget(hSplitter);

	QTabWidget* bottomTabs = new QTabWidget;
	bottomTabs->addTab(createMemoryPanel(), tr("Memory"));
	m_breakpointPanel = new BreakpointPanel(m_debugger);
	bottomTabs->addTab(m_breakpointPanel, tr("Breakpoints"));
	vSplitter->addWidget(bottomTabs);
	vSplitter->setStretchFactor(0, 3);
	vSplitter->setStretchFactor(1, 1);

	mainLayout->addWidget(vSplitter, 1);

	m_status = new QLabel(tr("Attaching..."));
	m_status->setFont(GBAApp::app()->monospaceFont());
	mainLayout->addWidget(m_status);

	connect(m_debugger, &DebuggerGuiController::debuggerEntered, this, &DebuggerGuiView::debuggerEntered);
	connect(m_debugger, &DebuggerGuiController::debuggerResumed, this, &DebuggerGuiView::debuggerResumed);
	connect(m_debugger, &DebuggerGuiController::breakpointsChanged, m_breakpointPanel, &BreakpointPanel::updateBreakpointList);
	connect(m_breakpointPanel, &BreakpointPanel::breakpointAddressesChanged, m_disassembly, &DisassemblyView::setBreakpoints);
	connect(m_controller.get(), &CoreController::paused, this, &DebuggerGuiView::frameLevelPaused);

	connect(m_controller.get(), &CoreController::frameAvailable, this, &DebuggerGuiView::refreshLiveViews);
	connect(m_controller.get(), &CoreController::stateLoaded, this, &DebuggerGuiView::stateChanged);
	connect(m_controller.get(), &CoreController::rewound, this, &DebuggerGuiView::stateChanged);

	connect(m_disassembly, &DisassemblyView::breakpointToggleRequested, this, &DebuggerGuiView::toggleBreakpointAt);
	connect(m_disassembly, &DisassemblyView::runToRequested, this, &DebuggerGuiView::runToAddress);
	connect(m_disassembly, &DisassemblyView::setPCRequested, this, &DebuggerGuiView::setPC);
	connect(m_registerPanel, &RegisterPanel::registerEdited, this, [this](bool reanchor) {
		if (reanchor) {
			refreshAll(executionPC());
		} else {
			updateStack();
		}
	});

	setPausedState(false);

	m_debugger->attach();
}

QToolBar* DebuggerGuiView::createToolBar() {
	QToolBar* toolbar = new QToolBar;
	toolbar->setToolButtonStyle(Qt::ToolButtonTextOnly);

	m_runAction = new QAction(tr("Break"), this);
	m_runAction->setShortcut(QKeySequence(Qt::Key_F9));
	connect(m_runAction, &QAction::triggered, this, &DebuggerGuiView::toggleRun);
	toolbar->addAction(m_runAction);

	m_stepIntoAction = new QAction(tr("Step into"), this);
	m_stepIntoAction->setShortcut(QKeySequence(Qt::Key_F7));
	connect(m_stepIntoAction, &QAction::triggered, m_debugger, &DebuggerGuiController::stepInto);
	toolbar->addAction(m_stepIntoAction);

	m_stepOverAction = new QAction(tr("Step over"), this);
	m_stepOverAction->setShortcut(QKeySequence(Qt::Key_F8));
	connect(m_stepOverAction, &QAction::triggered, m_debugger, &DebuggerGuiController::stepOver);
	toolbar->addAction(m_stepOverAction);

	m_stepOutAction = new QAction(tr("Step out"), this);
	m_stepOutAction->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F8));
	connect(m_stepOutAction, &QAction::triggered, m_debugger, &DebuggerGuiController::stepOut);
	toolbar->addAction(m_stepOutAction);

	m_runToCursorAction = new QAction(tr("Run to cursor"), this);
	m_runToCursorAction->setShortcut(QKeySequence(Qt::Key_F4));
	connect(m_runToCursorAction, &QAction::triggered, this, [this]() {
		runToAddress(m_disassembly->cursorAddress());
	});
	toolbar->addAction(m_runToCursorAction);

	QAction* toggleBreakpointAction = new QAction(tr("Toggle breakpoint"), this);
	toggleBreakpointAction->setShortcut(QKeySequence(Qt::Key_F2));
	connect(toggleBreakpointAction, &QAction::triggered, this, [this]() {
		toggleBreakpointAt(m_disassembly->cursorAddress());
	});
	toolbar->addAction(toggleBreakpointAction);

	toolbar->addSeparator();

	toolbar->addWidget(new QLabel(tr("Go to: ")));
	m_gotoEdit = new QLineEdit;
	m_gotoEdit->setFont(GBAApp::app()->monospaceFont());
	m_gotoEdit->setMaximumWidth(120);
	m_gotoEdit->setPlaceholderText(QStringLiteral("08000000"));
	connect(m_gotoEdit, &QLineEdit::returnPressed, this, &DebuggerGuiView::gotoAddress);
	toolbar->addWidget(m_gotoEdit);

	QAction* gotoFocus = new QAction(this);
	gotoFocus->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_G));
	connect(gotoFocus, &QAction::triggered, this, [this]() {
		m_gotoEdit->setFocus();
		m_gotoEdit->selectAll();
	});
	addAction(gotoFocus);

	toolbar->addSeparator();
	QComboBox* modeSelect = new QComboBox;
	modeSelect->addItem(tr("Auto"), static_cast<int>(DisassemblyView::Mode::AUTO));
	modeSelect->addItem(QStringLiteral("ARM"), static_cast<int>(DisassemblyView::Mode::ARM));
	modeSelect->addItem(QStringLiteral("Thumb"), static_cast<int>(DisassemblyView::Mode::THUMB));
	// m_disassembly is not constructed yet; resolve it at call time
	connect(modeSelect, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, modeSelect](int) {
		if (m_disassembly) {
			DisassemblyView::Mode mode = static_cast<DisassemblyView::Mode>(modeSelect->currentData().toInt());
			m_disassembly->setDisassemblyMode(mode);
		}
	});
	toolbar->addWidget(modeSelect);

	return toolbar;
}

QWidget* DebuggerGuiView::createStackPanel() {
	QGroupBox* group = new QGroupBox(tr("Stack"));
	QVBoxLayout* layout = new QVBoxLayout(group);
	layout->setContentsMargins(2, 2, 2, 2);
	m_stack = new QTableWidget(STACK_ROWS, 2);
	m_stack->setHorizontalHeaderLabels({tr("Address"), tr("Value")});
	m_stack->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
	m_stack->verticalHeader()->setVisible(false);
	m_stack->setFont(GBAApp::app()->monospaceFont());
	m_stack->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_stack->setSelectionBehavior(QAbstractItemView::SelectRows);
	connect(m_stack, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
		QTableWidgetItem* item = m_stack->item(row, 1);
		uint32_t value;
		if (item && parseHex32(item->text(), &value)) {
			jumpMemoryToAddress(value);
		}
	});
	layout->addWidget(m_stack);
	return group;
}

QWidget* DebuggerGuiView::createDisassemblyPanel() {
	QWidget* panel = new QWidget;
	QVBoxLayout* layout = new QVBoxLayout(panel);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(2);

	QHBoxLayout* controls = new QHBoxLayout;
	controls->setContentsMargins(2, 2, 2, 0);
	controls->addWidget(new QLabel(tr("Region:")));
	m_disasmRegions = new QComboBox;
	controls->addWidget(m_disasmRegions);
	controls->addStretch(1);
	layout->addLayout(controls);

	m_disassembly = new DisassemblyView;
	m_disassembly->setController(m_controller);
	m_disassembly->setAddressFolder([this](uint32_t address) {
		return m_debugger->foldAddress(address);
	});
	layout->addWidget(m_disassembly, 1);

	populateRegionSelector(m_disasmRegions, mCORE_MEMORY_MAPPED);

	connect(m_disasmRegions, QOverload<int>::of(&QComboBox::currentIndexChanged),
	        this, &DebuggerGuiView::setDisassemblyRegion);
	connect(m_disassembly, &DisassemblyView::regionChanged, this, &DebuggerGuiView::disassemblyRegionChanged);
	disassemblyRegionChanged(m_disassembly->regionBase());

	return panel;
}

void DebuggerGuiView::populateRegionSelector(QComboBox* box, uint32_t anyFlags) {
	if (m_memoryBlocks.isEmpty()) {
		mCore* core = m_controller->thread()->core;
		const mCoreMemoryBlock* blocks;
		size_t nBlocks = core->listMemoryBlocks(core, &blocks);
		if (!blocks) {
			return;
		}
		for (size_t i = 0; i < nBlocks; ++i) {
			m_memoryBlocks.append(blocks[i]);
		}
	}
	for (int i = 0; i < m_memoryBlocks.size(); ++i) {
		const mCoreMemoryBlock& block = m_memoryBlocks[i];
		if (!(block.flags & anyFlags)) {
			continue;
		}
		box->addItem(tr(block.longName), i);
	}
}

const mCoreMemoryBlock* DebuggerGuiView::memoryBlockForItem(QComboBox* box, int index) {
	bool ok = false;
	int blockId = box->itemData(index).toInt(&ok);
	if (!ok || blockId < 0 || blockId >= m_memoryBlocks.size()) {
		return nullptr;
	}
	return &m_memoryBlocks[blockId];
}

int DebuggerGuiView::findRegionItem(QComboBox* box, const std::function<bool(const mCoreMemoryBlock&)>& match) {
	for (int i = 0; i < box->count(); ++i) {
		const mCoreMemoryBlock* block = memoryBlockForItem(box, i);
		if (block && match(*block)) {
			return i;
		}
	}
	return -1;
}

void DebuggerGuiView::setDisassemblyRegion(int index) {
	if (index < 0 || !m_disassembly) {
		return;
	}
	const mCoreMemoryBlock* block = memoryBlockForItem(m_disasmRegions, index);
	if (!block) {
		return;
	}
	m_disassembly->jumpToAddress(block->start);
	m_disassembly->setFocus();
}

void DebuggerGuiView::disassemblyRegionChanged(uint32_t base) {
	if (!m_disasmRegions) {
		return;
	}
	int index = findRegionItem(m_disasmRegions, [base](const mCoreMemoryBlock& block) {
		return block.start == base;
	});
	if (index >= 0) {
		// Sync-back must not re-jump the view to the region start
		QSignalBlocker blocker(m_disasmRegions);
		m_disasmRegions->setCurrentIndex(index);
	}
}

QWidget* DebuggerGuiView::createMemoryPanel() {
	QWidget* panel = new QWidget;
	QVBoxLayout* layout = new QVBoxLayout(panel);
	layout->setContentsMargins(2, 2, 2, 2);
	layout->setSpacing(2);

	QHBoxLayout* controls = new QHBoxLayout;
	controls->setContentsMargins(0, 0, 0, 0);
	controls->addWidget(new QLabel(tr("Region:")));
	m_memoryRegions = new QComboBox;
	controls->addWidget(m_memoryRegions);
	controls->addWidget(new QLabel(tr("Address:")));
	QLineEdit* addressEdit = new QLineEdit;
	addressEdit->setFont(GBAApp::app()->monospaceFont());
	addressEdit->setPlaceholderText(QStringLiteral("0x03000000"));
	controls->addWidget(addressEdit);
	controls->addStretch(1);
	layout->addLayout(controls);

	m_memory = new MemoryModel;
	m_memory->setController(m_controller);
	layout->addWidget(m_memory, 1);

	populateRegionSelector(m_memoryRegions, mCORE_MEMORY_MAPPED | mCORE_MEMORY_VIRTUAL);
	int defaultIndex = -1;
	uint32_t bestSize = 0;
	for (int i = 0; i < m_memoryRegions->count(); ++i) {
		const mCoreMemoryBlock* block = memoryBlockForItem(m_memoryRegions, i);
		if (block && (block->flags & mCORE_MEMORY_WRITE) && block->size > bestSize) {
			bestSize = block->size;
			defaultIndex = i;
		}
	}

	connect(m_memoryRegions, QOverload<int>::of(&QComboBox::currentIndexChanged),
	        this, &DebuggerGuiView::setMemoryRegion);
	connect(addressEdit, &QLineEdit::returnPressed, this, [this, addressEdit]() {
		uint32_t address;
		if (parseHex32(addressEdit->text(), &address)) {
			jumpMemoryToAddress(address);
		}
	});

	if (defaultIndex >= 0) {
		m_memoryRegions->setCurrentIndex(defaultIndex);
	}
	// setCurrentIndex emits nothing when the index is unchanged
	setMemoryRegion(m_memoryRegions->currentIndex());

	return panel;
}

void DebuggerGuiView::setMemoryRegion(int index) {
	if (index < 0 || !m_memory) {
		return;
	}
	const mCoreMemoryBlock* block = memoryBlockForItem(m_memoryRegions, index);
	if (!block) {
		return;
	}
	m_memory->setRegion(block->start, block->end - block->start, block->shortName);
}

void DebuggerGuiView::jumpMemoryToAddress(uint32_t address) {
	if (!m_memory || !m_memoryRegions) {
		return;
	}
	int targetItem = findRegionItem(m_memoryRegions, [address](const mCoreMemoryBlock& block) {
		return address >= block.start && address < block.end;
	});
	if (targetItem < 0) {
		return;
	}
	if (targetItem != m_memoryRegions->currentIndex()) {
		m_memoryRegions->setCurrentIndex(targetItem);
	}
	m_memory->jumpToAddress(address);
}

void DebuggerGuiView::closeEvent(QCloseEvent* event) {
	m_debugger->resumeAndDetach();
	QWidget::closeEvent(event);
}

void DebuggerGuiView::debuggerEntered(quint32 pc, const QString& description) {
	setPausedState(true);
	m_status->setText(description);
	refreshAll(pc);
}

void DebuggerGuiView::debuggerResumed() {
	setPausedState(false);
	m_status->setText(tr("Running..."));
}

void DebuggerGuiView::frameLevelPaused() {
	if (m_paused) {
		return;
	}
	refreshAll(executionPC());
	m_status->setText(tr("Paused (frame); press Break to enable stepping"));
}

void DebuggerGuiView::toggleRun() {
	if (m_paused) {
		m_debugger->continueRun();
	} else {
		m_debugger->attach();
		m_debugger->breakInto();
	}
}

void DebuggerGuiView::gotoAddress() {
	uint32_t address;
	if (parseHex32(m_gotoEdit->text(), &address)) {
		m_disassembly->jumpToAddress(address);
		m_disassembly->setFocus();
	}
}

void DebuggerGuiView::refreshAll(quint32 pc) {
	m_registerPanel->updateRegisters();
	updateStack();
	m_breakpointPanel->updateBreakpointList();
	m_disassembly->jumpToPC(pc);
	m_memory->viewport()->update();
}

void DebuggerGuiView::refreshLiveViews() {
	m_memory->viewport()->update();
	m_disassembly->viewport()->update();
}

void DebuggerGuiView::stateChanged() {
	// m_paused only tracks debugger breaks; isPaused() catches the ordinary Pause action
	if (m_paused || m_controller->isPaused()) {
		refreshAll(executionPC());
	} else {
		refreshLiveViews();
	}
}

void DebuggerGuiView::updateStack() {
#ifdef M_CORE_GBA
	CoreController::Interrupter interrupter(m_controller);
	ARMCore* cpu = m_debugger->armCpu();
	if (!cpu) {
		return;
	}
	mCore* core = m_controller->thread()->core;
	uint32_t sp = cpu->gprs[ARM_SP];
	for (int i = 0; i < STACK_ROWS; ++i) {
		uint32_t address = sp + i * 4;
		// rawRead: a corrupted SP pointing at I/O must not perturb emulation on repaint
		uint32_t value = core->rawRead32(core, address, -1);
		QTableWidgetItem* addressItem = new QTableWidgetItem(formatHex32(address));
		QTableWidgetItem* valueItem = new QTableWidgetItem(formatHex32(value));
		if (i == 0) {
			QFont bold = m_stack->font();
			bold.setBold(true);
			addressItem->setFont(bold);
			valueItem->setFont(bold);
		}
		m_stack->setItem(i, 0, addressItem);
		m_stack->setItem(i, 1, valueItem);
	}
#endif
}

void DebuggerGuiView::setPausedState(bool paused) {
	m_paused = paused;
	m_runAction->setText(paused ? tr("Continue") : tr("Break"));
	m_stepIntoAction->setEnabled(paused);
	m_stepOverAction->setEnabled(paused);
	m_stepOutAction->setEnabled(paused);
	m_runToCursorAction->setEnabled(paused);
	m_registerPanel->setPaused(paused);
}

void DebuggerGuiView::setPC(uint32_t address) {
	if (!m_paused) {
		return;
	}
	m_registerPanel->writePC(address);
	m_registerPanel->updateRegisters();
	m_disassembly->jumpToPC(executionPC());
}

void DebuggerGuiView::runToAddress(uint32_t address) {
	m_debugger->runToAddress(address, m_disassembly->instructionSpanEnd(address));
}

void DebuggerGuiView::toggleBreakpointAt(uint32_t address) {
	m_debugger->toggleBreakpointAt(address, m_disassembly->instructionSpanEnd(address),
	                               m_disassembly->lineSpanEnd(address));
}

quint32 DebuggerGuiView::executionPC() {
	CoreController::Interrupter interrupter(m_controller);
	return m_debugger->executionPC();
}
