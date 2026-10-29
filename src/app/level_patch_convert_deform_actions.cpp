// TrenchBroom's Convert Selection to Patches and NetRadiant Custom's
// Deform… in the Levels editor. Convert turns the selected brushes (replaced)
// or the picked faces (their brushes kept) of a Quake III map into flat 3x3
// patches in their materials with their texture mapping, as one undo step
// that selects the patches, then turns on Edit Vertices, as TrenchBroom turns
// on its Control Point Tool; its Ctrl+P (TrenchBroom profile) acts while
// focus is in Levels, outside its fields, and keeps Go to File elsewhere
// (StudioCommandRegistry::setShortcutTypingGuards). Deform moves every
// control point of the selected patches along one axis by a random whole
// number of units up to a maximum, from a seed the dialog shows so a result
// can be repeated (`map deform-patch --seed`).
// Workflow reference: TB ActionManager "Menu/Edit/Convert Selection to
// Patches" and MapWindow::convertSelectionToPatches at 4c756be1
// (GPL-3.0-or-later); NRC radiant/patchmanip.cpp Patch_Deform and
// DoPatchDeformDlg at 449778b3 (GPL-2.0-or-later). Original Qt
// implementation; see docs/CREDITS.md.

#include "app/application_shell.h"
#include "core/level_patch_convert.h"
#include "core/level_patch_tools.h"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QRandomGenerator>
#include <QSet>
#include <QSpinBox>
#include <QStatusBar>

#include <algorithm>
#include <cmath>
#include <limits>

namespace vibestudio {

bool ApplicationShell::levelSelectionConvertibleToPatches() const
{
	if (m_levelMapDocument.format != LevelMapFormat::Quake3Map) {
		return false;
	}
	if (!levelPickedSurfaceFaces().isEmpty()) {
		return true;
	}
	// In a component mode only picked faces convert.
	return !levelComponentsActive() && !levelMapSelectedSurfaces(m_levelMapDocument).isEmpty();
}

void ApplicationShell::convertLevelSelectionToPatchesFromUi()
{
	if (m_levelMapDocument.format != LevelMapFormat::Quake3Map) {
		statusBar()->showMessage(tr("Convert Selection to Patches works on Quake III maps, which have patches."), 6000);
		return;
	}
	// Picked faces convert on their own; otherwise the selected brushes,
	// those of selected brush entities included, convert whole.
	const QVector<LevelSurfaceFace> picked = levelPickedSurfaceFaces();
	QVector<int> whole;
	if (picked.isEmpty() && !levelComponentsActive()) {
		QSet<int> seen;
		for (const LevelSurfaceFace& face : levelMapSelectedSurfaces(m_levelMapDocument)) {
			if (!seen.contains(face.brushId)) {
				seen.insert(face.brushId);
				whole << face.brushId;
			}
		}
	}
	if (picked.isEmpty() && whole.isEmpty()) {
		statusBar()->showMessage(tr("Select brushes, or pick brush faces, to convert to patches."), 6000);
		return;
	}
	// TrenchBroom makes 3 by 3 patches; map convert-patches takes others.
	LevelPatchConvertRequest request;
	request.imageSize = [this](const QString& material) { return levelPatchImageSize(material); };
	LevelPatchConvertReport report;
	QString error;
	if (!convertLevelMapFacesToPatches(&m_levelMapDocument, picked, whole, request, &report, &error)) {
		statusBar()->showMessage(tr("Could not convert to patches: %1").arg(error), 8000);
		return;
	}
	const int made = static_cast<int>(report.patchIds.size());
	recordActivity(tr("Faces converted to patches"), tr("%n patch(es) made", nullptr, made), QStringLiteral("level-map"), OperationState::Warning,
		tr("Unsaved map edit"));
	refreshLevelMapWorkbench();
	// TrenchBroom turns on its Control Point Tool: here, Edit Vertices.
	setLevelComponentMode(1, false);
	QString message = tr("Converted %n face(s) to patches, selected for Edit Vertices.", nullptr, report.faces);
	if (report.brushesRemoved > 0) {
		message += QLatin1Char(' ') + tr("%n brush(es) replaced.", nullptr, report.brushesRemoved);
	}
	if (!report.unsizedMaterials.isEmpty()) {
		message += QLatin1Char(' ')
			+ tr("No image was found for %1, so it is mapped as 64 by 64 texels.").arg(report.unsizedMaterials.join(QStringLiteral(", ")));
	}
	if (!report.skipped.isEmpty()) {
		const int skipped = static_cast<int>(report.skipped.size());
		message += QLatin1Char(' ') + tr("%n left as it was: %1", nullptr, skipped).arg(report.skipped.join(QStringLiteral("; ")));
	}
	statusBar()->showMessage(message, 10000);
}

void ApplicationShell::deformLevelPatchesFromUi()
{
	const QVector<int> ids = levelMapSelectedPatchIds(m_levelMapDocument);
	if (m_levelMapDocument.format != LevelMapFormat::Quake3Map || ids.isEmpty()) {
		statusBar()->showMessage(tr("Select Quake III patches to deform."), 6000);
		return;
	}
	// NetRadiant Custom's Patch deform dialog: Max deform (64 to start) and
	// X, Y or Z (Z to start). The last choice returns; each opening draws a
	// new seed, which can be typed to repeat a result.
	const QString settingsKey = QStringLiteral("levelPatchDeform");
	const QJsonObject last = QJsonDocument::fromJson(m_settings.shellLayoutState(settingsKey)).object();
	QDialog dialog(this);
	dialog.setObjectName(QStringLiteral("levelDeformPatchDialog"));
	dialog.setWindowTitle(tr("Deform Patch"));
	dialog.setAccessibleName(dialog.windowTitle());
	auto* form = new QFormLayout(&dialog);
	form->setRowWrapPolicy(QFormLayout::WrapLongRows);
	form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	auto* maximum = new QSpinBox(&dialog);
	maximum->setObjectName(QStringLiteral("levelDeformMaximum"));
	maximum->setRange(-9999, 9999);
	maximum->setValue(std::clamp(last.value(QStringLiteral("maximum")).toInt(64), -9999, 9999));
	maximum->setSuffix(tr(" units"));
	maximum->setAccessibleName(tr("Maximum deform in map units"));
	maximum->setToolTip(tr("Each control point moves by a whole number of units from 0 to this; a negative maximum moves them the other way."));
	auto* axis = new QComboBox(&dialog);
	axis->setObjectName(QStringLiteral("levelDeformAxis"));
	axis->setAccessibleName(tr("Axis to deform along"));
	axis->addItem(tr("X"), 0);
	axis->addItem(tr("Y"), 1);
	axis->addItem(tr("Z"), 2);
	axis->setCurrentIndex(std::clamp(last.value(QStringLiteral("axis")).toInt(2), 0, 2));
	// Every 32-bit seed, as map deform-patch draws and reports them; a whole
	// number in a double spin box, since QSpinBox stops at 2147483647.
	auto* seed = new QDoubleSpinBox(&dialog);
	seed->setObjectName(QStringLiteral("levelDeformSeed"));
	seed->setDecimals(0);
	seed->setRange(0.0, 4294967295.0);
	seed->setValue(static_cast<double>(QRandomGenerator::global()->generate()));
	seed->setAccessibleName(tr("Random seed"));
	seed->setToolTip(tr("The same seed, patches and settings give the same result; map deform-patch --seed takes it too."));
	auto* reseed = new QPushButton(tr("New Seed"), &dialog);
	reseed->setObjectName(QStringLiteral("levelDeformNewSeed"));
	reseed->setAccessibleName(tr("Draw a new random seed"));
	connect(reseed, &QPushButton::clicked, &dialog,
		[seed] { seed->setValue(static_cast<double>(QRandomGenerator::global()->generate())); });
	auto* seedRow = new QHBoxLayout;
	seedRow->addWidget(seed, 1);
	seedRow->addWidget(reseed);
	form->addRow(tr("Maximum"), maximum);
	form->addRow(tr("Axis"), axis);
	form->addRow(tr("Seed"), seedRow);
	auto* note = new QLabel(&dialog);
	note->setObjectName(QStringLiteral("levelDeformNote"));
	note->setWordWrap(true);
	note->setTextFormat(Qt::PlainText);
	const int count = static_cast<int>(ids.size());
	note->setText(tr("Deforms %n selected patch(es) in one undo step; hidden patches stay as they are. Texture coordinates do not move.", nullptr, count));
	form->addRow(note);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	buttons->setObjectName(QStringLiteral("levelDeformButtons"));
	buttons->button(QDialogButtonBox::Ok)->setText(tr("Deform"));
	form->addRow(buttons);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	if (dialog.exec() != QDialog::Accepted) {
		return;
	}
	LevelPatchDeformRequest request;
	request.maximum = maximum->value();
	request.axis = axis->currentData().toInt();
	request.seed = static_cast<quint32>(std::llround(seed->value()));
	m_settings.setShellLayoutState(settingsKey,
		QJsonDocument(QJsonObject {{QStringLiteral("maximum"), request.maximum}, {QStringLiteral("axis"), request.axis}}).toJson(QJsonDocument::Compact));
	int changed = 0;
	int skipped = 0;
	QString error;
	if (!deformLevelMapPatches(&m_levelMapDocument, ids, request, &changed, &skipped, &error)) {
		statusBar()->showMessage(tr("Could not deform: %1").arg(error), 8000);
		return;
	}
	if (changed == 0) {
		statusBar()->showMessage(tr("Every offset drawn was 0; nothing moved."), 5000);
		return;
	}
	recordActivity(tr("Level patches deformed"), tr("Seed %1").arg(request.seed), QStringLiteral("level-map"), OperationState::Warning,
		tr("Unsaved map edit"));
	refreshLevelMapWorkbench();
	QString message = tr("Deformed %n patch(es)", nullptr, changed) + QLatin1Char(' ')
		+ tr("by up to %1 units along %2 with seed %3.").arg(request.maximum).arg(axis->currentText()).arg(request.seed);
	if (skipped > 0) {
		message += QLatin1Char(' ') + tr("%n hidden or out-of-group patch(es) left as they were.", nullptr, skipped);
	}
	statusBar()->showMessage(message, 8000);
}

} // namespace vibestudio
