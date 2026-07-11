/* Copyright (c) 2013-2025 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "RegisterPanel.h"
#include "moc_RegisterPanel.cpp"

#include "CoreController.h"
#include "DebuggerGuiController.h"
#include "GBAApp.h"
#include "utils.h"

#include <QCheckBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>

#include <mgba/core/core.h>

#ifdef M_CORE_GBA
#include <mgba/internal/arm/arm.h>
#endif

using namespace QGBA;

#ifdef M_CORE_GBA
static const struct {
	uint32_t priv;
	const char* name;
} _privModeNames[] = {
	{MODE_USER, "User"},
	{MODE_FIQ, "FIQ"},
	{MODE_IRQ, "IRQ"},
	{MODE_SUPERVISOR, "SVC"},
	{MODE_ABORT, "Abort"},
	{MODE_UNDEFINED, "Undef"},
	{MODE_SYSTEM, "System"},
};
#endif

// Must match the union PSR bit layout in arm.h
static const struct {
	const char* name;
	uint32_t mask;
} _psrFlags[] = {
	{"N", 1u << 31},
	{"Z", 1u << 30},
	{"C", 1u << 29},
	{"V", 1u << 28},
	{"I", 1u << 7},
	{"F", 1u << 6},
	{"T", 1u << 5},
};

enum {
	REGISTER_ROWS = 8,
	REGISTER_COUNT = 16,
};

static const char* const _pcName = "pc";
static const char* const _cpsrName = "cpsr";

static const char* const _registerNames[REGISTER_COUNT] = {
	"r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7",
	"r8", "r9", "r10", "r11", "r12", "sp", "lr", _pcName,
};

RegisterPanel::RegisterPanel(DebuggerGuiController* debugger, std::shared_ptr<CoreController> controller,
                             QWidget* parent)
	: QGroupBox(tr("Registers"), parent)
	, m_debugger(debugger)
	, m_controller(controller)
{
	QGridLayout* grid = new QGridLayout(this);
	grid->setVerticalSpacing(2);
	grid->setHorizontalSpacing(4);
	const QFont font = GBAApp::app()->monospaceFont();

	auto addRegister = [this, grid, font](const QString& name, int row, int column) {
		QLabel* label = new QLabel(name);
		label->setFont(font);
		QLineEdit* edit = new QLineEdit;
		edit->setFont(font);
		edit->setMaxLength(8);
		edit->setMinimumWidth(90);
		grid->addWidget(label, row, column * 2);
		grid->addWidget(edit, row, column * 2 + 1);
		m_registers[name] = edit;
		connect(edit, &QLineEdit::editingFinished, this, [this, name]() {
			editFinished(name);
		});
	};

	for (int i = 0; i < REGISTER_COUNT; ++i) {
		addRegister(QLatin1String(_registerNames[i]), i % REGISTER_ROWS, i / REGISTER_ROWS);
	}

	addRegister(QLatin1String(_cpsrName), REGISTER_ROWS, 0);
	m_cpuModeLabel = new QLabel;
	m_cpuModeLabel->setFont(font);
	grid->addWidget(m_cpuModeLabel, REGISTER_ROWS, 2, 1, 2);

	QHBoxLayout* flagsLayout = new QHBoxLayout;
	for (const auto& flag : _psrFlags) {
		QCheckBox* box = new QCheckBox(QLatin1String(flag.name));
		box->setFont(font);
		m_flags.append(box);
		connect(box, &QCheckBox::clicked, this, &RegisterPanel::flagsEdited);
		flagsLayout->addWidget(box);
	}
	flagsLayout->addStretch();
	grid->addLayout(flagsLayout, REGISTER_ROWS + 1, 0, 1, 4);
}

void RegisterPanel::updateRegisters() {
#ifdef M_CORE_GBA
	CoreController::Interrupter interrupter(m_controller);
	ARMCore* cpu = m_debugger->armCpu();
	if (cpu) {
		for (int i = 0; i < REGISTER_COUNT; ++i) {
			// Instruction address, not the pipeline-advanced gprs[15]
			uint32_t value = i == ARM_PC ? m_debugger->executionPC() : cpu->gprs[i];
			m_registers[QLatin1String(_registerNames[i])]->setText(formatHex32(value));
		}
		union PSR cpsr = cpu->cpsr;
		m_registers[QLatin1String(_cpsrName)]->setText(formatHex32(cpsr.packed));
		for (int i = 0; i < m_flags.size(); ++i) {
			m_flags[i]->setChecked(cpsr.packed & _psrFlags[i].mask);
		}
		QString mode = QStringLiteral("?");
		for (const auto& privMode : _privModeNames) {
			if (cpsr.priv == privMode.priv) {
				mode = QLatin1String(privMode.name);
				break;
			}
		}
		m_cpuModeLabel->setText(tr("Mode: %1").arg(mode));
	}
#endif
}

void RegisterPanel::setPaused(bool paused) {
	m_paused = paused;
	for (QLineEdit* edit : m_registers) {
		edit->setReadOnly(!paused);
	}
	for (QCheckBox* box : m_flags) {
		box->setEnabled(paused);
	}
}

void RegisterPanel::writePC(uint32_t value) {
	writeRegister(_pcName, value);
}

void RegisterPanel::writeRegister(const char* name, uint32_t value) {
	CoreController::Interrupter interrupter(m_controller);
	mCore* core = m_controller->thread()->core;
	core->writeRegister(core, name, static_cast<int32_t>(value));
}

void RegisterPanel::editFinished(const QString& name) {
	if (!m_paused) {
		return;
	}
	uint32_t value;
	if (!parseHex32(m_registers[name]->text(), &value)) {
		updateRegisters();
		return;
	}
	writeRegister(name.toUtf8().constData(), value);
	bool reanchor = name == QLatin1String(_pcName) || name == QLatin1String(_cpsrName);
	if (!reanchor) {
		updateRegisters();
	}
	emit registerEdited(reanchor);
}

void RegisterPanel::flagsEdited() {
	if (!m_paused) {
		return;
	}
#ifdef M_CORE_GBA
	uint32_t value;
	if (!parseHex32(m_registers[QLatin1String(_cpsrName)]->text(), &value)) {
		return;
	}
	for (int i = 0; i < m_flags.size(); ++i) {
		if (m_flags[i]->isChecked()) {
			value |= _psrFlags[i].mask;
		} else {
			value &= ~_psrFlags[i].mask;
		}
	}
	writeRegister(_cpsrName, value);
	emit registerEdited(true);
#endif
}
