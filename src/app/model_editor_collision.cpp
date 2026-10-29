#include "app/model_editor_dialog.h"
#include "app/model_viewport.h"
#include "app/wrapping_action_button.h"
#include "app/wrapping_check_box.h"
#include "core/model_collision_hull.h"
#include "core/package_archive.h"
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>

namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(VibeStudioModelCollisionEditor)
};
ModelVec3 vector(QDoubleSpinBox *const *fields)
{
	return {float(fields[0]->value()), float(fields[1]->value()), float(fields[2]->value())};
}
void setVector(QDoubleSpinBox **fields, ModelVec3 v)
{
	fields[0]->setValue(v.x);
	fields[1]->setValue(v.y);
	fields[2]->setValue(v.z);
}
} // namespace
void ModelEditorDialog::addCollisionControls(QFormLayout *form)
{
	const auto combo = [&](const char *name, const QString &label) {
		auto *field = new QComboBox;
		field->setObjectName(QString::fromLatin1(name));
		field->setAccessibleName(label);
		field->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
		field->setMinimumContentsLength(12);
		form->addRow(label, field);
		return field;
	};
	m_collisionSummary = new LiveStatusLabel;
	m_collisionSummary->setObjectName("meshCollisionSummary");
	m_collisionSummary->setAccessibleName(Text::tr("Collision status"));
	m_collisionSummary->setTextFormat(Qt::PlainText);
	m_collisionSummary->setWordWrap(true);
	m_collisionSummary->setMinimumWidth(0);
	m_collisionSummary->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	form->addRow(m_collisionSummary);
	m_showCollision = new WrappingCheckBox(Text::tr("Show collision boxes"));
	m_showCollision->setObjectName("meshShowCollision");
	m_showCollision->setAccessibleName(m_showCollision->text());
	m_showCollision->setToolTip(
		Text::tr("Draw collision boxes at the preview pose. The selected box has solid thicker edges; other boxes have dashed edges."));
	m_showCollision->setChecked(true);
	m_preview->setShowCollision(true);
	form->addRow(m_showCollision);
	connect(m_showCollision, &QCheckBox::toggled, m_preview, &ModelViewport::setShowCollision);
	m_collisionBoxes = combo("meshCollisionBoxes", Text::tr("Collision box"));
	m_collisionName = new QLineEdit;
	m_collisionName->setObjectName("meshCollisionName");
	m_collisionName->setAccessibleName(Text::tr("Collision box name"));
	m_collisionName->setMaxLength(128);
	m_collisionName->setText("collision_1");
	form->addRow(Text::tr("Name"), m_collisionName);
	const auto axes = [&](const QString &label, const char *name, QDoubleSpinBox **fields, double low, double high, double initial) {
		for (int axis = 0; axis < 3; ++axis)
		{
			auto *field = new QDoubleSpinBox;
			field->setDecimals(4);
			field->setRange(low, high);
			field->setValue(initial);
			field->setLayoutDirection(Qt::LeftToRight);
			field->setMinimumWidth(0);
			field->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
			field->setObjectName(QString::fromLatin1(name) + QString::number(axis));
			const auto title = Text::tr("%1 %2").arg(label, QString(QChar('X' + axis)));
			field->setAccessibleName(title);
			form->addRow(title, field);
			fields[axis] = field;
		}
	};
	axes(Text::tr("Centre"), "meshCollisionCentre", m_collisionCentre, -32768, 32768, 0);
	axes(Text::tr("Size"), "meshCollisionSize", m_collisionSize, 1, 65536, 16);
	axes(Text::tr("Rotation"), "meshCollisionRotation", m_collisionRotation, -36000, 36000, 0);
	const auto button = [&](const char *name, const QString &label, ModelEditKind kind, bool selected) {
		auto *control = new WrappingActionButton(label, 2);
		control->setObjectName(QString::fromLatin1(name));
		control->setAccessibleName(label);
		form->addRow(control);
		connect(control, &QPushButton::clicked, this, [this, kind] { executeCollision(kind); });
		if (selected)
		{
			m_collisionSelectedButtons << control;
		}
		return control;
	};
	button("addMeshCollision", Text::tr("Add Box"), ModelEditKind::AddCollisionBox, false);
	m_collisionPoseScope = combo("meshCollisionPoseScope", Text::tr("Edit poses"));
	m_collisionPoseScope->addItems({Text::tr("All frames"), Text::tr("Current frame")});
	m_collisionPoseScope->setToolTip(Text::tr("Animated boxes: apply absolute values or transforms to the current pose or every pose. "
											  "Shared with Geometry transform scope. Static boxes always use all frames."));
	m_collisionPoseScope->setAccessibleDescription(m_collisionPoseScope->toolTip());
	connect(m_collisionPoseScope, &QComboBox::currentIndexChanged, m_frameScope, &QComboBox::setCurrentIndex);
	connect(m_frameScope, &QComboBox::currentIndexChanged, m_collisionPoseScope, &QComboBox::setCurrentIndex);
	button("updateMeshCollision", Text::tr("Apply Box"), ModelEditKind::UpdateCollisionBox, true);
	m_collisionAnimate = button("animateMeshCollision", Text::tr("Animate Box"), ModelEditKind::AnimateCollisionBox, true);
	m_collisionFreeze = button("freezeMeshCollision", Text::tr("Make Static from Current Frame"), ModelEditKind::FreezeCollisionBox, true);
	m_collisionAnimate->setToolTip(Text::tr("Copy this static box into every stored pose. Collision poses follow playback and frame edits, "
											"and remain independent of later mesh edits."));
	m_collisionAnimate->setAccessibleDescription(m_collisionAnimate->toolTip());
	m_collisionFreeze->setToolTip(Text::tr("Keep the current stored collision pose for every frame. Undo restores the complete track."));
	m_collisionFreeze->setAccessibleDescription(m_collisionFreeze->toolTip());
	button("duplicateMeshCollision", Text::tr("Duplicate Box"), ModelEditKind::DuplicateCollisionBox, true);
	button("deleteMeshCollision", Text::tr("Delete Box"), ModelEditKind::DeleteCollisionBox, true);
	m_collisionFitScope = combo("meshCollisionFitScope", Text::tr("Fit poses"));
	m_collisionFitScope->addItems({Text::tr("All frames"), Text::tr("Current frame")});
	auto *fit = button("fitMeshCollision", Text::tr("Fit New Box"), ModelEditKind::FitCollisionBox, false);
	fit->setToolTip(Text::tr("Fit an axis-aligned static box to selected vertices, edges or faces; without mesh components, fit every "
							 "surface. Thin dimensions expand to one unit. The box does not follow later mesh edits or animation."));
	fit->setAccessibleDescription(fit->toolTip());
	auto *fitAnimated = button("fitAnimatedMeshCollision", Text::tr("Fit New Animated Box"), ModelEditKind::FitAnimatedCollisionBox, false);
	fitAnimated->setToolTip(Text::tr("Fit an independent axis-aligned box in every stored frame, using selected components or the whole "
									 "model. Later mesh edits do not refit these poses."));
	fitAnimated->setAccessibleDescription(fitAnimated->toolTip());
	m_collisionHullProfile = combo("meshCollisionHullProfile", Text::tr("Hull budget"));
	m_collisionHullProfile->addItem(Text::tr("Map brush (128 faces)"), "quake3");
	m_collisionHullProfile->addItem(Text::tr("Doom 3 trace model (16 faces, 32 edges, 32 vertices)"), "doom3-trace");
	m_collisionHullProfile->setToolTip(Text::tr("The most faces, edges and vertices a fitted hull may have. A hull over budget is "
												"simplified conservatively: it only grows, so every vertex stays inside."));
	m_collisionHullProfile->setAccessibleDescription(m_collisionHullProfile->toolTip());
	m_collisionHullFaces = new QSpinBox;
	m_collisionHullFaces->setObjectName("meshCollisionHullFaces");
	m_collisionHullFaces->setRange(0, 128);
	m_collisionHullFaces->setSpecialValueText(Text::tr("Budget limit"));
	m_collisionHullFaces->setLayoutDirection(Qt::LeftToRight);
	m_collisionHullFaces->setAccessibleName(Text::tr("Maximum hull faces"));
	m_collisionHullFaces->setToolTip(Text::tr("A lower face limit than the budget's, from 4; Budget limit keeps the budget's own."));
	m_collisionHullFaces->setAccessibleDescription(m_collisionHullFaces->toolTip());
	form->addRow(Text::tr("Maximum hull faces"), m_collisionHullFaces);
	auto *fitHull = button("fitMeshCollisionHull", Text::tr("Fit New Hull"), ModelEditKind::FitCollisionHull, false);
	fitHull->setToolTip(Text::tr("Fit a convex hull to the selected vertices, edges or faces, or to the whole model, over the poses Fit "
								 "poses chooses. Transform, animate and export it like a box; its shape stays the same in every pose."));
	fitHull->setAccessibleDescription(fitHull->toolTip());
	auto *fitAnimatedHull = button("fitAnimatedMeshCollisionHull", Text::tr("Fit New Animated Hull"), ModelEditKind::FitAnimatedCollisionHull, false);
	fitAnimatedHull->setToolTip(Text::tr("Fit a convex hull separately in every stored frame, each within the budget, so its shape "
										 "follows the animation. Between stored frames the nearer frame's shape holds."));
	fitAnimatedHull->setAccessibleDescription(fitAnimatedHull->toolTip());
	m_collisionTarget = combo("meshCollisionTarget", Text::tr("Map target"));
	m_collisionTarget->addItem(Text::tr("Quake clip hull"), "quake");
	m_collisionTarget->addItem(Text::tr("Quake II player clip"), "quake2");
	m_collisionTarget->addItem(Text::tr("Quake III clip shader"), "quake3");
	m_collisionMaterial = new QLineEdit;
	m_collisionMaterial->setObjectName("meshCollisionMaterial");
	m_collisionMaterial->setAccessibleName(Text::tr("Quake III clip shader"));
	m_collisionMaterial->setLayoutDirection(Qt::LeftToRight);
	m_collisionMaterial->setPlaceholderText("common/clip");
	m_collisionMaterial->setToolTip(Text::tr("Project shader below textures/, without that prefix. Its compiler definition must include "
											 "surfaceparm playerclip and nodraw. Availability is not verified here."));
	m_collisionMaterial->setAccessibleDescription(m_collisionMaterial->toolTip());
	form->addRow(Text::tr("Clip shader"), m_collisionMaterial);
	axes(Text::tr("Map origin"), "meshCollisionMapOrigin", m_collisionOrigin, -32768, 32768, 0);
	m_collisionExport = new WrappingActionButton(Text::tr("Export Collision Map…"), 2);
	m_collisionExport->setObjectName("exportMeshCollision");
	m_collisionExport->setAccessibleName(m_collisionExport->text());
	form->addRow(m_collisionExport);
	m_collisionPlace = new WrappingActionButton(Text::tr("Place Collision in Level"), 2);
	m_collisionPlace->setObjectName("placeMeshCollision");
	m_collisionPlace->setAccessibleName(m_collisionPlace->text());
	const auto handoffHelp = Text::tr(
		"Export or place every box at the current stored frame as static map brushes. Collision animation remains in the mesh source.");
	for (auto *control : {m_collisionExport, m_collisionPlace})
	{
		control->setToolTip(handoffHelp);
		control->setAccessibleDescription(handoffHelp);
	}
	form->addRow(m_collisionPlace);
	m_collisionPlaceInstance = new WrappingActionButton(Text::tr("Place for Selected Model Instance"), 2);
	m_collisionPlaceInstance->setObjectName("placeMeshCollisionInstance");
	m_collisionPlaceInstance->setAccessibleName(m_collisionPlaceInstance->text());
	m_collisionPlaceInstance->setToolTip(Text::tr("Place the boxes through the transform of the model entity selected in the level: its "
												  "origin, angles and modelscale. Map origin is not used."));
	m_collisionPlaceInstance->setAccessibleDescription(m_collisionPlaceInstance->toolTip());
	form->addRow(m_collisionPlaceInstance);
	m_collisionRefresh = new WrappingActionButton(Text::tr("Refresh Placed Collision…"), 2);
	m_collisionRefresh->setObjectName("refreshMeshCollision");
	m_collisionRefresh->setAccessibleName(m_collisionRefresh->text());
	m_collisionRefresh->setToolTip(Text::tr("List collision placed from this model in the open level, current or stale, and replace one "
											"set with the current boxes as one level undo step."));
	m_collisionRefresh->setAccessibleDescription(m_collisionRefresh->toolTip());
	form->addRow(m_collisionRefresh);
	m_collisionDoom3Material = new QLineEdit;
	m_collisionDoom3Material->setObjectName("meshCollisionDoom3Material");
	m_collisionDoom3Material->setAccessibleName(Text::tr("Doom 3 collision material"));
	m_collisionDoom3Material->setLayoutDirection(Qt::LeftToRight);
	m_collisionDoom3Material->setText(QStringLiteral("textures/common/collision"));
	m_collisionDoom3Material->setToolTip(Text::tr("A material whose definition has the collision keyword. In Doom 3, when a model has "
												  "such surfaces, only they collide. With a package open, the export checks its material scripts."));
	m_collisionDoom3Material->setAccessibleDescription(m_collisionDoom3Material->toolTip());
	form->addRow(Text::tr("Doom 3 collision material"), m_collisionDoom3Material);
	m_collisionAseExport = new WrappingActionButton(Text::tr("Export ASE with Collision…"), 2);
	m_collisionAseExport->setObjectName("exportMeshCollisionAse");
	m_collisionAseExport->setAccessibleName(m_collisionAseExport->text());
	m_collisionAseExport->setToolTip(Text::tr("Write the current frame as an ASE model with one collision surface per box or hull, so "
											  "Doom 3 collides with those instead of the render surfaces."));
	m_collisionAseExport->setAccessibleDescription(m_collisionAseExport->toolTip());
	form->addRow(m_collisionAseExport);
	m_collisionClipExport = new WrappingActionButton(Text::tr("Export Doom 3 Clip Model…"), 2);
	m_collisionClipExport->setObjectName("exportMeshCollisionClipModel");
	m_collisionClipExport->setAccessibleName(m_collisionClipExport->text());
	m_collisionClipExport->setToolTip(Text::tr("Write the selected box or hull alone as an ASE for an entity's clipmodel key. It must fit a "
											   "Doom 3 trace model: 16 faces, 32 edges, 32 vertices."));
	m_collisionClipExport->setAccessibleDescription(m_collisionClipExport->toolTip());
	form->addRow(m_collisionClipExport);
	m_collisionSelectedButtons << m_collisionClipExport;
	m_collisionCmExport = new WrappingActionButton(Text::tr("Export Doom 3 .cm File…"), 2);
	m_collisionCmExport->setObjectName("exportMeshCollisionCm");
	m_collisionCmExport->setAccessibleName(m_collisionCmExport->text());
	m_collisionCmExport->setToolTip(Text::tr("Write every box and hull as a Doom 3 collision model file. Doom 3 reads it beside a static "
											 "model instead of converting the model's surfaces."));
	m_collisionCmExport->setAccessibleDescription(m_collisionCmExport->toolTip());
	form->addRow(m_collisionCmExport);
	const auto doom3Export = [this](bool clip) {
		if (m_working)
			return;
		m_preview->pause();
		const auto path = QFileDialog::getSaveFileName(this, clip ? Text::tr("Export Doom 3 Clip Model") : Text::tr("Export ASE with Collision"),
													   {}, Text::tr("ASCII Scene Export (*.ase)"));
		if (path.isEmpty())
			return;
		ModelCollisionSurfaceExport request;
		request.material = m_collisionDoom3Material->text().trimmed();
		request.frame = m_frame->currentIndex();
		QString error;
		const bool exported = clip ? exportCollisionClipModel(path, m_document.selection().collision, request, true, &error)
								   : exportCollisionAse(path, request, true, &error);
		if (!exported)
			m_status->setText(error, LiveStatus::Problem);
	};
	connect(m_collisionAseExport, &QPushButton::clicked, this, [doom3Export] { doom3Export(false); });
	connect(m_collisionClipExport, &QPushButton::clicked, this, [doom3Export] { doom3Export(true); });
	connect(m_collisionCmExport, &QPushButton::clicked, this, [this] {
		if (m_working)
			return;
		m_preview->pause();
		const auto path =
			QFileDialog::getSaveFileName(this, Text::tr("Export Doom 3 Collision Model"), {}, Text::tr("Doom 3 collision model (*.cm)"));
		if (path.isEmpty())
			return;
		// The collisionModel name is the model's game path: guess it from a
		// models/ folder in the chosen path, then let the user confirm it.
		const auto normal = QDir::fromNativeSeparators(path);
		const int models = normal.lastIndexOf(QStringLiteral("/models/"), -1, Qt::CaseInsensitive);
		auto guess = models >= 0 ? normal.mid(models + 1) : QStringLiteral("models/") + QFileInfo(normal).fileName();
		guess = guess.left(guess.lastIndexOf(QLatin1Char('.'))) + QStringLiteral(".ase");
		bool accepted = false;
		const auto modelPath = QInputDialog::getText(this, Text::tr("Model Game Path"),
													 Text::tr("The static model's path in the game, which Doom 3 matches to this file:"),
													 QLineEdit::Normal, guess, &accepted)
								   .trimmed();
		if (!accepted || modelPath.isEmpty())
			return;
		ModelCollisionSurfaceExport request;
		request.material = m_collisionDoom3Material->text().trimmed();
		request.frame = m_frame->currentIndex();
		QString error;
		if (!exportCollisionModelFile(path, modelPath, request, true, &error))
			m_status->setText(error, LiveStatus::Problem);
	});
	connect(m_collisionExport, &QPushButton::clicked, this, [this] { handoffCollision(false); });
	connect(m_collisionPlace, &QPushButton::clicked, this, [this] { handoffCollision(true); });
	connect(m_collisionPlaceInstance, &QPushButton::clicked, this, [this] {
		if (m_working)
			return;
		m_preview->pause();
		QString error;
		if (!placeCollisionForInstance(-1, &error))
			m_status->setText(error, LiveStatus::Problem);
	});
	connect(m_collisionRefresh, &QPushButton::clicked, this, [this] { chooseCollisionRefresh(); });
	connect(m_collisionTarget, &QComboBox::currentIndexChanged, this, [this] { refreshCollision(); });
	connect(m_collisionBoxes, &QComboBox::currentIndexChanged, this, [this] {
		if (m_refreshing || m_working)
		{
			return;
		}
		ModelSelection selection;
		selection.surface = m_document.selection().surface;
		selection.collision = m_collisionBoxes->currentData().toString();
		m_document.setSelection(selection);
		refresh();
	});
}
void ModelEditorDialog::refreshCollision()
{
	if (!m_collisionBoxes)
	{
		return;
	}
	const auto &mesh = m_document.mesh();
	const auto name = m_document.selection().collision;
	const auto box = findModelCollisionBox(mesh, name);
	const QSignalBlocker block(m_collisionBoxes);
	if (m_collisionRevision != m_document.revisionFingerprint())
	{
		m_collisionBoxes->clear();
		m_collisionBoxes->addItem(Text::tr("No collision selection"), QString());
		for (const auto &entry : mesh.collisionBoxes)
		{
			m_collisionBoxes->addItem(entry.name, entry.name);
		}
	}
	m_collisionBoxes->setCurrentIndex(std::max(0, m_collisionBoxes->findData(name)));
	const int frame = m_frame->currentIndex();
	ModelCollisionBox pose;
	if (box && sampleModelCollisionBox(*box, frame, frame, 0, &pose) &&
		(name != m_displayedCollision || m_collisionRevision != m_document.revisionFingerprint() || frame != m_displayedCollisionFrame))
	{
		m_collisionName->setText(box->name);
		setVector(m_collisionCentre, pose.centre);
		setVector(m_collisionSize, pose.size);
		setVector(m_collisionRotation, pose.rotation);
	}
	m_displayedCollisionFrame = frame;
	m_displayedCollision = name;
	m_collisionRevision = m_document.revisionFingerprint();
	for (auto *button : m_collisionSelectedButtons)
	{
		button->setEnabled(box != nullptr);
	}
	const bool animated = box && !box->framePoses.isEmpty();
	m_collisionAnimate->setEnabled(box && !animated);
	m_collisionFreeze->setEnabled(animated);
	m_collisionPoseScope->setEnabled(animated);
	m_collisionPoseScope->setCurrentIndex(m_frameScope->currentIndex());
	m_preview->setEditCollision(name);
	m_collisionMaterial->setEnabled(m_collisionTarget->currentData() == "quake3");
	m_collisionExport->setEnabled(!mesh.collisionBoxes.isEmpty());
	QString hullText;
	ModelCollisionHullShape shape;
	if (box && isModelCollisionHull(*box) && modelCollisionHullShape(box->hull, &shape))
	{
		const int vertices = int(shape.vertices.size()), edges = int(shape.edges.size()), faces = int(shape.faces.size());
		hullText = QStringLiteral("\n") + Text::tr("Convex hull: %1, %2, %3.")
											  .arg(Text::tr("%n vertex(es)", nullptr, vertices), Text::tr("%n edge(s)", nullptr, edges),
												   Text::tr("%n face(s)", nullptr, faces)) +
				   QStringLiteral(" ") +
				   (faces <= 16 && edges <= 32 && vertices <= 32
						? Text::tr("Within the Doom 3 trace model budget and the map brush budget.")
						: Text::tr("Within the map brush budget; over the Doom 3 trace model budget (16 faces, 32 edges, 32 vertices)."));
	}
	m_collisionSummary->setText(
		Text::tr("%1 of 64 collision boxes. Source, undo and recovery retain them; model exports omit them.")
			.arg(mesh.collisionBoxes.size()) +
		QStringLiteral("\n") +
		(animated ? Text::tr("Animated box · stored frame %1 of %2. Map handoff samples the current stored frame as static brushes.")
						.arg(frame)
						.arg(mesh.frames.size())
				  : Text::tr("Static box. Animate Box creates a pose for every mesh frame.")) +
		QStringLiteral("\n") +
		(m_collisionTarget->currentData() == "quake3"
			 ? Text::tr("Quake III needs a project clip shader in the compiler assets. Export does not verify its behavior.")
		 : m_collisionTarget->currentData() == "quake2" ? Text::tr("Quake II: player movement only; no monster or projectile collision.")
														: Text::tr("Quake: expanded clip hulls; no point-trace collision.")) +
		hullText);
	refreshContext();
}
void ModelEditorDialog::executeCollision(ModelEditKind kind)
{
	if (m_working)
	{
		return;
	}
	m_preview->pause();
	ModelEdit edit;
	edit.kind = kind;
	edit.selection = m_document.selection();
	edit.collisionBox = {m_collisionName->text(), vector(m_collisionCentre), vector(m_collisionSize), vector(m_collisionRotation)};
	if ((kind == ModelEditKind::AddCollisionBox || kind == ModelEditKind::FitCollisionBox ||
		 kind == ModelEditKind::FitAnimatedCollisionBox || kind == ModelEditKind::FitCollisionHull ||
		 kind == ModelEditKind::FitAnimatedCollisionHull || kind == ModelEditKind::DuplicateCollisionBox) &&
		findModelCollisionBox(m_document.mesh(), edit.collisionBox.name))
	{
		const auto stem = edit.collisionBox.name.left(115);
		int suffix = 2;
		while (findModelCollisionBox(m_document.mesh(), stem + '_' + QString::number(suffix)))
		{
			++suffix;
		}
		edit.collisionBox.name = stem + '_' + QString::number(suffix);
	}
	edit.frame = (kind == ModelEditKind::FitCollisionBox || kind == ModelEditKind::FitCollisionHull) && m_collisionFitScope->currentIndex() == 1
					 ? m_frame->currentIndex()
					 : -1;
	if (kind == ModelEditKind::FitCollisionHull || kind == ModelEditKind::FitAnimatedCollisionHull)
	{
		edit.collisionHullProfile = m_collisionHullProfile->currentData().toString();
		edit.collisionHullMaxFaces = m_collisionHullFaces->value();
		if (edit.collisionHullProfile == QStringLiteral("doom3-trace") && edit.collisionHullMaxFaces > 16)
			edit.collisionHullMaxFaces = 16;
	}
	const auto box = findModelCollisionBox(m_document.mesh(), edit.selection.collision);
	if (kind == ModelEditKind::FreezeCollisionBox ||
		(kind == ModelEditKind::UpdateCollisionBox && box && !box->framePoses.isEmpty() && m_collisionPoseScope->currentIndex() == 1))
		edit.frame = m_frame->currentIndex();
	QString error;
	if (!applyEdit(edit, &error))
	{
		m_status->setText(error);
	}
	else if (kind == ModelEditKind::AnimateCollisionBox || kind == ModelEditKind::FitAnimatedCollisionBox ||
			 kind == ModelEditKind::FitAnimatedCollisionHull)
		m_frameScope->setCurrentIndex(1);
}
ModelCollisionExport ModelEditorDialog::collisionRequest() const
{
	ModelCollisionExport request{m_collisionTarget->currentData().toString(),
								 m_collisionTarget->currentData() == "quake3" ? m_collisionMaterial->text() : QString(),
								 vector(m_collisionOrigin), m_frame->currentIndex()};
	request.sourcePath = collisionSourcePath();
	return request;
}
QString ModelEditorDialog::collisionSourcePath() const
{
	// The saved source, else the file the mesh was imported from; a recovered
	// draft records the source it was recovered from.
	if (!m_document.path().isEmpty())
		return m_document.path();
	if (!m_recoverySourcePath.isEmpty())
		return m_recoverySourcePath;
	return m_document.mesh().sourcePath.endsWith(QStringLiteral(".mesh.json"), Qt::CaseInsensitive) ? m_document.mesh().sourcePath
																									: QString();
}
bool ModelEditorDialog::exportCollision(const QString &path, const ModelCollisionExport &request, bool overwrite, QString *error)
{
	QString localError;
	if (!error)
	{
		error = &localError;
	}
	if (m_working || !path.endsWith(".map", Qt::CaseInsensitive))
	{
		*error = Text::tr("Choose a .map output while the modeller is idle.");
		return false;
	}
	const auto recoverySource = m_recoverySourcePath;
	ModelCollisionMap result;
	const bool ok = performWork(
		Text::tr("Export Collision Map"),
		[path, request, overwrite, recoverySource, &result](ModelDocument &candidate, QString *failure, const ModelWorkControl &control) {
			const auto target = inspectModelWriteTarget(path, control);
			if (!target.isValid())
			{
				*failure = target.error;
				return false;
			}
			if ((target.existed && !overwrite) || modelPathsReferToSameFile(path, candidate.path()) ||
				modelPathsReferToSameFile(path, candidate.mesh().sourcePath) || modelPathsReferToSameFile(path, recoverySource))
			{
				*failure = Text::tr("Protect source inputs and approve any existing derivative before replacing it.");
				return false;
			}
			return exportModelCollisionMap(candidate.mesh(), request, &result, failure, control) &&
				   writeModelFile(target, result.bytes, failure, control);
		},
		error, true);
	if (ok)
	{
		m_status->setText(Text::tr("Exported %Ln collision brush(es) to %1. %2", nullptr, result.brushCount).arg(path, result.notes.join(' ')),
						  LiveStatus::Result);
	}
	return ok;
}
bool ModelEditorDialog::exportCollisionAse(const QString &path, const ModelCollisionSurfaceExport &request, bool overwrite, QString *error)
{
	QString localError;
	if (!error)
		error = &localError;
	if (m_working || !path.endsWith(".ase", Qt::CaseInsensitive))
	{
		*error = Text::tr("Choose an .ase output while the modeller is idle.");
		return false;
	}
	const auto recoverySource = m_recoverySourcePath;
	const auto archive = m_materialSource.archive;
	ModelExportReport report;
	ModelDoom3CollisionMaterialCheck material;
	const bool ok = performWork(
		Text::tr("Export ASE with Collision"),
		[path, request, overwrite, recoverySource, archive, &report, &material](ModelDocument &candidate, QString *failure,
																			   const ModelWorkControl &control) {
			const auto target = inspectModelWriteTarget(path, control);
			if (!target.isValid())
			{
				*failure = target.error;
				return false;
			}
			if ((target.existed && !overwrite) || modelPathsReferToSameFile(path, candidate.path()) ||
				modelPathsReferToSameFile(path, candidate.mesh().sourcePath) || modelPathsReferToSameFile(path, recoverySource))
			{
				*failure = Text::tr("Protect source inputs and approve any existing derivative before replacing it.");
				return false;
			}
			const auto bytes = exportModelAseWithCollision(candidate.mesh(), request, failure, control, &report);
			if (bytes.isEmpty() || !writeModelFile(target, bytes, failure, control))
				return false;
			// The open package's material scripts decide whether Doom 3 treats
			// these surfaces as collision; a missing keyword is reported, not refused.
			if (archive && archive->isOpen())
				material = checkModelDoom3CollisionMaterial(*archive, request.material, control);
			return true;
		},
		error, true);
	if (ok && !material.note.isEmpty())
		m_status->setText(Text::tr("Exported %1 with collision surfaces, but check the material: %2").arg(path, material.note),
						  LiveStatus::Problem);
	else if (ok)
		m_status->setText(Text::tr("Exported %1 with collision surfaces. %2").arg(path, report.notes.join(' ')), LiveStatus::Result);
	return ok;
}
bool ModelEditorDialog::exportCollisionClipModel(const QString &path, const QString &volume, const ModelCollisionSurfaceExport &request,
												 bool overwrite, QString *error)
{
	QString localError;
	if (!error)
		error = &localError;
	if (m_working || !path.endsWith(".ase", Qt::CaseInsensitive))
	{
		*error = Text::tr("Choose an .ase output while the modeller is idle.");
		return false;
	}
	const auto recoverySource = m_recoverySourcePath;
	ModelDoom3ClipModel clip;
	const bool ok = performWork(
		Text::tr("Export Doom 3 Clip Model"),
		[path, volume, request, overwrite, recoverySource, &clip](ModelDocument &candidate, QString *failure, const ModelWorkControl &control) {
			const auto target = inspectModelWriteTarget(path, control);
			if (!target.isValid())
			{
				*failure = target.error;
				return false;
			}
			if ((target.existed && !overwrite) || modelPathsReferToSameFile(path, candidate.path()) ||
				modelPathsReferToSameFile(path, candidate.mesh().sourcePath) || modelPathsReferToSameFile(path, recoverySource))
			{
				*failure = Text::tr("Protect source inputs and approve any existing derivative before replacing it.");
				return false;
			}
			return exportModelDoom3ClipModel(candidate.mesh(), volume, request, &clip, failure, control) &&
				   writeModelFile(target, clip.bytes, failure, control);
		},
		error, true);
	if (ok)
		m_status->setText(Text::tr("Exported clip model %1 (%2, %3, %4). %5")
							  .arg(path, Text::tr("%n face(s)", nullptr, clip.faces), Text::tr("%n edge(s)", nullptr, clip.edges),
								   Text::tr("%n vertex(es)", nullptr, clip.vertices), clip.notes.join(' ')),
						  LiveStatus::Result);
	return ok;
}
bool ModelEditorDialog::exportCollisionModelFile(const QString &path, const QString &modelPath, const ModelCollisionSurfaceExport &request,
												 bool overwrite, QString *error)
{
	QString localError;
	if (!error)
		error = &localError;
	if (m_working || !path.endsWith(".cm", Qt::CaseInsensitive))
	{
		*error = Text::tr("Choose a .cm output while the modeller is idle.");
		return false;
	}
	const auto recoverySource = m_recoverySourcePath;
	ModelDoom3CollisionModelFile file;
	const bool ok = performWork(
		Text::tr("Export Doom 3 Collision Model"),
		[path, modelPath, request, overwrite, recoverySource, &file](ModelDocument &candidate, QString *failure,
																	  const ModelWorkControl &control) {
			const auto target = inspectModelWriteTarget(path, control);
			if (!target.isValid())
			{
				*failure = target.error;
				return false;
			}
			if ((target.existed && !overwrite) || modelPathsReferToSameFile(path, candidate.path()) ||
				modelPathsReferToSameFile(path, candidate.mesh().sourcePath) || modelPathsReferToSameFile(path, recoverySource))
			{
				*failure = Text::tr("Protect source inputs and approve any existing derivative before replacing it.");
				return false;
			}
			return exportModelDoom3CollisionModelFile(candidate.mesh(), modelPath, request, &file, failure, control) &&
				   writeModelFile(target, file.bytes, failure, control);
		},
		error, true);
	if (ok)
		m_status->setText(Text::tr("Exported collision model %1 (%2). %3")
							  .arg(path, Text::tr("%n polygon(s)", nullptr, file.polygons), file.notes.join(' ')),
						  LiveStatus::Result);
	return ok;
}
bool ModelEditorDialog::placeCollision(const ModelCollisionExport &request, QString *error)
{
	QString localError;
	if (!error)
	{
		error = &localError;
	}
	if (m_working || !collisionDestination)
	{
		*error = Text::tr("Open a Quake-family level before placing collision.");
		return false;
	}
	const auto destination = collisionDestination();
	if (!destination.publish)
	{
		*error = Text::tr("No writable level destination is available.");
		return false;
	}
	LevelPlacementResult placed;
	if (!performWork(
			Text::tr("Place Collision"),
			[request, map = destination.document, &placed](ModelDocument &candidate, QString *failure, const ModelWorkControl &control) {
				placed = prepareModelCollisionPlacement(candidate.mesh(), request, map, control);
				if (!placed.succeeded)
				{
					*failure = placed.error;
				}
				return placed.succeeded;
			},
			error))
	{
		return false;
	}
	if (!destination.publish(placed.document, error))
	{
		return false;
	}
	m_status->setText(Text::tr("Placed static collision brushes in one func_group, as one level undo step. It records this model, so Refresh "
							   "Placed Collision can update it after the boxes change. Save and compile the map."),
					  LiveStatus::Result);
	return true;
}
bool ModelEditorDialog::placeCollisionForInstance(int entityId, QString *error)
{
	QString localError;
	if (!error)
		error = &localError;
	if (m_working || !collisionDestination)
	{
		*error = Text::tr("Open a Quake-family level before placing collision.");
		return false;
	}
	auto request = collisionRequest();
	if (!modelCollisionInstanceFromSelection(collisionDestination().document, &request, entityId, error))
		return false;
	return placeCollision(request, error);
}
QVector<ModelCollisionRecord> ModelEditorDialog::placedCollisionRecords() const
{
	if (!collisionDestination)
		return {};
	return levelCollisionRecords(collisionDestination().document, &m_document.mesh(), collisionSourcePath());
}
bool ModelEditorDialog::refreshPlacedCollision(int entityId, QString *error)
{
	QString localError;
	if (!error)
		error = &localError;
	if (m_working || !collisionDestination)
	{
		*error = Text::tr("Open the level that holds the placed collision first.");
		return false;
	}
	const auto destination = collisionDestination();
	if (!destination.publish)
	{
		*error = Text::tr("No writable level destination is available.");
		return false;
	}
	const auto source = collisionSourcePath();
	LevelPlacementResult refreshed;
	if (!performWork(
			Text::tr("Refresh Placed Collision"),
			[map = destination.document, source, entityId, &refreshed](ModelDocument &candidate, QString *failure,
																	   const ModelWorkControl &control) {
				// A record without a source (placed from an unsaved model) can be
				// refreshed only by choosing it explicitly, as here.
				refreshed = prepareModelCollisionRefresh(candidate.mesh(), source, map, entityId, -1, true, control);
				if (!refreshed.succeeded)
					*failure = refreshed.error;
				return refreshed.succeeded;
			},
			error))
	{
		return false;
	}
	if (!destination.publish(refreshed.document, error))
		return false;
	m_status->setText(Text::tr("Replaced the collision placed as entity %1 with the current boxes, as one level undo step. Save and compile the "
							   "map.")
						  .arg(entityId),
					  LiveStatus::Result);
	return true;
}
void ModelEditorDialog::chooseCollisionRefresh()
{
	if (m_working)
		return;
	m_preview->pause();
	const auto records = placedCollisionRecords();
	QStringList items;
	QVector<int> ids;
	int preferred = 0;
	bool stale = false;
	for (const auto &record : records)
	{
		// Only this model's records (or ones with no recorded source) can be refreshed here.
		if (record.status == ModelCollisionRecordStatus::Invalid ||
			(record.status == ModelCollisionRecordStatus::OtherSource && !record.source.isEmpty()))
			continue;
		QString status;
		if (record.status == ModelCollisionRecordStatus::Current)
			status = Text::tr("Current");
		else if (record.status == ModelCollisionRecordStatus::Stale)
			status = Text::tr("Stale");
		else
			status = Text::tr("No recorded source");
		if (record.status == ModelCollisionRecordStatus::Stale && !stale)
		{
			preferred = int(ids.size());
			stale = true;
		}
		items << Text::tr("Entity %1 · %2 · frame %3 · %n brush(es)", nullptr, record.brushCount).arg(record.entityId).arg(status).arg(record.frame);
		ids << record.entityId;
	}
	if (items.isEmpty())
	{
		m_status->setText(records.isEmpty() ? Text::tr("The open level has no collision placed by the modeller.")
											: Text::tr("No collision in the open level was placed from this model."),
						  LiveStatus::Problem);
		return;
	}
	bool accepted = false;
	const auto chosen = QInputDialog::getItem(this, Text::tr("Refresh Placed Collision"),
											  Text::tr("Collision placed from this model (stale sets need refreshing):"), items, preferred,
											  false, &accepted);
	if (!accepted)
		return;
	QString error;
	if (!refreshPlacedCollision(ids.value(int(items.indexOf(chosen))), &error))
		m_status->setText(error, LiveStatus::Problem);
}
void ModelEditorDialog::handoffCollision(bool place)
{
	if (m_working)
	{
		return;
	}
	m_preview->pause();
	QString error;
	if (place)
	{
		if (!placeCollision(collisionRequest(), &error))
		{
			m_status->setText(error, LiveStatus::Problem);
		}
	}
	else
	{
		const auto path = QFileDialog::getSaveFileName(this, Text::tr("Export Collision Map"), {}, Text::tr("Quake-family map (*.map)"));
		if (!path.isEmpty() && !exportCollision(path, collisionRequest(), true, &error))
		{
			m_status->setText(error, LiveStatus::Problem);
		}
	}
}
} // namespace vibestudio
