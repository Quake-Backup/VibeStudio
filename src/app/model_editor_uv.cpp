#include "app/model_editor_dialog.h"
#include "app/locale_number.h"
#include "core/model_mdl.h"
#include "core/model_targets.h"
#include "core/model_skin_handoff.h"
#include "core/model_uv_atlas.h"
#include "core/level_materials.h"

#include "app/model_uv_view.h"
#include "app/model_uv_density_dialog.h"
#include "app/model_editor_tools.h"
#include "app/model_viewport.h"
#include "app/wrapping_action_button.h"
#include "app/wrapping_check_box.h"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QToolBar>
#include <QVBoxLayout>
#include <algorithm>
#include <array>

namespace vibestudio
{
QString ModelEditorDialog::nativeUvStatus() const
{
	const auto &surfaces = m_document.mesh().surfaces;
	return std::any_of(surfaces.cbegin(), surfaces.cend(), [](const auto &surface) { return !surface.uvSeams.isEmpty(); })
			   ? QLatin1Char(' ') +
					 QCoreApplication::translate("VibeStudioModelEditor",
												 "Export uses resolved UVs and split indices; seam marks remain in the editable source.")
			   : QString();
}
QWidget *ModelEditorDialog::createUvView()
{
	auto *page = new QWidget;
	auto *layout = new QVBoxLayout(page);
	layout->setContentsMargins(0, 0, 0, 0);
	auto *tools = new QToolBar;
	tools->setAccessibleName(QCoreApplication::translate("VibeStudioModelEditor", "UV view controls"));
	m_uv = new ModelUvView;
	m_uv->setObjectName(QStringLiteral("meshUvPreview"));
	const auto action = [&](const QString &label, const QString &name)
	{
		auto *item = tools->addAction(label);
		item->setObjectName(name);
		return item;
	};
	auto *all = action(QCoreApplication::translate("VibeStudioModelEditor", "Frame All"), QStringLiteral("frameMeshUvAll"));
	auto *selection =
		action(QCoreApplication::translate("VibeStudioModelEditor", "Frame Selection"), QStringLiteral("frameMeshUvSelection"));
	auto *islands = action(QCoreApplication::translate("VibeStudioModelEditor", "Select Islands"), QStringLiteral("selectMeshUvIslands"));
	auto *density = action(QCoreApplication::translate("ModelUvDensityDialog", "Texel Density…"), QStringLiteral("inspectMeshUvDensity"));
	density->setToolTip(QCoreApplication::translate("ModelUvDensityDialog", "Measure and match UV island texture density using original image dimensions."));
	connect(density, &QAction::triggered, this, &ModelEditorDialog::editUvDensity);
	islands->setToolTip(QCoreApplication::translate(
		"VibeStudioModelEditor",
		"Expand selected faces, vertices, or edges to their UV islands. Marked seams and split edges separate islands."));
	m_uvPickIslands = action(QCoreApplication::translate("VibeStudioModelEditor", "Pick Islands"), QStringLiteral("pickMeshUvIslands"));
	m_uvPickIslands->setCheckable(true);
	m_uvPickIslands->setToolTip(QCoreApplication::translate(
		"VibeStudioModelEditor", "In face mode, clicking the UV view selects a whole island. Control-click toggles it."));
	m_uvMove = action(QCoreApplication::translate("VibeStudioModelEditor", "Move"), QStringLiteral("moveMeshUvs"));
	m_uvMove->setCheckable(true);
	m_uvMove->setChecked(true);
	m_uvMove->setToolTip(QCoreApplication::translate("VibeStudioModelEditor",
													 "Drag the centre square to move selected UVs. Release applies one undo step; "
													 "Escape cancels. Surface controls provide numeric movement and snapping."));
	// UV health overlay: shades one problem at a time on the view's worker.
	tools->addSeparator();
	m_uvHealthOverlay = new QComboBox;
	m_uvHealthOverlay->setObjectName(QStringLiteral("meshUvHealthOverlay"));
	m_uvHealthOverlay->addItems({QCoreApplication::translate("ModelUvHealth", "No UV problems shown"),
								 QCoreApplication::translate("ModelUvHealth", "Stretched faces"),
								 QCoreApplication::translate("ModelUvHealth", "Overlapping faces"),
								 QCoreApplication::translate("ModelUvHealth", "Flipped faces"),
								 QCoreApplication::translate("ModelUvHealth", "Faces outside the tile")});
	m_uvHealthOverlay->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	m_uvHealthOverlay->setMinimumContentsLength(12);
	m_uvHealthOverlay->setAccessibleName(QCoreApplication::translate("ModelUvHealth", "UV problem overlay"));
	m_uvHealthOverlay->setToolTip(QCoreApplication::translate(
		"ModelUvHealth", "Shade one kind of UV problem in the UV view. Each kind has its own hatching and the view's legend counts the "
						 "faces. Stretch is measured against the displayed pose and the skin's original size."));
	m_uvHealthOverlay->setAccessibleDescription(m_uvHealthOverlay->toolTip());
	tools->addWidget(m_uvHealthOverlay);
	m_uvHealthStretch = new QDoubleSpinBox;
	m_uvHealthStretch->setObjectName(QStringLiteral("meshUvHealthStretch"));
	m_uvHealthStretch->setRange(1.01, 100);
	m_uvHealthStretch->setDecimals(2);
	m_uvHealthStretch->setSingleStep(0.1);
	m_uvHealthStretch->setValue(1.5);
	m_uvHealthStretch->setLayoutDirection(Qt::LeftToRight);
	m_uvHealthStretch->setAccessibleName(QCoreApplication::translate("ModelUvHealth", "UV stretch limit"));
	m_uvHealthStretch->setToolTip(QCoreApplication::translate(
		"ModelUvHealth", "Faces whose texels are stretched or squashed by more than this factor, against their island's average "
						 "density, count as stretched. 1 is no stretch."));
	m_uvHealthStretch->setAccessibleDescription(m_uvHealthStretch->toolTip());
	tools->addWidget(m_uvHealthStretch);
	auto *problems = action(QCoreApplication::translate("ModelUvHealth", "Select Shown Problems"), QStringLiteral("selectMeshUvProblems"));
	problems->setToolTip(QCoreApplication::translate("ModelUvHealth", "Select the faces the UV problem overlay shades. The model is not changed."));
	auto *check = action(QCoreApplication::translate("ModelUvHealth", "Check UV Health"), QStringLiteral("checkMeshUvHealth"));
	check->setToolTip(QCoreApplication::translate(
		"ModelUvHealth", "Report stretched, overlapping, flipped, collapsed and out-of-tile faces, the largest texel-centre error, the "
						 "MDL, MD2 and MD3 vertices an export would store, and the GLQuake skin flood-fill hazard for an MDL."));
	connect(problems, &QAction::triggered, this,
			[this]
			{
				const auto mask = modelUvHealthOverlayMask(ModelUvHealthOverlay(m_uvHealthOverlay->currentIndex()));
				if (mask)
					selectUvProblems(mask);
			});
	connect(check, &QAction::triggered, this, [this] { checkUvHealth(); });
	auto *shared = action(QCoreApplication::translate("ModelUvOps", "Show Shared Surfaces"), QStringLiteral("showMeshUvShared"));
	shared->setCheckable(true);
	shared->setToolTip(QCoreApplication::translate("ModelUvOps", "Draw the UVs of other surfaces bound to the same material as dim dashed "
																 "wires, so islands that would overlap on the shared skin show."));
	connect(shared, &QAction::toggled, this, [this] { refreshUv(); });
	// Skin round trips: a template to paint over, a repaint after UV edits,
	// and the skin itself in the texture editor.
	tools->addSeparator();
	auto *layoutExport = action(QCoreApplication::translate("ModelUvLayoutDialog", "Export UV Layout…"), QStringLiteral("exportMeshUvLayout"));
	layoutExport->setToolTip(QCoreApplication::translate("ModelUvLayoutDialog", "Write the UV layout as a skin template image to paint over, or stage it in the package."));
	connect(layoutExport, &QAction::triggered, this, &ModelEditorDialog::exportUvLayout);
	auto *transfer = action(QCoreApplication::translate("ModelSkinHandoff", "Transfer Skin…"), QStringLiteral("transferMeshSkin"));
	transfer->setToolTip(QCoreApplication::translate("ModelSkinHandoff", "After a UV edit, repaint the surface's skin for the new UVs by sampling it through the old ones."));
	connect(transfer, &QAction::triggered, this, &ModelEditorDialog::transferSkin);
	auto *edit = action(QCoreApplication::translate("ModelSkinHandoff", "Edit Skin…"), QStringLiteral("editMeshSkin"));
	edit->setToolTip(QCoreApplication::translate("ModelSkinHandoff", "Open the surface's skin in the texture editor. Staging it there updates the model's preview."));
	connect(edit, &QAction::triggered, this, [this] { editSurfaceSkin(); });
	connect(m_uvHealthOverlay, &QComboBox::currentIndexChanged, this, [this] { refreshUv(); });
	connect(m_uvHealthStretch, &QDoubleSpinBox::valueChanged, this, [this] { refreshUv(); });
	layout->addWidget(tools);
	layout->addWidget(m_uv, 1);
	connect(all, &QAction::triggered, m_uv, &ModelUvView::frameAll);
	connect(selection, &QAction::triggered, m_uv, &ModelUvView::frameSelection);
	connect(islands, &QAction::triggered, this, &ModelEditorDialog::selectUvIslands);
	connect(m_uvPickIslands, &QAction::toggled, this, [this] { refreshUv(); });
	connect(m_uvMove, &QAction::toggled, this, [this] { refreshUv(); });
	connect(m_uv, &ModelUvView::componentPicked, this,
			[this](int kind, int a, int b, bool toggle)
			{
				if (m_working || m_uv->isRendering() || kind < 0 || kind > 3 || (kind == 3 ? 0 : kind) != m_selectionMode->currentIndex())
				{
					return;
				}
				auto selected = m_document.selection();
				if (!toggle)
				{
					selected.vertices.clear();
					selected.faces.clear();
					selected.edges.clear();
				}
				if (kind == 3)
				{
					const auto *topology = m_uv->topology();
					if (!topology || a < 0 || a >= topology->faceIsland.size())
					{
						return;
					}
					const auto &faces = topology->islands[topology->faceIsland[a]].faces;
					const bool remove =
						toggle && std::all_of(faces.cbegin(), faces.cend(), [&](int face) { return selected.faces.contains(face); });
					for (int face : faces)
					{
						if (remove)
						{
							selected.faces.remove(face);
						}
						else
						{
							selected.faces.insert(face);
						}
					}
				}
				else if (kind == 2)
				{
					const auto edge = modelEdge(a, b);
					if (toggle && selected.edges.contains(edge))
					{
						selected.edges.remove(edge);
					}
					else
					{
						selected.edges.insert(edge);
					}
				}
				else
				{
					auto &items = kind == 1 ? selected.vertices : selected.faces;
					if (toggle && items.contains(a))
					{
						items.remove(a);
					}
					else
					{
						items.insert(a);
					}
				}
				m_document.setSelection(selected);
				refresh();
			});
	connect(m_uv, &ModelUvView::movePreviewChanged, this,
			[this](double u, double v)
			{
				m_status->setText(
					QCoreApplication::translate("VibeStudioModelEditor", "UV move preview: U %1, V %2. Release to apply; Escape cancels.")
						.arg(localeNumber(u, 7))
						.arg(localeNumber(v, 7)));
			});
	connect(m_uv, &ModelUvView::moveActiveChanged, this,
			[this](bool active)
			{
				if (active)
				{
					m_preview->finishEditMove(false);
				}
				else if (!m_refreshing && !m_working)
				{
					refreshSelection();
				}
			});
	// Modal Scale and Rotate in the UV view: one TransformUv about the
	// selection's centre, as the numeric controls would apply it.
	connect(m_uv, &ModelUvView::modalPreviewChanged, this, [this](const QString &summary) { m_status->setText(summary); });
	connect(m_uv, &ModelUvView::transformRequested, this,
			[this](double scaleU, double scaleV, double rotation, double pivotU, double pivotV)
			{
				if (m_working)
					return;
				ModelEdit edit;
				edit.kind = ModelEditKind::TransformUv;
				edit.selection = m_document.selection();
				edit.uvScale = {float(scaleU), float(scaleV)};
				edit.uvRotation = rotation;
				edit.uvPivotMode = ModelUvPivot::Custom;
				edit.uvPivot = {float(pivotU), float(pivotV)};
				QString error;
				if (!applyEdit(edit, &error))
					m_status->setText(error, LiveStatus::Problem);
				else if (rotation != 0)
					m_status->setText(QCoreApplication::translate("VibeStudioModelUvView", "Rotated the selected UVs by %1 degrees.")
										  .arg(localeNumber(rotation, 6)),
									  LiveStatus::Result);
				else
					m_status->setText(QCoreApplication::translate("VibeStudioModelUvView", "Scaled the selected UVs by %1 × %2.")
										  .arg(localeNumber(scaleU, 6), localeNumber(scaleV, 6)),
									  LiveStatus::Result);
			});
	connect(m_uv, &ModelUvView::moveRequested, this,
			[this](double u, double v)
			{
				if (m_working)
				{
					return;
				}
				ModelEdit edit;
				edit.kind = ModelEditKind::TransformUv;
				edit.selection = m_document.selection();
				edit.uvOffset = {float(u), float(v)};
				edit.uvTranslationGrid = m_uvSnap->isChecked() ? m_uvGrid->value() : 0;
				QString error;
				if (!applyEdit(edit, &error))
				{
					m_status->setText(error);
				}
			});
	return page;
}

void ModelEditorDialog::addUvControls(QFormLayout *surface)
{
	m_uvAtlasResolution = new QSpinBox;
	m_uvAtlasResolution->setObjectName(QStringLiteral("meshUvAtlasResolution"));
	m_uvAtlasResolution->setRange(32, 4096);
	m_uvAtlasResolution->setValue(512);
	m_uvAtlasResolution->setLayoutDirection(Qt::LeftToRight);
	m_uvAtlasResolution->setAccessibleName(QCoreApplication::translate("VibeStudioModelEditor", "UV atlas width in pixels"));
	m_uvAtlasResolution->setToolTip(QCoreApplication::translate(
		"VibeStudioModelEditor",
		"Texture width used for chart packing and pixel padding. UVs use one texture tile; existing texture images are not repainted."));
	m_uvAtlasResolution->setAccessibleDescription(m_uvAtlasResolution->toolTip());
	surface->addRow(QCoreApplication::translate("VibeStudioModelEditor", "Atlas Width"), m_uvAtlasResolution);
	m_uvAtlasHeight = new QSpinBox;
	m_uvAtlasHeight->setObjectName(QStringLiteral("meshUvAtlasHeight"));
	m_uvAtlasHeight->setRange(32, 4096);
	m_uvAtlasHeight->setValue(512);
	m_uvAtlasHeight->setLayoutDirection(Qt::LeftToRight);
	m_uvAtlasHeight->setAccessibleName(QCoreApplication::translate("VibeStudioModelEditor", "UV atlas height in pixels"));
	m_uvAtlasHeight->setToolTip(QCoreApplication::translate("VibeStudioModelEditor",
		"Use Same as width for a square atlas, or 32–4096 pixels for a rectangular texture. Chart shape is preserved in texture pixels. "
		"Match your texture dimensions; this does not resize images or change export skin dimensions."));
	m_uvAtlasHeight->setAccessibleDescription(m_uvAtlasHeight->toolTip());
	surface->addRow(QCoreApplication::translate("VibeStudioModelEditor", "Atlas Height"), m_uvAtlasHeight);
	m_uvAtlasSquare = new WrappingCheckBox(QCoreApplication::translate("VibeStudioModelEditor", "Same as width"));
	m_uvAtlasSquare->setObjectName(QStringLiteral("meshUvAtlasSquare"));
	m_uvAtlasSquare->setChecked(true);
	m_uvAtlasSquare->setAccessibleName(QCoreApplication::translate("VibeStudioModelEditor", "Keep atlas height equal to width"));
	m_uvAtlasSquare->setToolTip(QCoreApplication::translate("VibeStudioModelEditor", "Turn off to choose an independent atlas height for a rectangular texture."));
	m_uvAtlasSquare->setAccessibleDescription(m_uvAtlasSquare->toolTip());
	surface->addRow(m_uvAtlasSquare);
	m_uvAtlasHeight->setEnabled(false);
	m_uvAtlasPadding = new QSpinBox;
	m_uvAtlasPadding->setObjectName(QStringLiteral("meshUvAtlasPadding"));
	m_uvAtlasPadding->setRange(0, 63);
	m_uvAtlasPadding->setValue(4);
	m_uvAtlasPadding->setLayoutDirection(Qt::LeftToRight);
	m_uvAtlasPadding->setAccessibleName(QCoreApplication::translate("VibeStudioModelEditor", "UV atlas padding in pixels"));
	m_uvAtlasPadding->setToolTip(QCoreApplication::translate(
		"VibeStudioModelEditor",
		"Padding surrounds charts and the atlas border. Bilinear filtering space is included. Use more padding for mipmapped textures."));
	m_uvAtlasPadding->setAccessibleDescription(m_uvAtlasPadding->toolTip());
	surface->addRow(QCoreApplication::translate("VibeStudioModelEditor", "Atlas Padding"), m_uvAtlasPadding);
	const auto updatePadding = [this]()
	{
		if (m_uvAtlasSquare->isChecked())
		{
			m_uvAtlasHeight->setValue(m_uvAtlasResolution->value());
		}
		m_uvAtlasHeight->setEnabled(!m_uvAtlasSquare->isChecked());
		m_uvAtlasPadding->setMaximum(std::min(64, (std::min(m_uvAtlasResolution->value(), m_uvAtlasHeight->value()) - 1) / 8));
	};
	connect(m_uvAtlasResolution, &QSpinBox::valueChanged, this, updatePadding);
	connect(m_uvAtlasHeight, &QSpinBox::valueChanged, this, updatePadding);
	connect(m_uvAtlasSquare, &QCheckBox::toggled, this, updatePadding);
	m_uvUnwrap = new WrappingActionButton(QCoreApplication::translate("VibeStudioModelEditor", "Unwrap and Pack Faces"), 2);
	m_uvUnwrap->setObjectName(QStringLiteral("unwrapMeshUv"));
	m_uvUnwrap->setToolTip(QCoreApplication::translate(
		"VibeStudioModelEditor", "Create UV charts from selected faces in the displayed pose, honour marked seams, and pack one atlas. All "
								 "animation poses retain their geometry. Undo restores the previous mapping."));
	m_uvPack = new WrappingActionButton(QCoreApplication::translate("VibeStudioModelEditor", "Pack Selected UVs"), 2);
	m_uvPack->setObjectName(QStringLiteral("packMeshUv"));
	m_uvPack->setToolTip(QCoreApplication::translate(
		"VibeStudioModelEditor",
		"Pack selected faces into one atlas while preserving island shapes, orientation, and relative UV scale. Use Select Islands first "
		"to include entire islands. Unselected UVs remain fixed and can overlap the new atlas."));
	for (auto *button : {m_uvUnwrap, m_uvPack})
	{
		button->setAccessibleName(button->text());
		button->setAccessibleDescription(button->toolTip());
		surface->addRow(button);
	}
	connect(m_uvUnwrap, &QPushButton::clicked, this, [this] { execute(ModelEditKind::UnwrapUv); });
	connect(m_uvPack, &QPushButton::clicked, this, [this] { execute(ModelEditKind::PackUv); });
	m_uvObstacleScale = new QComboBox;
	m_uvObstacleScale->setObjectName(QStringLiteral("meshUvObstacleScale"));
	m_uvObstacleScale->setMinimumContentsLength(10);
	m_uvObstacleScale->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	m_uvObstacleScale->addItems({QCoreApplication::translate("ModelUvObstacles", "Fit uniformly"),
		QCoreApplication::translate("ModelUvObstacles", "Keep current UV scale")});
	m_uvObstacleScale->setAccessibleName(QCoreApplication::translate("ModelUvObstacles", "Packing around fixed regions: scale"));
	m_uvObstacleScale->setToolTip(QCoreApplication::translate("ModelUvObstacles",
		"Pack Around Unselected can fit all moving islands with one shared scale, or keep their current texel scale and fail if they do not fit. Orientation and relative density are preserved."));
	m_uvObstacleScale->setAccessibleDescription(m_uvObstacleScale->toolTip());
	surface->addRow(QCoreApplication::translate("ModelUvObstacles", "Fixed-region packing"), m_uvObstacleScale);
	m_uvPackAround = new WrappingActionButton(QCoreApplication::translate("ModelUvObstacles", "Pack Around Unselected"), 2);
	m_uvPackAround->setObjectName(QStringLiteral("packMeshUvAround"));
	m_uvPackAround->setAccessibleName(m_uvPackAround->text());
	m_uvPackAround->setToolTip(QCoreApplication::translate("ModelUvObstacles",
		"Pack complete selected islands around fixed UV faces on this surface and other surfaces sharing a material slot. Fixed UVs must fit the 0–1 tile. Atlas dimensions and padding apply; images are not repainted. Use Select Islands first."));
	m_uvPackAround->setAccessibleDescription(m_uvPackAround->toolTip());
	surface->addRow(m_uvPackAround);
	connect(m_uvPackAround, &QPushButton::clicked, this, [this] { execute(ModelEditKind::PackUvAround); });
	m_uvPivotMode = new QComboBox;
	m_uvPivotMode->setObjectName(QStringLiteral("meshUvPivotMode"));
	m_uvPivotMode->setAccessibleName(QCoreApplication::translate("VibeStudioModelEditor", "UV transform pivot"));
	m_uvPivotMode->addItems({QCoreApplication::translate("VibeStudioModelEditor", "UV Origin"),
							 QCoreApplication::translate("VibeStudioModelEditor", "Selection Centre"),
							 QCoreApplication::translate("VibeStudioModelEditor", "Custom"),
							 QCoreApplication::translate("VibeStudioModelEditor", "Individual Islands")});
	m_uvPivotMode->setToolTip(QCoreApplication::translate(
		"VibeStudioModelEditor", "Scale and rotate around the origin, selection centre, custom coordinates or each complete island's centre. "
								 "For Individual Islands, use Select Islands first. Shared corners split across all poses. "
								 "Projection computes each pivot after projecting onto the chosen plane."));
	m_uvPivotMode->setAccessibleDescription(m_uvPivotMode->toolTip());
	surface->addRow(QCoreApplication::translate("VibeStudioModelEditor", "UV Pivot"), m_uvPivotMode);
	for (int axis = 0; axis < 2; ++axis)
	{
		auto *value = new QDoubleSpinBox;
		m_uvPivot[axis] = value;
		value->setObjectName(QStringLiteral("meshUvPivot%1").arg(axis));
		value->setDecimals(6);
		value->setRange(-1000000, 1000000);
		value->setEnabled(false);
		const auto label = axis == 0 ? QCoreApplication::translate("VibeStudioModelEditor", "Pivot U")
									 : QCoreApplication::translate("VibeStudioModelEditor", "Pivot V");
		value->setAccessibleName(label);
		surface->addRow(label, value);
	}
	connect(m_uvPivotMode, &QComboBox::currentIndexChanged, this,
			[this](int index)
			{
				for (auto *value : m_uvPivot)
				{
					value->setEnabled(index == int(ModelUvPivot::Custom));
				}
			});
	m_uvSnap = new WrappingCheckBox(QCoreApplication::translate("VibeStudioModelEditor", "Snap UV Offset"));
	m_uvSnap->setObjectName(QStringLiteral("meshUvSnap"));
	m_uvSnap->setAccessibleName(m_uvSnap->text());
	m_uvSnap->setToolTip(QCoreApplication::translate(
		"VibeStudioModelEditor", "Snap numeric and dragged UV offsets to multiples of the grid step. One UV unit is one texture tile."));
	surface->addRow(m_uvSnap);
	m_uvGrid = new QDoubleSpinBox;
	m_uvGrid->setObjectName(QStringLiteral("meshUvGrid"));
	m_uvGrid->setAccessibleName(QCoreApplication::translate("VibeStudioModelEditor", "UV offset grid step"));
	m_uvGrid->setDecimals(6);
	m_uvGrid->setRange(0.000001, 1000000);
	m_uvGrid->setValue(0.125);
	m_uvGrid->setSingleStep(0.03125);
	m_uvGrid->setEnabled(false);
	surface->addRow(QCoreApplication::translate("VibeStudioModelEditor", "UV Grid"), m_uvGrid);
	connect(m_uvSnap, &QCheckBox::toggled, this,
			[this](bool enabled)
			{
				m_uvGrid->setEnabled(enabled);
				refreshUv();
			});
	connect(m_uvGrid, &QDoubleSpinBox::valueChanged, this, [this] { refreshUv(); });
	const auto button = [&](const QString &label, const QString &name, ModelEditKind kind)
	{
		auto *item = new WrappingActionButton(label, 2);
		item->setObjectName(name);
		item->setProperty("requiresMeshComponents", true);
		surface->addRow(item);
		connect(item, &QPushButton::clicked, this, [this, kind] { execute(kind); });
		return item;
	};
	auto *mark = button(QCoreApplication::translate("VibeStudioModelEditor", "Mark UV Seams"), QStringLiteral("markMeshUvSeams"),
						ModelEditKind::MarkUvSeams);
	mark->setToolTip(QCoreApplication::translate("VibeStudioModelEditor",
												 "Mark selected edges as UV island boundaries. Marks are saved in editable sources. Detach "
												 "or move selected faces to split their shared UV corners for native export."));
	button(QCoreApplication::translate("VibeStudioModelEditor", "Clear UV Seams"), QStringLiteral("clearMeshUvSeams"),
		   ModelEditKind::ClearUvSeams);
	auto *detach = button(QCoreApplication::translate("VibeStudioModelEditor", "Detach UV Faces"), QStringLiteral("detachMeshUvs"),
						  ModelEditKind::DetachUv);
	detach->setToolTip(QCoreApplication::translate(
		"VibeStudioModelEditor", "Split selected faces from unselected neighbours without moving UVs or changing poses and normals. Select "
								 "one island to detach it; Weld UVs joins split corners again."));
	detach->setAccessibleDescription(detach->toolTip());
	// UV operators (core/model_uv_ops.h): each is one undo step.
	const auto toolButton = [&](const QString &label, const QString &name, ModelEditKind kind, const QString &tip)
	{
		auto *item = new WrappingActionButton(label, 2);
		item->setObjectName(name);
		item->setProperty("requiresMeshComponents", true);
		item->setToolTip(tip);
		item->setAccessibleName(label);
		item->setAccessibleDescription(tip);
		surface->addRow(item);
		connect(item, &QPushButton::clicked, this, [this, kind] { runUvTool(kind); });
		return item;
	};
	m_uvWeldTolerance = new QDoubleSpinBox;
	m_uvWeldTolerance->setObjectName(QStringLiteral("meshUvWeldTolerance"));
	m_uvWeldTolerance->setDecimals(6);
	m_uvWeldTolerance->setRange(0, 1);
	m_uvWeldTolerance->setSingleStep(0.001);
	m_uvWeldTolerance->setLayoutDirection(Qt::LeftToRight);
	m_uvWeldTolerance->setAccessibleName(QCoreApplication::translate("ModelUvOps", "UV weld distance"));
	m_uvWeldTolerance->setToolTip(QCoreApplication::translate(
		"ModelUvOps", "Split UV corners join when their UVs are at most this far apart (one unit is one texture tile). Zero joins only "
					  "corners at exactly the same UV, such as those Detach UV Faces split."));
	m_uvWeldTolerance->setAccessibleDescription(m_uvWeldTolerance->toolTip());
	surface->addRow(QCoreApplication::translate("ModelUvOps", "Weld Distance"), m_uvWeldTolerance);
	toolButton(QCoreApplication::translate("ModelUvOps", "Weld UVs"), QStringLiteral("weldMeshUvs"), ModelEditKind::WeldUv,
			   QCoreApplication::translate("ModelUvOps", "Join the selected corners with their split copies where position and normal match in "
														 "every pose, at their mean UV. Seam marks stay; clear them to merge the islands."));
	toolButton(QCoreApplication::translate("ModelUvOps", "Stitch UVs"), QStringLiteral("stitchMeshUvs"), ModelEditKind::StitchUv,
			   QCoreApplication::translate("ModelUvOps", "In edge selection, choose boundary edges of UV seams, between as many islands as you "
														 "like. In each group of joined islands the largest stays put; the others turn and move, "
														 "never mirrored or scaled, so the edges meet; the corners then weld."));
	m_uvAlignMode = new QComboBox;
	m_uvAlignMode->setObjectName(QStringLiteral("meshUvAlignMode"));
	m_uvAlignMode->addItems({QCoreApplication::translate("ModelUvOps", "U to minimum"), QCoreApplication::translate("ModelUvOps", "U to centre"),
							 QCoreApplication::translate("ModelUvOps", "U to maximum"), QCoreApplication::translate("ModelUvOps", "V to minimum"),
							 QCoreApplication::translate("ModelUvOps", "V to centre"), QCoreApplication::translate("ModelUvOps", "V to maximum"),
							 QCoreApplication::translate("ModelUvOps", "Straighten")});
	m_uvAlignMode->setCurrentIndex(1);
	m_uvAlignMode->setAccessibleName(QCoreApplication::translate("ModelUvOps", "UV alignment"));
	m_uvAlignMode->setToolTip(QCoreApplication::translate(
		"ModelUvOps", "Line the selected UV corners up on U or V at their lowest, middle or highest value, or straighten them onto the "
					  "line between the two corners farthest apart. Faces sharing those corners follow."));
	m_uvAlignMode->setAccessibleDescription(m_uvAlignMode->toolTip());
	surface->addRow(QCoreApplication::translate("ModelUvOps", "Align"), m_uvAlignMode);
	toolButton(QCoreApplication::translate("ModelUvOps", "Align UVs"), QStringLiteral("alignMeshUvs"), ModelEditKind::AlignUv,
			   m_uvAlignMode->toolTip());
	m_uvSnapMode = new QComboBox;
	m_uvSnapMode->setObjectName(QStringLiteral("meshUvSnapMode"));
	m_uvSnapMode->addItems(
		{QCoreApplication::translate("ModelUvOps", "Texel centres"), QCoreApplication::translate("ModelUvOps", "Texel corners")});
	m_uvSnapMode->setAccessibleName(QCoreApplication::translate("ModelUvOps", "Texel snapping"));
	m_uvSnapMode->setToolTip(QCoreApplication::translate(
		"ModelUvOps", "Texel centres are where MDL and MD2 store UVs, so an export after snapping loses nothing. The grid is the MDL skin "
					  "size for an MDL, else the MD2 skin size."));
	m_uvSnapMode->setAccessibleDescription(m_uvSnapMode->toolTip());
	surface->addRow(QCoreApplication::translate("ModelUvOps", "Snap To"), m_uvSnapMode);
	toolButton(QCoreApplication::translate("ModelUvOps", "Snap UVs to Texels"), QStringLiteral("snapMeshUvTexels"), ModelEditKind::SnapUvTexels,
			   m_uvSnapMode->toolTip());
	// Pins (core/model_uv_pins.h), saved with the model, hold UV corners in
	// place during a conformal unwrap.
	toolButton(QCoreApplication::translate("ModelUvOps", "Pin UVs"), QStringLiteral("pinMeshUvs"), ModelEditKind::PinUv,
			   QCoreApplication::translate("ModelUvOps", "Pin the selected UV corners: a conformal unwrap keeps them where they are and flattens "
														 "the rest of their island around them. Pins are saved with the model."));
	toolButton(QCoreApplication::translate("ModelUvOps", "Unpin UVs"), QStringLiteral("unpinMeshUvs"), ModelEditKind::UnpinUv,
			   QCoreApplication::translate("ModelUvOps", "Remove the pins from the selected UV corners."));
	// Unwrap method and packing choices, read by Unwrap and Pack.
	m_uvUnwrapMethod = new QComboBox;
	m_uvUnwrapMethod->setObjectName(QStringLiteral("meshUvUnwrapMethod"));
	m_uvUnwrapMethod->addItems({QCoreApplication::translate("ModelUvOps", "Automatic charts"),
								QCoreApplication::translate("ModelUvOps", "Conformal (seams only)")});
	m_uvUnwrapMethod->setAccessibleName(QCoreApplication::translate("ModelUvOps", "UV unwrap method"));
	m_uvUnwrapMethod->setToolTip(QCoreApplication::translate(
		"ModelUvOps", "Automatic charts may add cuts of their own. Conformal makes exactly one island per seam-bounded region with "
					  "least squares conformal maps; a closed region needs a seam first. Pinned UV corners keep their place; in "
					  "vertex selection, the selected vertices are pinned for that unwrap too and their islands unwrap."));
	m_uvUnwrapMethod->setAccessibleDescription(m_uvUnwrapMethod->toolTip());
	surface->addRow(QCoreApplication::translate("ModelUvOps", "Unwrap Method"), m_uvUnwrapMethod);
	m_uvUnwrapPack = new WrappingCheckBox(QCoreApplication::translate("ModelUvOps", "Pack after a conformal unwrap"));
	m_uvUnwrapPack->setObjectName(QStringLiteral("meshUvUnwrapPack"));
	m_uvUnwrapPack->setChecked(true);
	m_uvUnwrapPack->setAccessibleName(m_uvUnwrapPack->text());
	m_uvUnwrapPack->setToolTip(QCoreApplication::translate(
		"ModelUvOps", "Turn off to keep conformal islands in a row at one density, or where two or more pinned corners put them."));
	m_uvUnwrapPack->setAccessibleDescription(m_uvUnwrapPack->toolTip());
	surface->addRow(m_uvUnwrapPack);
	m_uvPackRotate = new WrappingCheckBox(QCoreApplication::translate("ModelUvOps", "Turn islands when packing"));
	m_uvPackRotate->setObjectName(QStringLiteral("meshUvPackRotate"));
	m_uvPackRotate->setAccessibleName(m_uvPackRotate->text());
	m_uvPackRotate->setToolTip(QCoreApplication::translate(
		"ModelUvOps", "Pack Selected UVs and a packed conformal unwrap also try every island a quarter turn round, keeping whichever fits "
					  "densest. Islands are never mirrored."));
	m_uvPackRotate->setAccessibleDescription(m_uvPackRotate->toolTip());
	surface->addRow(m_uvPackRotate);
	m_uvPackShared = new WrappingCheckBox(QCoreApplication::translate("ModelUvOps", "Pack with surfaces sharing this material"));
	m_uvPackShared->setObjectName(QStringLiteral("meshUvPackShared"));
	m_uvPackShared->setAccessibleName(m_uvPackShared->text());
	m_uvPackShared->setToolTip(QCoreApplication::translate(
		"ModelUvOps", "Pack Selected UVs also packs every face of the other surfaces bound to the same material slot into the same atlas, "
					  "so islands from different surfaces never overlap."));
	m_uvPackShared->setAccessibleDescription(m_uvPackShared->toolTip());
	surface->addRow(m_uvPackShared);
	connect(m_uvUnwrapMethod, &QComboBox::currentIndexChanged, this, [this] { refreshUv(); });
}

void ModelEditorDialog::configureUvAtlasTools(ModelEdit *edit) const
{
	if (!m_uvUnwrapMethod)
		return;
	auto &tools = edit->uvTools;
	const bool conformal = edit->kind == ModelEditKind::UnwrapUv && m_uvUnwrapMethod->currentIndex() == 1;
	tools.unwrapMethod = conformal ? ModelUvUnwrapMethod::Conformal : ModelUvUnwrapMethod::Automatic;
	tools.unwrapPack = !conformal || m_uvUnwrapPack->isChecked();
	tools.packRotate = m_uvPackRotate->isChecked() && (edit->kind == ModelEditKind::PackUv || (conformal && tools.unwrapPack));
	const auto &mesh = m_document.mesh();
	const int active = edit->selection.surface;
	if (edit->kind == ModelEditKind::PackUv && m_uvPackShared->isChecked() && active >= 0 && active < mesh.surfaces.size())
		for (int s = 0; s < mesh.surfaces.size(); ++s)
			if (s != active && modelUvSurfacesShareMaterial(mesh.surfaces[active], mesh.surfaces[s]))
				tools.packSurfaces.insert(s);
	// In vertex selection, a conformal unwrap pins the selected vertices and
	// unwraps their islands.
	if (conformal && edit->selection.faces.isEmpty() && !edit->selection.vertices.isEmpty() && active >= 0 && active < mesh.surfaces.size())
	{
		ModelUvTopology topology;
		QSet<int> faces;
		if (buildModelUvTopology(mesh.surfaces[active], &topology) &&
			expandModelUvIslands(mesh.surfaces[active], topology, {}, edit->selection.vertices, {}, &faces))
			edit->selection.faces = faces;
	}
}

void ModelEditorDialog::rememberUvTransferSource(const ModelMesh &before, const ModelEdit &edit)
{
	static constexpr std::array uvKinds{ModelEditKind::TransformUv, ModelEditKind::ProjectUv,	   ModelEditKind::DetachUv,
										ModelEditKind::UnwrapUv,	ModelEditKind::PackUv,		   ModelEditKind::PackUvAround,
										ModelEditKind::SetUvDensity, ModelEditKind::ProjectUvMapping, ModelEditKind::WeldUv,
										ModelEditKind::StitchUv,	ModelEditKind::AlignUv,		   ModelEditKind::SnapUvTexels};
	const int surface = edit.selection.surface;
	if (std::find(uvKinds.cbegin(), uvKinds.cend(), edit.kind) == uvKinds.cend() || surface < 0 || surface >= before.surfaces.size())
	{
		m_uvTransferSurface = -1;
		m_uvTransferBefore = {};
		return;
	}
	// Consecutive UV edits on one surface keep the layout from before the
	// first, which is what the painted skin matches.
	if (m_uvTransferSurface != surface)
	{
		m_uvTransferBefore = before.surfaces[surface];
		m_uvTransferSurface = surface;
	}
	m_uvTransferRevision = m_document.revisionFingerprint();
}

bool ModelEditorDialog::canTransferSkin() const
{
	const auto &mesh = m_document.mesh();
	return m_uvTransferSurface >= 0 && m_uvTransferSurface < mesh.surfaces.size() && m_uvTransferRevision == m_document.revisionFingerprint() &&
		   m_uvTransferBefore.triangles.size() == mesh.surfaces[m_uvTransferSurface].triangles.size();
}

bool ModelEditorDialog::runUvTool(ModelEditKind kind, QString *error)
{
	if (m_working || m_document.mesh().surfaces.isEmpty() || !isModelUvToolEdit(kind))
		return false;
	ModelEdit edit;
	edit.kind = kind;
	edit.selection = m_document.selection();
	auto &tools = edit.uvTools;
	if (kind == ModelEditKind::WeldUv)
		tools.weldTolerance = m_uvWeldTolerance->value();
	if (kind == ModelEditKind::AlignUv)
	{
		const int align = m_uvAlignMode->currentIndex();
		tools.alignAxis = align >= 3 && align < 6 ? 1 : 0;
		tools.alignMode = align == 6 ? ModelUvAlignMode::Straighten
						  : align % 3 == 0 ? ModelUvAlignMode::Minimum
						  : align % 3 == 1 ? ModelUvAlignMode::Centre
										   : ModelUvAlignMode::Maximum;
	}
	if (kind == ModelEditKind::SnapUvTexels)
		tools.snapMode = m_uvSnapMode->currentIndex() == 1 ? ModelUvSnapMode::Corners : ModelUvSnapMode::Centres;
	QString failure;
	// Islands before a stitch, to say how many joined.
	ModelUvTopology islandsBefore;
	if (kind == ModelEditKind::StitchUv)
		buildModelUvTopology(m_document.mesh().surfaces[edit.selection.surface], &islandsBefore);
	// Through the tools, so Adjust Last Operation can change the settings.
	if (!(m_tools ? m_tools->runTool(edit, &failure) : applyEdit(edit, &failure)))
	{
		if (error)
			*error = failure;
		m_status->setText(failure, LiveStatus::Problem);
		return false;
	}
	const auto size = modelUvSnapSize(m_document.mesh());
	const int vertices = m_document.mesh().surfaces[m_document.selection().surface].vertexCount;
	switch (kind)
	{
	case ModelEditKind::WeldUv:
		m_status->setText(QCoreApplication::translate("ModelUvOps", "Welded UV corners; the surface now has %Ln vertices.", nullptr, vertices),
						  LiveStatus::Result);
		break;
	case ModelEditKind::StitchUv:
	{
		ModelUvTopology islandsAfter;
		buildModelUvTopology(m_document.mesh().surfaces[m_document.selection().surface], &islandsAfter);
		const int joined = std::max(0, int(islandsBefore.islands.size() - islandsAfter.islands.size()));
		m_status->setText(QCoreApplication::translate("ModelUvOps", "Stitched %Ln islands onto their neighbours.", nullptr, joined) + QLatin1Char(' ') +
							  QCoreApplication::translate("ModelUvOps", "The surface now has %Ln vertices.", nullptr, vertices),
						  LiveStatus::Result);
		break;
	}
	case ModelEditKind::PinUv:
	case ModelEditKind::UnpinUv:
	{
		const int pinned = int(m_document.mesh().surfaces[m_document.selection().surface].uvPins.size());
		m_status->setText(QCoreApplication::translate("ModelUvOps", "%Ln UV corners on this surface are pinned for a conformal unwrap.", nullptr, pinned),
						  LiveStatus::Result);
		break;
	}
	case ModelEditKind::AlignUv:
		m_status->setText(QCoreApplication::translate("ModelUvOps", "Aligned the selected UVs."), LiveStatus::Result);
		break;
	default:
		m_status->setText(QCoreApplication::translate("ModelUvOps", "Snapped the selected UVs to the %L1 × %L2 texel grid.")
							  .arg(size.width())
							  .arg(size.height()),
						  LiveStatus::Result);
		break;
	}
	return true;
}

bool ModelEditorDialog::layoutQuakeFrontBack(int axis, QString *error)
{
	if (m_working || m_document.mesh().surfaces.isEmpty())
		return false;
	ModelEdit edit;
	edit.kind = ModelEditKind::ProjectUvMapping;
	edit.selection = m_document.selection();
	edit.selection.vertices.clear();
	edit.selection.edges.clear();
	const auto &surface = m_document.mesh().surfaces[edit.selection.surface];
	if (edit.selection.faces.isEmpty())
		for (int face = 0; face < surface.triangles.size(); ++face)
			edit.selection.faces.insert(face);
	edit.tool.uvProjection = ModelUvProjection::QuakeFrontBack;
	edit.tool.axis = std::clamp(axis, 0, 2);
	edit.tool.referenceFrame = std::clamp(m_preview->frame(), 0, std::max(0, int(m_document.mesh().frames.size()) - 1));
	QString failure;
	if (!(m_tools ? m_tools->runTool(edit, &failure) : applyEdit(edit, &failure)))
	{
		if (error)
			*error = failure;
		m_status->setText(failure, LiveStatus::Problem);
		return false;
	}
	const auto &mesh = m_document.mesh();
	const auto size = modelUvSnapSize(mesh);
	int stored = 0, render = 0;
	ModelTargetProfile target;
	findModelTargetProfile(QStringLiteral("mdl"), deliveryTarget(QStringLiteral("mdl")), &target);
	if (mesh.mdl.enabled && mesh.surfaces.size() == 1 && modelMdlVertexCounts(mesh, &stored, &render))
		m_status->setText(QCoreApplication::translate("ModelUvOps", "Laid out front and back halves on %L1 × %L2 texels. The MDL stores %L3 "
																	"vertices (%4 allows %L5), %L6 of them on the seam.")
							  .arg(size.width())
							  .arg(size.height())
							  .arg(stored)
							  .arg(target.label)
							  .arg(target.limits.vertices)
							  .arg(render - stored),
						  LiveStatus::Result);
	else
		m_status->setText(QCoreApplication::translate("ModelUvOps", "Laid out front and back halves on %L1 × %L2 texels.")
							  .arg(size.width())
							  .arg(size.height()),
						  LiveStatus::Result);
	return true;
}

void ModelEditorDialog::refreshUv()
{
	if (!m_uv || m_document.mesh().surfaces.isEmpty())
	{
		return;
	}
	const auto selected = m_document.selection();
	const auto &surface = m_document.mesh().surfaces[selected.surface];
	const auto material = m_preview->mdlPlaybackActive() ? m_preview->mdlPlaybackSkin()
						  : m_mdlPreview.isNull()		 ? m_surfaceImages.value(selected.surface)
														 : m_mdlPreview;
	const auto image = m_renderMode->currentIndex() == 3 && !material.isNull() ? material : m_checker;
	const bool meshMode = m_selectionMode->currentIndex() < 3 && selected.tag.isEmpty() && selected.collision.isEmpty() && selected.surfaces.isEmpty();
	findChild<QAction *>(QStringLiteral("selectMeshUvIslands"))->setEnabled(meshMode);
	const bool atlasSelection = meshMode && !selected.faces.isEmpty() && selected.vertices.isEmpty() && selected.edges.isEmpty();
	const bool conformal = m_uvUnwrapMethod && m_uvUnwrapMethod->currentIndex() == 1;
	m_uvUnwrap->setEnabled(atlasSelection || (conformal && meshMode && !selected.vertices.isEmpty() && selected.edges.isEmpty()));
	m_uvPack->setEnabled(atlasSelection);
	m_uvPackAround->setEnabled(atlasSelection);
	m_uvObstacleScale->setEnabled(atlasSelection);
	m_uvMove->setEnabled(meshMode);
	m_uvPickIslands->setEnabled(m_selectionMode->currentIndex() == 0);
	m_uv->setPickMode(m_selectionMode->currentIndex() == 0 && m_uvPickIslands->isChecked() ? 3 : m_selectionMode->currentIndex());
	m_uv->setMoveEnabled(meshMode && m_uvMove->isChecked(), m_uvSnap->isChecked() ? m_uvGrid->value() : 0);
	// The profile's Scale and Rotate keys start the UV view's modal transforms.
	const auto keys = [this](const QString &command)
	{
		QList<QKeySequence> result;
		for (const auto &key : modelEditorCommandKeys(m_controls, command))
			result.append(QKeySequence::fromString(key, QKeySequence::PortableText));
		return result;
	};
	m_uv->setModalKeys(keys(QStringLiteral("meshScale")), keys(QStringLiteral("meshRotate")));
	const auto overlay = ModelUvHealthOverlay(m_uvHealthOverlay->currentIndex());
	const int pose = std::clamp(m_preview->frame(), 0, std::max(0, int(surface.frames.size()) - 1));
	m_uv->setHealthOverlay(overlay, overlay == ModelUvHealthOverlay::None || surface.frames.isEmpty() ? QVector<ModelVec3>{} : surface.frames[pose].positions,
						   uvSkinSize(selected.surface), m_uvHealthStretch->value());
	m_uvHealthStretch->setEnabled(overlay == ModelUvHealthOverlay::Stretch);
	findChild<QAction *>(QStringLiteral("selectMeshUvProblems"))->setEnabled(meshMode && overlay != ModelUvHealthOverlay::None);
	findChild<QAction *>(QStringLiteral("transferMeshSkin"))->setEnabled(canTransferSkin());
	// MD2: the resolved skin image against the MD2 skin size.
	if (m_md2SkinSizeWarning)
	{
		QString mismatch;
		const auto &mesh = m_document.mesh();
		if (mesh.surfaces.size() == 1 && !mesh.mdl.enabled)
			for (const auto &material : m_materialAssets.materials)
				if (material.key == modelPreviewSurfaceMaterialKey(0) && material.imagePath.endsWith(QStringLiteral(".pcx"), Qt::CaseInsensitive))
					mismatch = modelMd2SkinSizeMismatch(mesh, material.sourceSize, QLocale());
		m_md2SkinSizeWarning->setText(mismatch);
		m_md2SkinSizeWarning->setAccessibleDescription(mismatch);
		m_md2SkinSizeWarning->setVisible(!mismatch.isEmpty());
	}
	QVector<ModelSurface> ghosts;
	if (const auto *shared = findChild<QAction *>(QStringLiteral("showMeshUvShared")); shared && shared->isChecked())
		for (int s = 0; s < m_document.mesh().surfaces.size(); ++s)
			if (s != selected.surface && modelUvSurfacesShareMaterial(surface, m_document.mesh().surfaces[s]))
				ghosts.append(m_document.mesh().surfaces[s]);
	m_uv->setGhostSurfaces(ghosts);
	m_uv->setSource(surface, selected, image);
}

QSize ModelEditorDialog::uvSkinSize(int surface) const
{
	if (m_preview->mdlPlaybackActive() && !m_preview->mdlPlaybackSkin().isNull())
		return m_preview->mdlPlaybackSkin().size();
	if (!m_mdlPreview.isNull())
		return m_mdlPreview.size();
	for (const auto &material : m_materialAssets.materials)
		if (material.key == modelPreviewSurfaceMaterialKey(surface) && material.sourceSize.isValid() && !material.sourceSize.isEmpty())
			return material.sourceSize;
	return modelUvHealthDefaultTextureSize(m_document.mesh());
}

bool ModelEditorDialog::selectUvProblems(quint8 flags, QString *error)
{
	if (m_working || m_document.mesh().surfaces.isEmpty() || m_selectionMode->currentIndex() >= 3)
	{
		const auto message = QCoreApplication::translate("ModelUvHealth", "Switch to face, vertex or edge selection to select UV problems.");
		if (error)
			*error = message;
		m_status->setText(message, LiveStatus::Problem);
		return false;
	}
	ModelUvHealthOptions options;
	options.textureSize = uvSkinSize(m_document.selection().surface);
	options.referenceFrame = std::clamp(m_preview->frame(), 0, std::max(0, int(m_document.mesh().frames.size()) - 1));
	options.stretchLimit = m_uvHealthStretch->value();
	int count = 0;
	QString failure;
	if (!performWork(
			QCoreApplication::translate("ModelUvHealth", "Select UV Problems"),
			[options, flags, &count](ModelDocument &candidate, QString *problem, const ModelWorkControl &control)
			{
				auto selected = candidate.selection();
				ModelUvHealthReport report;
				if (!inspectModelUvSurfaceHealth(candidate.mesh().surfaces[selected.surface], options, &report, problem, control))
					return false;
				const auto faces = report.faces(flags);
				count = int(faces.size());
				selected.faces = QSet<int>(faces.cbegin(), faces.cend());
				selected.vertices.clear();
				selected.edges.clear();
				candidate.setSelection(selected);
				return true;
			},
			&failure))
	{
		if (error)
			*error = failure;
		m_status->setText(failure);
		return false;
	}
	{
		QSignalBlocker blocker(m_selectionMode);
		m_selectionMode->setCurrentIndex(0);
	}
	refresh();
	if (count)
		m_status->setText(QCoreApplication::translate("ModelUvHealth", "Selected %Ln faces with this UV problem.", nullptr, count),
						  LiveStatus::Result);
	else
		m_status->setText(QCoreApplication::translate("ModelUvHealth", "No face has this UV problem. The selection is now empty."),
						  LiveStatus::Result);
	return true;
}

bool ModelEditorDialog::checkUvHealth(ModelUvHealthReport *result, QString *error)
{
	if (m_document.mesh().surfaces.isEmpty())
		return false;
	ModelUvHealthOptions options;
	const int surface = m_document.selection().surface;
	options.textureSize = uvSkinSize(surface);
	options.referenceFrame = std::clamp(m_preview->frame(), 0, std::max(0, int(m_document.mesh().frames.size()) - 1));
	options.stretchLimit = m_uvHealthStretch->value();
	ModelUvHealthReport report;
	QString failure;
	if (!performWork(
			QCoreApplication::translate("ModelUvHealth", "Check UV Health"),
			[options, surface, &report](ModelDocument &candidate, QString *problem, const ModelWorkControl &control)
			{ return inspectModelUvHealth(candidate.mesh(), surface, options, &report, problem, control); },
			&failure))
	{
		if (error)
			*error = failure;
		m_status->setText(failure);
		return false;
	}
	const int overlapping = int(report.overlapping.size()), flipped = int(report.flipped.size()), outside = int(report.outside.size());
	const int stretched = int(report.stretched.size()), collapsed = int(report.collapsedUv.size() + report.collapsedGeometry.size());
	QStringList parts;
	parts << QCoreApplication::translate("ModelUvHealth", "UV health for %1 against %L2 × %L3 texels. Faces overlapping: %L4; flipped: %L5; "
										 "stretched: %L6; collapsed: %L7; outside the tile: %L8.")
				 .arg(m_document.mesh().surfaces[surface].name)
				 .arg(report.textureSize.width())
				 .arg(report.textureSize.height())
				 .arg(overlapping)
				 .arg(flipped)
				 .arg(stretched)
				 .arg(collapsed)
				 .arg(outside);
	if (report.overlapLimitReached)
		parts << QCoreApplication::translate("ModelUvHealth", "The overlap check reached its work limit; select fewer faces or split the surface.");
	if (report.mirroredIslands)
	{
		const int mirrored = report.mirroredIslands;
		parts << QCoreApplication::translate("ModelUvHealth", "Mirrored islands: %L1.").arg(mirrored);
	}
	parts << QCoreApplication::translate("ModelUvHealth", "Largest texel-centre error %1 texels.").arg(localeNumber(report.maxTexelError, 3));
	for (const auto &native : report.native)
	{
		if (!native.available)
			continue;
		const auto format = native.format.toUpper();
		if (native.limit > 0)
			parts << QCoreApplication::translate("ModelUvHealth", "%1 stores %L2 of %L3 vertices.").arg(format).arg(native.stored).arg(native.limit);
		else
			parts << QCoreApplication::translate("ModelUvHealth", "%1 stores %L2 vertices.").arg(format).arg(native.stored);
	}
	if (report.floodFillHazard())
		parts << QCoreApplication::translate("ModelUvHealth", "GLQuake's skin flood fill reaches %Ln used texels; keep the first texel's colour out "
											 "of painted areas.", nullptr, report.floodFillUsedTexels);
	const bool problems = overlapping || flipped || stretched || collapsed || outside || report.floodFillHazard() || report.overlapLimitReached;
	m_status->setText(parts.join(QLatin1Char(' ')), problems ? LiveStatus::Problem : LiveStatus::Result);
	if (result)
		*result = std::move(report);
	return true;
}

void ModelEditorDialog::editUvDensity()
{
	if (m_working || m_document.mesh().surfaces.isEmpty()) return;
	m_preview->finishEditTransform(false); m_preview->pause(); m_uv->finishMove(false);
	if (m_tools) m_tools->finishModal(false);
	const auto revision = m_document.revisionFingerprint();
	const auto selected = m_document.selection();
	QSize size;
	QString name;
	QImage texture;
	if (m_preview->mdlPlaybackActive()) texture = m_preview->mdlPlaybackSkin();
	else if (!m_mdlPreview.isNull()) texture = m_mdlPreview;
	if (!texture.isNull()) { size = texture.size(); name = modelPreviewSurfaceMaterialKey(selected.surface); }
	else
	{
		texture = m_surfaceImages.value(selected.surface);
		for (const auto &material : m_materialAssets.materials)
			if (material.key == modelPreviewSurfaceMaterialKey(selected.surface)) { size = material.sourceSize; name = material.imagePath; break; }
	}
	if (texture.isNull()) texture = m_checker;
	ModelUvDensityDialog dialog(m_document.mesh(), selected, m_preview->frame(), texture, size, name, this);
	if (dialog.exec() != QDialog::Accepted) return;
	if (revision != m_document.revisionFingerprint())
	{
		m_status->setText(QCoreApplication::translate("ModelUvDensityDialog", "The model changed while reviewing density. Reopen Texel Density."));
		return;
	}
	QString error;
	if (!applyEdit(dialog.edit(), &error)) m_status->setText(error);
}

void ModelEditorDialog::selectUvIslands()
{
	if (m_working || m_selectionMode->currentIndex() >= 3)
	{
		return;
	}
	QString error;
	if (!performWork(
			QCoreApplication::translate("VibeStudioModelEditor", "Select UV Islands"),
			[](ModelDocument &candidate, QString *failure, const ModelWorkControl &control)
			{
				auto selected = candidate.selection();
				const auto &surface = candidate.mesh().surfaces[selected.surface];
				ModelUvTopology topology;
				QSet<int> faces;
				if (!buildModelUvTopology(surface, &topology, failure, control) ||
					!expandModelUvIslands(surface, topology, selected.faces, selected.vertices, selected.edges, &faces, failure, control))
				{
					return false;
				}
				selected.faces = std::move(faces);
				selected.vertices.clear();
				selected.edges.clear();
				candidate.setSelection(selected);
				return true;
			},
			&error))
	{
		m_status->setText(error);
		return;
	}
	QSignalBlocker blocker(m_selectionMode);
	m_selectionMode->setCurrentIndex(0);
	refresh();
}
} // namespace vibestudio
