// NetRadiant Custom's Patch menu prefabs in the Levels editor: Patch
// Cylinder, Square Patch Cylinder, Patch End Cap, Patch Bevel, Classic Patch
// Cone and Patch Sphere, each at Radiant's density (9x3, 5x3, 3x3 and 9x5),
// fill the selection's bounds, or the work zone, along the active view's
// axis, in the current material with Natural mapping by the material's
// image size, and replace the selected brushes and patches as one undo step
// (core/level_shapes.h replaceLevelMapSelectionWithPatchShape, which
// `map add-shape --object` shares). Add Patch… (Simple Patch Mesh, Shift+P
// in the NetRadiant Custom profile) fits the same box. Exact Patch
// Cylinder…, Exact Patch Cone… and Exact Patch Sphere… first ask for the
// density and Square in a Patch Density dialog, as NetRadiant Custom's
// Exact commands do, and start their rings at angle 0.
// Workflow reference: NRC radiant/patchmanip.cpp Patch_Cylinder and the
// other prefab commands, Patch_XactCylinder, Patch_XactCone,
// Patch_XactSphere and DoNewPatchDlg, Scene_PatchConstructPrefab and
// PatchCreator_getBounds, and radiant/patch.cpp Patch::ConstructPrefab at
// 449778b3 (GPL-2.0-or-later). Original Qt implementation; see
// docs/CREDITS.md.

#include "app/application_shell.h"
#include "app/map_viewport.h"
#include "app/studio_actions.h"
#include "core/level_materials.h"
#include "core/level_shapes.h"
#include "core/level_visibility.h"
#include "core/package_staging.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLocale>
#include <QStatusBar>

#include <algorithm>
#include <memory>

namespace vibestudio {
namespace {

QString materialKey(QString name)
{
	return name.trimmed().replace(QLatin1Char('\\'), QLatin1Char('/')).toCaseFolded();
}

} // namespace

void ApplicationShell::registerLevelPatchPrefabCommands()
{
	struct Entry {
		QString commandId;
		QString shape;
		QString label;
		QString tip;
	};
	const Entry entries[] = {
		{QStringLiteral("map.patchCylinder"), QStringLiteral("patch-cylinder"), tr("Patch Cylinder"),
			tr("Replace the selected brushes and patches with Radiant's nine-column, three-row patch cylinder filling their bounds along the "
			   "active view's axis, in the current material with Natural mapping, as one undo step.")},
		{QStringLiteral("map.patchSquareCylinder"), QStringLiteral("patch-square-cylinder"), tr("Square Patch Cylinder"),
			tr("Replace the selected brushes and patches with a four-sided, nine-column, three-row patch tube round their bounds along the "
			   "active view's axis, in the current material with Natural mapping, as one undo step.")},
		{QStringLiteral("map.patchEndCap"), QStringLiteral("patch-endcap"), tr("Patch End Cap"),
			tr("Replace the selected brushes and patches with a five-column half-round arch patch filling their bounds along the active "
			   "view's axis, in the current material with Natural mapping, as one undo step.")},
		{QStringLiteral("map.patchBevel"), QStringLiteral("patch-bevel"), tr("Patch Bevel"),
			tr("Replace the selected brushes and patches with a three-column quarter-round patch from one corner of their bounds to the "
			   "other along the active view's axis, in the current material with Natural mapping, as one undo step.")},
		{QStringLiteral("map.patchClassicCone"), QStringLiteral("patch-classic-cone"), tr("Classic Patch Cone"),
			tr("Replace the selected brushes and patches with Radiant's nine-column cone, full width until it closes to its tip, filling "
			   "their bounds along the active view's axis, in the current material with Natural mapping, as one undo step.")},
		{QStringLiteral("map.patchSphere"), QStringLiteral("patch-sphere"), tr("Patch Sphere"),
			tr("Replace the selected brushes and patches with Radiant's nine-column, five-row patch sphere filling their bounds along the "
			   "active view's axis, in the current material with Natural mapping, as one undo step.")},
	};
	// NetRadiant Custom's Exact Cylinder…, Exact Cone… and Exact Sphere…,
	// which ask for the density first.
	const Entry exact[] = {
		{QStringLiteral("map.patchExactCylinder"), QStringLiteral("patch-cylinder"), tr("Exact Patch Cylinder…"),
			tr("Choose the columns round, the rows along and Square, then replace the selected brushes and patches with a true-circle patch "
			   "cylinder filling their bounds along the active view's axis, in the current material with Natural mapping, as one undo step.")},
		{QStringLiteral("map.patchExactCone"), QStringLiteral("patch-cone"), tr("Exact Patch Cone…"),
			tr("Choose the columns round, the rows along and Square, then replace the selected brushes and patches with a straight-sided "
			   "true-circle patch cone filling their bounds along the active view's axis, in the current material with Natural mapping, as one "
			   "undo step.")},
		{QStringLiteral("map.patchExactSphere"), QStringLiteral("patch-sphere"), tr("Exact Patch Sphere…"),
			tr("Choose the columns round, the rows from pole to pole and Square, then replace the selected brushes and patches with a "
			   "true-circle patch sphere filling their bounds along the active view's axis, in the current material with Natural mapping, as "
			   "one undo step.")},
	};
	const auto add = [this](const Entry& entry, bool asks) {
		StudioCommandRegistration command;
		command.commandId = entry.commandId;
		command.group = StudioCommandGroup::Edit;
		command.menuSection = tr("Patch Shapes");
		command.label = entry.label;
		command.statusTip = entry.tip;
		command.iconName = levelShapeIconName(entry.shape);
		if (asks) {
			command.handler = [this, shape = entry.shape]() { addLevelExactPatchFromUi(shape); };
		} else {
			command.handler = [this, shape = entry.shape]() { addLevelPatchPrefabFromUi(shape); };
		}
		m_commands->registerCommand(command);
		m_commands->setEnabled(command.commandId, false);
	};
	for (const Entry& entry : entries) {
		add(entry, false);
	}
	for (const Entry& entry : exact) {
		add(entry, true);
	}
}

void ApplicationShell::refreshLevelPatchPrefabCommands()
{
	if (!m_commands) {
		return;
	}
	const bool quake3 = m_levelMapDocument.format == LevelMapFormat::Quake3Map;
	for (const QString& id : {QStringLiteral("map.patchCylinder"), QStringLiteral("map.patchSquareCylinder"), QStringLiteral("map.patchEndCap"),
			 QStringLiteral("map.patchBevel"), QStringLiteral("map.patchClassicCone"), QStringLiteral("map.patchSphere"),
			 QStringLiteral("map.patchExactCylinder"), QStringLiteral("map.patchExactCone"), QStringLiteral("map.patchExactSphere")}) {
		m_commands->setEnabled(id, quake3);
	}
}

void ApplicationShell::levelPatchCreationBox(LevelMapVec3* mins, LevelMapVec3* maxs) const
{
	if (!mins || !maxs) {
		return;
	}
	const double grid = m_levelMapViewport ? m_levelMapViewport->gridSize() : 8.0;
	if (levelMapPatchShapeBounds(m_levelMapDocument, grid, mins, maxs)) {
		return;
	}
	// Nothing selected: Radiant's work zone, the last selection's bounds,
	// which the views remember on each axis (64 units from 0 before any).
	double low[3] = {0, 0, 0};
	double high[3] = {64, 64, 64};
	if (m_levelMapViewport) {
		for (int axis = 0; axis < 3; ++axis) {
			const auto range = m_levelMapViewport->brushDepthRange(axis);
			low[axis] = range.first;
			high[axis] = range.second > range.first + 1 ? range.second : range.first + std::max(grid, 1.0) * 2;
		}
	}
	*mins = {low[0], low[1], low[2], true};
	*maxs = {high[0], high[1], high[2], true};
}

QSize ApplicationShell::levelPatchImageSize(const QString& material) const
{
	const QString token = levelPatchMaterialToken(material, false);
	const QString key = materialKey(token);
	if (key.isEmpty()) {
		return {};
	}
	// The camera's loaded materials, then sizes the surface tools resolved.
	for (const LevelPreviewMaterial& candidate : std::as_const(m_levelPreviewAssets.materials)) {
		if (candidate.key == key && candidate.ready() && candidate.sourceSize.isValid()) {
			return candidate.sourceSize;
		}
	}
	for (auto it = m_levelSurfaceSizes.cbegin(); it != m_levelSurfaceSizes.cend(); ++it) {
		if (materialKey(it.key()) == key && it.value().isValid()) {
			return it.value();
		}
	}
	// Else read just this material from the open package, as the camera does.
	std::shared_ptr<const PackageArchiveReader> archive;
	if (m_packageStaging.isLoaded()) {
		archive = std::make_shared<PackageStagingArchive>(m_packageStaging);
	} else {
		archive = m_packageArchive.snapshotReader();
	}
	if (!archive && m_packageArchive.isOpen()) {
		archive = std::make_shared<const PackageArchive>(m_packageArchive);
	}
	if (!archive) {
		return {};
	}
	LevelMapDocument probe;
	probe.format = m_levelMapDocument.format;
	probe.engineFamily = m_levelMapDocument.engineFamily;
	LevelMapBrush brush;
	LevelMapBrushFace face;
	face.textureName = token;
	brush.faces << face;
	probe.brushes << brush;
	LevelPreviewAssetOptions options;
	options.paletteId = activePaletteId();
	options.materialLimit = 1;
	options.modelLimit = 0;
	options.previewDimension = 16;
	const LevelPreviewAssets assets = resolveLevelPreviewAssets(probe, *archive, options);
	return levelPreviewTextureSizes(assets).value(key);
}

void ApplicationShell::addLevelPatchPrefabFromUi(const QString& shape)
{
	setMode(StudioMode::Levels);
	if (m_levelMapDocument.format != LevelMapFormat::Quake3Map) {
		statusBar()->showMessage(tr("Open a Quake III map to add patches."), 6000);
		return;
	}
	LevelShapeRequest request;
	request.shape = shape;
	levelPatchCreationBox(&request.mins, &request.maxs);
	request.axis = levelShapeViewAxis();
	// Radiant's prefab densities: four spans round (nine columns) and a
	// sphere's two from pole to pole (five rows).
	request.sides = 4;
	request.bands = 2;
	request.texture = levelBrushMaterial();
	request.patchMapping = LevelPatchCreateMapping::Radiant;
	placeLevelPatchShapeFromUi(request);
}

void ApplicationShell::addLevelExactPatchFromUi(const QString& shape)
{
	setMode(StudioMode::Levels);
	if (m_levelMapDocument.format != LevelMapFormat::Quake3Map) {
		statusBar()->showMessage(tr("Open a Quake III map to add patches."), 6000);
		return;
	}
	// NetRadiant Custom's Patch density dialog (DoNewPatchDlg): odd column
	// counts from 7 to 31, and rows from 3 (a sphere's from 5) to 31, starting
	// at 13 by 3 (a sphere's 13 by 7). The last choice for each shape returns.
	const bool sphere = shape == QStringLiteral("patch-sphere");
	const int fewestRows = sphere ? 5 : 3;
	const QString settingsKey = QStringLiteral("levelPatchDensity");
	QJsonObject remembered = QJsonDocument::fromJson(m_settings.shellLayoutState(settingsKey)).object();
	const QJsonObject last = remembered.value(shape).toObject();
	const auto oddIn = [](int value, int low, int high) { return std::clamp(value | 1, low, high); };
	const int startColumns = oddIn(last.value(QStringLiteral("columns")).toInt(13), 7, kLevelPatchMaxDimension);
	const int startRows = oddIn(last.value(QStringLiteral("rows")).toInt(sphere ? 7 : 3), fewestRows, kLevelPatchMaxDimension);

	LevelShapeRequest request;
	request.shape = shape;
	levelPatchCreationBox(&request.mins, &request.maxs);
	request.axis = levelShapeViewAxis();
	// The selected brushes and patches the patch takes the place of: hidden
	// ones stay, as replaceLevelMapSelectionWithPatch leaves them.
	const QSet<QString> hidden = levelMapHiddenObjects(m_levelMapDocument);
	int replacing = 0;
	for (const LevelMapSelectionRef& ref : std::as_const(m_levelMapDocument.selection)) {
		if (hidden.contains(levelMapSelectionRefId(ref))) {
			continue;
		}
		if (ref.kind == LevelMapSelectionKind::QuakeBrush) {
			replacing += std::any_of(m_levelMapDocument.brushes.cbegin(), m_levelMapDocument.brushes.cend(),
				[&ref](const LevelMapBrush& brush) { return brush.id == ref.objectId; }) ? 1 : 0;
		} else if (ref.kind == LevelMapSelectionKind::QuakePatch) {
			replacing += std::any_of(m_levelMapDocument.patches.cbegin(), m_levelMapDocument.patches.cend(),
				[&ref](const LevelMapPatch& patch) { return patch.id == ref.objectId; }) ? 1 : 0;
		}
	}

	QDialog dialog(this);
	dialog.setObjectName(QStringLiteral("levelPatchDensityDialog"));
	dialog.setWindowTitle(tr("Patch Density"));
	dialog.setAccessibleName(tr("Patch Density for the %1").arg(levelShapeLabel(shape)));
	auto* form = new QFormLayout(&dialog);
	form->setRowWrapPolicy(QFormLayout::WrapLongRows);
	form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	const QLocale locale;
	auto* columns = new QComboBox(&dialog);
	columns->setObjectName(QStringLiteral("patchDensityColumns"));
	columns->setAccessibleName(tr("Control columns round the patch"));
	columns->setToolTip(tr("Control points round the ring: each two columns are one quadratic span of the circle."));
	for (int count = 7; count <= kLevelPatchMaxDimension; count += 2) {
		columns->addItem(locale.toString(count), count);
	}
	columns->setCurrentIndex(columns->findData(startColumns));
	auto* rows = new QComboBox(&dialog);
	rows->setObjectName(QStringLiteral("patchDensityRows"));
	rows->setAccessibleName(sphere ? tr("Control rows from pole to pole") : tr("Control rows along the patch"));
	rows->setToolTip(sphere ? tr("Control points from the bottom pole to the top one: each two rows are one span of the half circle.")
							: tr("Control points along the axis, from the base to the far end."));
	for (int count = fewestRows; count <= kLevelPatchMaxDimension; count += 2) {
		rows->addItem(locale.toString(count), count);
	}
	rows->setCurrentIndex(rows->findData(startRows));
	auto* square = new QCheckBox(tr("Re-disperse columns and rows"), &dialog);
	square->setObjectName(QStringLiteral("patchDensitySquare"));
	square->setAccessibleName(tr("Square the patch"));
	square->setToolTip(tr("Once the patch is mapped, put each span's middle control point halfway between its ends, so the patch is made of "
						  "flat facets, as NetRadiant Custom's Square box does."));
	square->setChecked(last.value(QStringLiteral("square")).toBool(false));
	form->addRow(tr("Columns"), columns);
	form->addRow(tr("Rows"), rows);
	form->addRow(tr("Square"), square);
	// What will happen, before it does.
	static const char* const axisNames[] = {"X", "Y", "Z"};
	const QString axisName = QString::fromLatin1(axisNames[std::clamp(request.axis, 0, 2)]);
	auto* note = new QLabel(&dialog);
	note->setObjectName(QStringLiteral("patchDensityNote"));
	note->setWordWrap(true);
	note->setTextFormat(Qt::PlainText);
	if (replacing > 0) {
		note->setText(tr("The patch fills the selection's bounds along the %1 axis and takes the place of %n selected brush(es) and patch(es), "
						 "in one undo step.", nullptr, replacing)
				.arg(axisName));
	} else if (m_levelMapDocument.selection.isEmpty()) {
		note->setText(tr("The patch fills the work zone, the last selection's bounds, along the %1 axis.").arg(axisName));
	} else {
		note->setText(tr("The patch fills the selection's bounds along the %1 axis.").arg(axisName));
	}
	form->addRow(note);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	buttons->setObjectName(QStringLiteral("patchDensityButtons"));
	form->addRow(buttons);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	if (dialog.exec() != QDialog::Accepted) {
		return;
	}
	const int columnCount = columns->currentData().toInt();
	const int rowCount = rows->currentData().toInt();
	remembered.insert(shape, QJsonObject {{QStringLiteral("columns"), columnCount}, {QStringLiteral("rows"), rowCount},
								 {QStringLiteral("square"), square->isChecked()}});
	m_settings.setShellLayoutState(settingsKey, QJsonDocument(remembered).toJson(QJsonDocument::Compact));

	// NetRadiant Custom's Exact shapes: true circles of (columns - 1) / 2
	// spans, ring starting at angle 0, mapped Natural, then squared if asked.
	request.sides = (columnCount - 1) / 2;
	if (sphere) {
		request.bands = (rowCount - 1) / 2;
	} else {
		request.rows = rowCount;
	}
	request.patchRingStart = LevelPatchRingStart::Exact;
	request.patchSquare = square->isChecked();
	request.texture = levelBrushMaterial();
	request.patchMapping = LevelPatchCreateMapping::Radiant;
	placeLevelPatchShapeFromUi(request);
}

void ApplicationShell::placeLevelPatchShapeFromUi(const LevelShapeRequest& prepared)
{
	LevelShapeRequest request = prepared;
	request.patchImageSize = levelPatchImageSize(request.texture);
	const QString& shape = request.shape;
	const QString label = levelShapeLabel(shape);
	int patchId = -1;
	QString error;
	QVector<LevelMapSelectionRef> taken;
	if (!replaceLevelMapSelectionWithPatchShape(&m_levelMapDocument, request, &patchId, &error, &taken)) {
		statusBar()->showMessage(tr("Could not make the %1: %2").arg(label, error), 6000);
		return;
	}
	rememberLevelMaterial(request.texture);
	recordActivity(tr("Patch shape added"), QStringLiteral("%1 patch:%2").arg(shape).arg(patchId), QStringLiteral("level-map"), OperationState::Warning,
		tr("Unsaved map edit"));
	refreshLevelMapWorkbench();
	const int replaced = static_cast<int>(taken.size());
	QString message = replaced > 0 ? tr("%1 replaced %n brush(es) and patch(es) as patch %2, selected.", nullptr, replaced).arg(label).arg(patchId)
									: tr("%1 added as patch %2, selected.").arg(label).arg(patchId);
	if (!request.patchImageSize.isValid()) {
		message += QLatin1Char(' ') + tr("No image was found for %1, so it is mapped as 64 by 64 texels.").arg(request.texture);
	}
	statusBar()->showMessage(message, 8000);
}

} // namespace vibestudio
