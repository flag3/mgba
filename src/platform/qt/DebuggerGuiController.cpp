/* Copyright (c) 2013-2025 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "DebuggerGuiController.h"
#include "moc_DebuggerGuiController.cpp"

#include "CoreController.h"
#include "utils.h"

#include <mgba/core/core.h>

#ifdef M_CORE_GBA
#include "ArmThumbDecode.h"

#include <mgba/internal/arm/arm.h>
#include <mgba/internal/arm/debugger/debugger.h>
#include <mgba/internal/arm/decoder.h>
#endif

using namespace QGBA;

// Callers must hold the core (Interrupter) or run on the core thread
static QList<mBreakpoint> _listBreakpoints(mDebuggerPlatform* platform, mDebuggerModule* module) {
	QList<mBreakpoint> ret;
	mBreakpointList list;
	mBreakpointListInit(&list, 0);
	platform->listBreakpoints(platform, module, &list);
	for (size_t i = 0; i < mBreakpointListSize(&list); ++i) {
		ret.append(*mBreakpointListGetPointer(&list, i));
	}
	mBreakpointListDeinit(&list);
	return ret;
}

static QList<mWatchpoint> _listWatchpoints(mDebuggerPlatform* platform, mDebuggerModule* module) {
	QList<mWatchpoint> ret;
	mWatchpointList list;
	mWatchpointListInit(&list, 0);
	platform->listWatchpoints(platform, module, &list);
	for (size_t i = 0; i < mWatchpointListSize(&list); ++i) {
		ret.append(*mWatchpointListGetPointer(&list, i));
	}
	mWatchpointListDeinit(&list);
	return ret;
}

DebuggerGuiController::DebuggerGuiController(QObject* parent)
	: DebuggerController(&m_module, parent)
{
	m_module.self = this;
	m_module.entered = moduleEntered;
	m_module.paused = modulePaused;
	m_module.interrupt = moduleInterrupt;
	m_module.type = DEBUGGER_CUSTOM;
}

DebuggerGuiController::~DebuggerGuiController() {
	// Wake any core thread blocked in modulePaused before m_module is freed
	resumeAndDetach();
}

QList<mBreakpoint> DebuggerGuiController::breakpoints() {
	if (!isAttached()) {
		return {};
	}
	CoreController::Interrupter interrupter(m_gameController);
	return _listBreakpoints(m_module.p->platform, &m_module);
}

QList<mWatchpoint> DebuggerGuiController::watchpoints() {
	if (!isAttached()) {
		return {};
	}
	CoreController::Interrupter interrupter(m_gameController);
	return _listWatchpoints(m_module.p->platform, &m_module);
}

// Callers must hold the core (Interrupter) or run on the core thread
ssize_t DebuggerGuiController::addBreakpoint(uint32_t address, uint32_t addressHi, bool temporary) {
	mDebuggerPlatform* platform = m_module.p->platform;
	mBreakpoint breakpoint{};
	breakpoint.address = address;
	breakpoint.addressHi = addressHi;
	breakpoint.segment = -1;
	breakpoint.type = BREAKPOINT_HARDWARE;
	breakpoint.isTemporary = temporary;
	return platform->setBreakpoint(platform, &m_module, &breakpoint);
}

void DebuggerGuiController::setBreakpointAt(uint32_t address, uint32_t addressHi) {
	if (!isAttached()) {
		return;
	}
	CoreController::Interrupter interrupter(m_gameController);
	addBreakpoint(address, addressHi, false);
	interrupter.resume();
	emit breakpointsChanged();
}

void DebuggerGuiController::toggleBreakpointAt(uint32_t address, uint32_t addressHi, uint32_t clearAddressHi) {
	if (!isAttached()) {
		return;
	}
	bool cleared = false;
	{
		CoreController::Interrupter interrupter(m_gameController);
		mDebuggerPlatform* platform = m_module.p->platform;
		uint32_t foldedLo = foldAddress(address);
		uint32_t foldedHi = foldAddress(qMax(address, qMax(addressHi, clearAddressHi)));
		for (const mBreakpoint& breakpoint : _listBreakpoints(platform, &m_module)) {
			if (breakpoint.isTemporary) {
				continue;
			}
			// Folded span overlap: a toggle at any mirror alias or halfword clears instead of stacking
			if (foldAddress(breakpoint.address) <= foldedHi && foldAddress(breakpoint.addressHi) >= foldedLo) {
				platform->clearBreakpoint(platform, breakpoint.id);
				cleared = true;
			}
		}
	}
	if (cleared) {
		emit breakpointsChanged();
	} else {
		setBreakpointAt(address, addressHi);
	}
}

void DebuggerGuiController::setWatchpoint(uint32_t minAddress, uint32_t maxAddress, mWatchpointType type) {
	if (!isAttached()) {
		return;
	}
	CoreController::Interrupter interrupter(m_gameController);
	mDebuggerPlatform* platform = m_module.p->platform;
	if (!platform->setWatchpoint) {
		return;
	}
	mWatchpoint watchpoint{};
	watchpoint.minAddress = minAddress;
	watchpoint.maxAddress = maxAddress;
	watchpoint.segment = -1;
	watchpoint.type = type;
	platform->setWatchpoint(platform, &m_module, &watchpoint);
	interrupter.resume();
	emit breakpointsChanged();
}

void DebuggerGuiController::clearPoint(ssize_t id) {
	if (!isAttached()) {
		return;
	}
	{
		CoreController::Interrupter interrupter(m_gameController);
		mDebuggerPlatform* platform = m_module.p->platform;
		platform->clearBreakpoint(platform, id);
	}
	emit breakpointsChanged();
}

void DebuggerGuiController::enablePoint(ssize_t id, bool enable) {
	if (!isAttached()) {
		return;
	}
	{
		CoreController::Interrupter interrupter(m_gameController);
		mDebuggerPlatform* platform = m_module.p->platform;
		platform->toggleBreakpoint(platform, id, enable);
	}
	emit breakpointsChanged();
}

void DebuggerGuiController::stepInto() {
	enqueue(Command::STEP_INTO);
}

void DebuggerGuiController::stepOver() {
	enqueue(Command::STEP_OVER);
}

void DebuggerGuiController::stepOut() {
	enqueue(Command::STEP_OUT);
}

void DebuggerGuiController::runToAddress(uint32_t address, uint32_t addressHi) {
	if (!isAttached()) {
		return;
	}
	{
		QMutexLocker locker(&m_mutex);
		if (!m_module.isPaused) {
			return;
		}
	}
	{
		CoreController::Interrupter interrupter(m_gameController);
		addBreakpoint(address, addressHi, true);
	}
	enqueue(Command::CONTINUE);
}

void DebuggerGuiController::continueRun() {
	enqueue(Command::CONTINUE);
}

void DebuggerGuiController::resumeAndDetach() {
	if (!isAttached()) {
		return;
	}
	{
		CoreController::Interrupter interrupter(m_gameController);
		QMutexLocker locker(&m_mutex);
		clearAndResumeLocked();
		if (m_module.p) {
			mDebuggerUpdatePaused(m_module.p);
		}
	}
	detach();
	emit debuggerResumed();
}

// Caller must hold m_mutex
void DebuggerGuiController::clearAndResumeLocked() {
	m_commands.clear();
	m_module.isPaused = false;
	m_cond.wakeAll();
}

void DebuggerGuiController::attachInternal() {
	QMutexLocker locker(&m_mutex);
	m_commands.clear();
}

void DebuggerGuiController::shutdownInternal() {
	{
		QMutexLocker locker(&m_mutex);
		clearAndResumeLocked();
	}
	if (!m_module.p) {
		return;
	}
	mDebuggerPlatform* platform = m_module.p->platform;
	if (platform) {
		for (const mBreakpoint& breakpoint : _listBreakpoints(platform, &m_module)) {
			platform->clearBreakpoint(platform, breakpoint.id);
		}
		for (const mWatchpoint& watchpoint : _listWatchpoints(platform, &m_module)) {
			platform->clearBreakpoint(platform, watchpoint.id);
		}
	}
	mDebuggerUpdatePaused(m_module.p);
}

DebuggerGuiController* DebuggerGuiController::controllerFor(mDebuggerModule* module) {
	return static_cast<GuiDebugger*>(module)->self;
}

void DebuggerGuiController::moduleEntered(mDebuggerModule* module, enum mDebuggerEntryReason reason, mDebuggerEntryInfo* info) {
	DebuggerGuiController* self = controllerFor(module);
	QString description;
	switch (reason) {
	case DEBUGGER_ENTER_MANUAL:
		description = tr("Paused");
		break;
	case DEBUGGER_ENTER_ATTACHED:
		description = tr("Debugger attached");
		break;
	case DEBUGGER_ENTER_BREAKPOINT:
		if (info) {
			description = tr("Breakpoint %1 hit at 0x%2")
				.arg(info->pointId)
				.arg(formatHex32(info->address));
		} else {
			description = tr("Breakpoint hit");
		}
		break;
	case DEBUGGER_ENTER_WATCHPOINT:
		if (info) {
			if (info->type.wp.accessType & WATCHPOINT_WRITE) {
				description = tr("Watchpoint %1 hit at 0x%2: 0x%3 -> 0x%4")
					.arg(info->pointId)
					.arg(formatHex32(info->address))
					.arg(formatHex32(info->type.wp.oldValue))
					.arg(formatHex32(info->type.wp.newValue));
			} else {
				description = tr("Watchpoint %1 hit at 0x%2: value 0x%3")
					.arg(info->pointId)
					.arg(formatHex32(info->address))
					.arg(formatHex32(info->type.wp.oldValue));
			}
		} else {
			description = tr("Watchpoint hit");
		}
		break;
	case DEBUGGER_ENTER_ILLEGAL_OP:
		if (info) {
			description = tr("Illegal opcode 0x%1 at 0x%2")
				.arg(formatHex32(info->type.bp.opcode))
				.arg(formatHex32(info->address));
		} else {
			description = tr("Illegal opcode");
		}
		break;
	case DEBUGGER_ENTER_STACK:
		description = tr("Stack event");
		break;
	}
	emit self->debuggerEntered(self->executionPC(), description);
}

void DebuggerGuiController::modulePaused(mDebuggerModule* module, int32_t timeoutMs) {
	controllerFor(module)->processCommands(timeoutMs);
}

void DebuggerGuiController::moduleInterrupt(mDebuggerModule* module) {
	controllerFor(module)->m_cond.wakeAll();
}

void DebuggerGuiController::processCommands(int32_t timeoutMs) {
	QMutexLocker locker(&m_mutex);
	if (m_commands.isEmpty()) {
		if (timeoutMs < 0) {
			timeoutMs = 50;
		}
		if (timeoutMs > 0) {
			m_cond.wait(&m_mutex, static_cast<unsigned long>(timeoutMs));
		}
	}
	while (!m_commands.isEmpty()) {
		Command command = m_commands.takeFirst();
		locker.unlock();
		executeCommand(command);
		locker.relock();
		if (!m_module.isPaused) {
			m_commands.clear();
			break;
		}
	}
}

void DebuggerGuiController::executeCommand(Command command) {
	switch (command) {
	case Command::STEP_INTO:
		stepIntoInternal();
		break;
	case Command::STEP_OVER:
		stepOverInternal();
		break;
	case Command::STEP_OUT:
		stepOutInternal();
		break;
	case Command::CONTINUE:
		continueInternal();
		break;
	}
}

void DebuggerGuiController::stepIntoInternal() {
	if (!m_module.p) {
		return;
	}
	mCore* core = m_module.p->core;
	core->step(core);
	mDebuggerPlatform* platform = m_module.p->platform;
	if (platform->getStackTraceMode && platform->getStackTraceMode(platform) != STACK_TRACE_DISABLED) {
		platform->updateStackTrace(platform);
	}
	uint32_t pc = executionPC();
	emit debuggerEntered(pc, tr("Stepped to 0x%1").arg(formatHex32(pc)));
}

void DebuggerGuiController::stepOverInternal() {
#ifdef M_CORE_GBA
	ARMCore* cpu = armCpu();
	if (cpu) {
		mCore* core = m_module.p->core;
		bool thumb = cpu->executionMode == MODE_THUMB;
		uint32_t pc = executionPC();
		int width;
		ARMInstructionInfo info;
		if (thumb) {
			pc &= ~(WORD_SIZE_THUMB - 1);
			uint32_t readLimit = UINT32_MAX;
			const mCoreMemoryBlock* blocks = nullptr;
			size_t nBlocks = core->listMemoryBlocks(core, &blocks);
			for (size_t i = 0; blocks && i < nBlocks; ++i) {
				if (pc >= blocks[i].start && pc < blocks[i].end) {
					readLimit = blocks[i].end;
					break;
				}
			}
			ThumbInstruction instr = decodeThumbAt(core, pc, readLimit);
			width = instr.isPair ? WORD_SIZE_THUMB * 2 : WORD_SIZE_THUMB;
			info = instr.info;
		} else {
			pc &= ~(WORD_SIZE_ARM - 1);
			ArmInstruction instr = decodeArmAt(core, pc);
			width = WORD_SIZE_ARM;
			info = instr.info;
		}
		bool isCall = info.branchType == ARM_BRANCH_LINKED || info.traps;
		if (isCall) {
			addBreakpoint(pc + width, 0, true);
			continueInternal();
		} else {
			stepIntoInternal();
		}
		return;
	}
#endif
	stepIntoInternal();
}

void DebuggerGuiController::stepOutInternal() {
#ifdef M_CORE_GBA
	ARMCore* cpu = armCpu();
	if (cpu) {
		uint32_t returnAddress = cpu->gprs[ARM_LR] & ~1;
		addBreakpoint(returnAddress, 0, true);
		continueInternal();
		return;
	}
#endif
	stepIntoInternal();
}

void DebuggerGuiController::continueInternal() {
	// isPaused is read from the GUI thread under m_mutex; runs here with processCommands unlocked
	{
		QMutexLocker locker(&m_mutex);
		m_module.isPaused = false;
	}
	emit debuggerResumed();
}

uint32_t DebuggerGuiController::executionPC() {
#ifdef M_CORE_GBA
	ARMCore* cpu = armCpu();
	if (cpu) {
		return cpu->gprs[ARM_PC] - (cpu->executionMode == MODE_THUMB ? WORD_SIZE_THUMB : WORD_SIZE_ARM);
	}
#endif
	return 0;
}

ARMCore* DebuggerGuiController::armCpu() {
#ifdef M_CORE_GBA
	if (m_module.p) {
		mCore* core = m_module.p->core;
		if (core->platform(core) == mPLATFORM_GBA) {
			return static_cast<ARMCore*>(core->cpu);
		}
	}
#endif
	return nullptr;
}

uint32_t DebuggerGuiController::foldAddress(uint32_t address) {
#ifdef M_CORE_GBA
	if (isAttached() && armCpu()) {
		ARMDebugger* platform = reinterpret_cast<ARMDebugger*>(m_module.p->platform);
		if (platform->foldAddress) {
			return platform->foldAddress(platform, address);
		}
	}
#endif
	return address;
}

void DebuggerGuiController::enqueue(Command command) {
	if (!isAttached()) {
		return;
	}
	QMutexLocker locker(&m_mutex);
	if (!m_module.isPaused) {
		return;
	}
	m_commands.append(command);
	m_cond.wakeOne();
}
