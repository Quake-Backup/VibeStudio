#include "app/model_editor_dialog.h"
#include "app/model_editor_tools.h"
#include "app/model_uv_density_dialog.h"
#include "app/model_uv_layout_dialog.h"
#include "app/model_uv_view.h"
#include "app/studio_theme.h"
#include "core/idtech_image.h"
#include "core/model_mdl.h"
#include "core/model_skin_handoff.h"
#include "core/studio_settings.h"
#include "core/texture_project.h"
#include "tests/model_mdl_test_helpers.h"
#include "tests/model_uv_health_test_helpers.h"
#include "tests/model_uv_islands_test_helpers.h"
#include "tests/model_uv_view_test_helpers.h"
#include "tests/render_test_support.h"

#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QImageWriter>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <cmath>
#include <cstring>
#include <iostream>

using namespace vibestudio;
using namespace vibestudio::tests;
namespace
{
int checks = 0, failures = 0;
bool expect(bool value, const char *message, const QString &detail = {})
{
	++checks;
	if (!value)
	{
		++failures;
		std::cerr << "FAIL: " << message << ": " << qPrintable(detail) << '\n';
	}
	return value;
}
QByteArray source(const ModelMesh &mesh) { return QJsonDocument(editableModelJson(mesh)).toJson(QJsonDocument::Compact); }
void showUvTab(ModelEditorDialog &editor, ModelUvView *view)
{
	for (auto *tabs : editor.findChildren<QTabWidget *>())
		for (int index = 0; index < tabs->count(); ++index)
			if (tabs->widget(index)->isAncestorOf(view))
				tabs->setCurrentIndex(index);
	editor.showSidebarPage(QStringLiteral("surface"));
}
void selectFaces(ModelEditorDialog &editor, ModelUvView *view, std::initializer_list<int> faces)
{
	editor.findChild<QComboBox *>(QStringLiteral("meshSelectionMode"))->setCurrentIndex(0);
	settleModelUv(*view);
	bool first = true;
	for (int face : faces)
	{
		// Picks wait for the view to settle after each selection change.
		settleModelUv(*view);
		view->componentPicked(0, face, -1, !first);
		first = false;
	}
	settleModelUv(*view);
}
QWidget *dialogNamed(const QString &name)
{
	for (auto *widget : QApplication::topLevelWidgets())
		if (widget->objectName() == name && widget->isVisible())
			return widget;
	return nullptr;
}
// The Adjust Last Operation fields, rebuilt after each change.
template <typename T> QList<T *> lastFields(ModelEditorDialog &editor)
{
	QApplication::processEvents();
	auto *panel = editor.tools()->lastOperationPanel();
	QList<T *> fields;
	if (!panel)
		return fields;
	for (auto *field : panel->findChildren<T *>())
		if (field->property("lastOperationField").isValid())
			fields.append(field);
	return fields;
}
// Saves the round-trip project as Textures would after painting one texel
// on a new layer.
bool paintRoundTrip(const QString &path, QPoint texel, QRgb colour, QString *error)
{
	TextureDocument project;
	QJsonObject metadata;
	if (!readTextureProject(path, &project, nullptr, &metadata, error))
		return false;
	auto layers = project.layers();
	QImage paint(project.size(), QImage::Format_ARGB32);
	paint.fill(Qt::transparent);
	paint.setPixel(texel, colour);
	int id = 0;
	for (const auto &layer : layers)
		id = std::max(id, layer.id);
	layers.insert(1, TextureLayer{id + 1, QStringLiteral("Paint"), paint, true, false, 100, TextureBlendMode::Normal});
	TextureProjectSaveRequest save;
	save.path = path;
	save.overwrite = true;
	const auto saved = project.restoreLayers(project.size(), layers, 1, error) ? writeTextureProject(project, save, metadata) : TextureProjectSaveReport{};
	if (!saved.succeeded && error && error->isEmpty())
		*error = saved.error;
	return saved.succeeded;
}
} // namespace

int main(int argc, char **argv)
{
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc, argv);
#ifdef Q_OS_WIN
	app.setFont(QFont(QStringLiteral("Segoe UI"), 10));
#endif
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
		return 1;
	QTemporaryDir temp(QDir(root).filePath(QStringLiteral("uv-tools-ui-XXXXXX")));
	if (!temp.isValid())
		return 1;
	StudioSettings::setOverrideFilePath(temp.filePath(QStringLiteral("settings.ini")));
	if (const int unavailable = test_support::exitCodeWithoutRenderer("UV tools UI"); unavailable >= 0)
		return unavailable;
	QString error;
	// Surfaces sharing the material draw as dim wires under the active one.
	{
		const auto shared = uvIslandsFixture();
		ModelUvRenderRequest request;
		request.surface = shared.surfaces[0];
		request.surface.frames.clear();
		request.logicalSize = {200, 200};
		request.background = Qt::black;
		request.foreground = Qt::white;
		request.accent = Qt::yellow;
		ModelUvRenderResult plain, ghosted;
		auto moved = shared.surfaces[1];
		for (auto &uv : moved.texCoords)
			uv = {uv.u * .5f + .1f, uv.v * .5f + .3f};
		expect(renderModelUv(request, &plain, &error), "render without shared surfaces");
		request.ghosts = {moved};
		expect(renderModelUv(request, &ghosted, &error) && ghosted.image != plain.image, "shared surfaces add their wires");
	}
	for (int scenario = 0; scenario < 2; ++scenario)
	{
		app.setLayoutDirection(scenario ? Qt::RightToLeft : Qt::LeftToRight);
		applyStudioTheme(app, studioThemeTokens(scenario ? StudioTheme::HighContrastDark : StudioTheme::Dark, UiDensity::Standard,
												scenario ? 200 : 100));
		auto mesh = strip(2);
		mesh.md2SkinSize = {64, 32};
		ModelEditorDialog editor;
		editor.setAttribute(Qt::WA_DeleteOnClose, false);
		editor.findChild<QAction *>(QStringLiteral("meshRecoveryEnabled"))->setChecked(false);
		expect(editor.setMesh(mesh, &error), "open the strip", error);
		editor.resize(scenario ? 1700 : 1440, scenario ? 1100 : 960);
		editor.show();
		app.processEvents();
		auto *view = editor.findChild<ModelUvView *>(QStringLiteral("meshUvPreview"));
		if (!view)
			return 1;
		showUvTab(editor, view);
		app.processEvents();
		settleModelUv(*view);
		for (const auto *name : {"meshUvWeldTolerance", "meshUvAlignMode", "meshUvSnapMode", "meshUvUnwrapMethod", "meshUvUnwrapPack",
								 "meshUvPackRotate", "meshUvPackShared", "weldMeshUvs", "stitchMeshUvs", "alignMeshUvs", "snapMeshUvTexels",
								 "pinMeshUvs", "unpinMeshUvs"})
		{
			auto *control = editor.findChild<QWidget *>(QString::fromLatin1(name));
			expect(control && !control->accessibleName().isEmpty() && !control->accessibleDescription().isEmpty() &&
					   control->focusPolicy() != Qt::NoFocus,
				   "UV operator controls are named, described and focusable", QString::fromLatin1(name));
		}
		for (const auto *name : {"meshUvWeld", "meshUvStitch", "meshUvAlign", "meshUvSnapTexels", "meshUnwrapConformal", "meshUvQuakeFrontBack",
								 "meshUvExportLayout", "meshUvTransferSkin", "meshUvEditSkin", "exportMeshUvLayout", "transferMeshSkin", "editMeshSkin",
								 "meshUvPin", "meshUvUnpin", "meshUvApplySkin", "meshSkinAutoApply"})
			expect(editor.findChild<QAction *>(QString::fromLatin1(name)) != nullptr, "UV commands are reachable", QString::fromLatin1(name));
		// Detach, then Weld: one undo step each, and Weld restores the source.
		const auto original = source(editor.document().mesh());
		selectFaces(editor, view, {2, 3});
		editor.findChild<QPushButton *>(QStringLiteral("detachMeshUvs"))->click();
		expect(editor.document().mesh().surfaces[0].vertexCount == 8, "detach splits the column");
		editor.findChild<QPushButton *>(QStringLiteral("weldMeshUvs"))->click();
		expect(source(editor.document().mesh()) == original, "Weld UVs restores the source", editor.findChild<QLabel *>("meshStatus")->text());
		editor.findChild<QAction *>(QStringLiteral("undoMesh"))->trigger();
		expect(editor.document().mesh().surfaces[0].vertexCount == 8, "one undo step for the weld");
		editor.findChild<QAction *>(QStringLiteral("undoMesh"))->trigger();
		expect(source(editor.document().mesh()) == original, "and one for the detach");
		// Snap to the MD2 skin's texel centres.
		selectFaces(editor, view, {0, 1, 2, 3});
		editor.findChild<QPushButton *>(QStringLiteral("snapMeshUvTexels"))->click();
		bool centred = true;
		for (const auto uv : editor.document().mesh().surfaces[0].texCoords)
		{
			const double x = double(uv.u) * 64 - .5, y = double(uv.v) * 32 - .5;
			centred = centred && std::abs(x - std::round(x)) < 1e-4 && std::abs(y - std::round(y)) < 1e-4;
		}
		expect(centred, "Snap UVs to Texels uses the MD2 skin size");
		// Adjust Last Operation changes the snap to texel corners.
		auto snapFields = lastFields<QComboBox>(editor);
		auto gridFields = lastFields<QDoubleSpinBox>(editor);
		if (expect(snapFields.size() == 1 && gridFields.size() == 2 && !snapFields[0]->accessibleName().isEmpty(),
				   "Snap UVs to Texels offers its grid in Adjust Last Operation"))
		{
			// A 128-texel grid (the height follows the width when unset).
			gridFields[0]->setValue(128);
			bool centres = true;
			QStringList detail{QString::number(editor.document().undoSteps())};
			for (const auto uv : editor.document().mesh().surfaces[0].texCoords)
			{
				const double x = double(uv.u) * 128 - .5, y = double(uv.v) * 128 - .5;
				centres = centres && std::abs(x - std::round(x)) < 1e-4 && std::abs(y - std::round(y)) < 1e-4;
				detail << QStringLiteral("%1,%2").arg(x).arg(y);
			}
			expect(centres && editor.document().undoSteps() == 1, "the adjusted snap replays once on a 128-texel grid", detail.join(' '));
		}
		editor.findChild<QAction *>(QStringLiteral("undoMesh"))->trigger();
		// Align, then Adjust Last Operation picks another alignment.
		selectFaces(editor, view, {0, 1});
		editor.findChild<QPushButton *>(QStringLiteral("alignMeshUvs"))->click();
		auto alignFields = lastFields<QComboBox>(editor);
		if (expect(alignFields.size() == 1 && alignFields[0]->count() == 7 && alignFields[0]->currentIndex() == 1, "Align offers its seven choices"))
		{
			alignFields[0]->setCurrentIndex(5);
			const auto &aligned = editor.document().mesh().surfaces[0];
			const auto face = aligned.triangles[0];
			expect(aligned.texCoords[face.a].v == aligned.texCoords[face.b].v && aligned.texCoords[face.b].v == aligned.texCoords[face.c].v &&
					   aligned.texCoords[face.a].v == 1.f && editor.document().undoSteps() == 1,
				   "V to maximum replaces the alignment in one step");
		}
		editor.findChild<QAction *>(QStringLiteral("undoMesh"))->trigger();
		// Pin, then the conformal unwrap holds the pinned corners.
		selectFaces(editor, view, {0});
		editor.findChild<QPushButton *>(QStringLiteral("pinMeshUvs"))->click();
		const auto pinned = editor.document().mesh().surfaces[0].uvPins;
		expect(pinned.size() == 3 && editor.findChild<QLabel *>("meshStatus")->text().contains(QStringLiteral("3")), "Pin UVs pins the face's corners",
			   editor.findChild<QLabel *>("meshStatus")->text());
		selectFaces(editor, view, {0, 1, 2, 3});
		const auto pinnedUvs = editor.document().mesh().surfaces[0].texCoords;
		editor.findChild<QAction *>(QStringLiteral("meshUnwrapConformal"))->trigger();
		bool held = editor.document().mesh().surfaces[0].uvPins == pinned;
		for (int pin : pinned)
			held = held && editor.document().mesh().surfaces[0].texCoords[pin].u == pinnedUvs[pin].u &&
				   editor.document().mesh().surfaces[0].texCoords[pin].v == pinnedUvs[pin].v;
		expect(held, "stored pins hold their UVs through Unwrap (Seams Only)");
		editor.findChild<QAction *>(QStringLiteral("undoMesh"))->trigger();
		selectFaces(editor, view, {0});
		editor.findChild<QPushButton *>(QStringLiteral("unpinMeshUvs"))->click();
		expect(editor.document().mesh().surfaces[0].uvPins.isEmpty(), "Unpin UVs removes the pins");
		editor.findChild<QAction *>(QStringLiteral("undoMesh"))->trigger();
		editor.findChild<QAction *>(QStringLiteral("undoMesh"))->trigger();
		expect(editor.document().mesh().surfaces[0].uvPins.isEmpty() && !editor.document().canUndo(), "pins undo away");
		// Modal scale from the keyboard: S, type 2, Enter.
		selectFaces(editor, view, {0, 1});
		view->activateWindow();
		view->setFocus(Qt::OtherFocusReason);
		settleModelUv(*view);
		const auto before = editor.document().mesh().surfaces[0].texCoords;
		QKeyEvent scaleKey(QEvent::KeyPress, Qt::Key_S, Qt::NoModifier, QStringLiteral("s"));
		QApplication::sendEvent(view, &scaleKey);
		expect(view->modal() == ModelUvModal::Scale && !view->accessibleDescription().isEmpty(), "the profile's Scale key starts a modal scale");
		for (const auto &[key, text] : {std::pair{Qt::Key_2, QStringLiteral("2")}, std::pair{Qt::Key_Return, QString()}})
		{
			QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier, text);
			QApplication::sendEvent(view, &press);
		}
		settleModelUv(*view);
		const auto &after = editor.document().mesh().surfaces[0].texCoords;
		expect(view->modal() == ModelUvModal::None && editor.document().canUndo() && after.size() > before.size(),
			   "typed scale applies one transform, splitting the selection from its neighbours");
		const auto t = editor.document().mesh().surfaces[0].triangles[0];
		expect(std::abs((after[t.b].u - after[t.a].u) - 2 * (before[1].u - before[0].u)) < 1e-5, "the face is twice as wide in U");
		editor.findChild<QAction *>(QStringLiteral("undoMesh"))->trigger();
		const auto undone = source(editor.document().mesh());
		settleModelUv(*view);
		view->setFocus(Qt::OtherFocusReason);
		expect(view->beginModal(ModelUvModal::Rotate, view->uvToScreen({.9f, .5f})) && view->typeModal(QStringLiteral("90")), "modal rotate");
		view->finishModal(false);
		expect(source(editor.document().mesh()) == undone && view->modal() == ModelUvModal::None, "a cancelled modal changes nothing");
		// Conformal unwrap from the UV menu.
		selectFaces(editor, view, {0, 1, 2, 3});
		editor.findChild<QAction *>(QStringLiteral("meshUnwrapConformal"))->trigger();
		settleModelUv(*view);
		expect(view->topology() && view->topology()->islands.size() == 1 && editor.document().canUndo(), "Unwrap (Seams Only) gives one island");
		editor.findChild<QAction *>(QStringLiteral("undoMesh"))->trigger();
		// Quake front/back layout.
		// The strip lies flat in XY, so it faces along Z (the menu uses X).
		expect(editor.layoutQuakeFrontBack(2, &error), "lay out the front and back halves along Z", error);
		bool left = true;
		QStringList us;
		for (const auto uv : editor.document().mesh().surfaces[0].texCoords)
		{
			left = left && uv.u < .5f;
			us << QString::number(uv.u);
		}
		expect(left && editor.findChild<QLabel *>("meshStatus")->text().contains(QStringLiteral("64")),
			   "facing faces fill the left half of the skin", editor.findChild<QLabel *>("meshStatus")->text() + QLatin1Char(' ') + us.join(','));
		const auto frontBack = lastFields<QComboBox>(editor);
		expect(frontBack.size() == 2 && frontBack[0]->currentIndex() == 4 && frontBack[1]->currentIndex() == 2,
			   "Adjust Last Operation shows the front/back layout and its facing axis");
		editor.findChild<QAction *>(QStringLiteral("undoMesh"))->trigger();
		// Export UV Layout through its dialog and the document worker.
		const auto layoutPath = temp.filePath(QStringLiteral("strip_uv_%1.png").arg(scenario));
		QTimer::singleShot(0, [&]
						   {
							   auto *dialog = dynamic_cast<ModelUvLayoutDialog *>(dialogNamed(QStringLiteral("modelUvLayoutDialog")));
							   if (!expect(dialog != nullptr, "Export UV Layout opens its dialog"))
								   return;
							   for (auto *control : dialog->findChildren<QWidget *>())
								   if (control->objectName().startsWith(QStringLiteral("uvLayout")))
									   expect(!control->accessibleName().isEmpty(), "layout controls are named", control->objectName());
							   expect(dialog->findChild<QSpinBox *>(QStringLiteral("uvLayoutWidth"))->value() == 64 &&
										  dialog->findChild<QSpinBox *>(QStringLiteral("uvLayoutHeight"))->value() == 32,
									  "the layout defaults to the MD2 skin size");
							   dialog->findChild<QLineEdit *>(QStringLiteral("uvLayoutPath"))->setText(layoutPath);
							   dialog->accept();
						   });
		editor.findChild<QAction *>(QStringLiteral("exportMeshUvLayout"))->trigger();
		expect(QFile::exists(layoutPath) && QImage(layoutPath).size() == QSize(64, 32), "the layout file is written",
			   editor.findChild<QLabel *>("meshStatus")->text());
		expect(!editor.document().isModified(), "exporting a layout does not change the model");
	}
	// MDL: transfer the skin after a UV edit, and member round trips.
	{
		auto palette = generatedIdTechPalette(QStringLiteral("quake"));
		ModelMesh mdl;
		expect(importEditableModel("fixture.mdl", groupedMdlFixture().bytes, &mdl, &error, &palette), "import the MDL fixture", error);
		ModelEditorDialog editor;
		editor.setAttribute(Qt::WA_DeleteOnClose, false);
		editor.findChild<QAction *>(QStringLiteral("meshRecoveryEnabled"))->setChecked(false);
		expect(editor.setMesh(mdl, &error), "open the MDL", error);
		editor.resize(1440, 960);
		editor.show();
		app.processEvents();
		const auto member = editor.document().mesh().embeddedSkins[0].indexedFrames[0];
		ModelEdit move;
		move.kind = ModelEditKind::TransformUv;
		move.selection.faces = {0, 1};
		move.uvOffset = {.25f, 0};
		expect(editor.applyEdit(move, &error) && editor.canTransferSkin() &&
				   editor.findChild<QAction *>(QStringLiteral("transferMeshSkin"))->isEnabled(),
			   "a UV edit offers Transfer Skin", error);
		const int steps = editor.document().undoSteps();
		QTimer::singleShot(0, [&]
						   {
							   auto *dialog = dialogNamed(QStringLiteral("modelSkinTransferDialog"));
							   if (!expect(dialog != nullptr, "Transfer Skin opens its dialog"))
								   return;
							   dialog->findChild<QPushButton *>(QStringLiteral("skinTransferApply"))->click();
						   });
		editor.findChild<QAction *>(QStringLiteral("transferMeshSkin"))->trigger();
		expect(editor.document().undoSteps() == steps + 1 && editor.document().mesh().embeddedSkins[0].indexedFrames[0] != member,
			   "the transferred member is one undo step", editor.findChild<QLabel *>("meshStatus")->text());
		editor.findChild<QAction *>(QStringLiteral("undoMesh"))->trigger();
		expect(editor.document().mesh().embeddedSkins[0].indexedFrames[0] == member, "undo restores the member");
		const auto exported = temp.filePath(QStringLiteral("member.pcx"));
		expect(editor.exportMdlSkinMember(exported, &error) && QFile::exists(exported), "Export Member writes the member", error);
		auto image = modelMdlSkinMemberImage(editor.document().mesh(), 0, 0).convertToFormat(QImage::Format_ARGB32);
		image.setPixel(0, 0, qRgb(255, 0, 0));
		const auto edited = temp.filePath(QStringLiteral("edited.png"));
		expect(QImageWriter(edited, "png").write(image), "write an edited member");
		const int before = editor.document().undoSteps();
		expect(editor.replaceMdlSkinMemberFromImage(edited, &error) && editor.document().undoSteps() == before + 1,
			   "Replace Member From Image is one undo step", error);
		editor.findChild<QAction *>(QStringLiteral("undoMesh"))->trigger();
		// Edit Skin writes a texture project the project owns and watches it.
		expect(!editor.editSurfaceSkin(&error) && !error.isEmpty(), "Edit Skin needs the Textures workbench");
		editor.deliveryContext = [&]
		{
			ModelEditorDeliveryContext context;
			context.projectRoot = temp.path();
			return context;
		};
		QString opened;
		editor.editSkinFile = [&](const QString &path, QString *)
		{
			opened = path;
			return true;
		};
		expect(editor.editSurfaceSkin(&error) && QFileInfo(opened).absolutePath() == QDir(temp.path()).filePath(QStringLiteral(".vibestudio/model-skins")) &&
				   QFile::exists(opened) && editor.skinRoundTripFiles().size() == 1,
			   "Edit Skin opens its texture project in Textures", error);
		auto *offer = editor.findChild<QWidget *>(QStringLiteral("meshSkinOffer"));
		editor.checkSkinRoundTrips(true);
		expect(offer && !offer->isVisible() && !editor.hasPendingSkin(), "nothing is offered before Textures saves");
		// Textures saves: the skin is offered, then applied as one undo step.
		expect(paintRoundTrip(opened, {0, 0}, qRgb(255, 0, 0), &error), "save the painted project", error);
		editor.checkSkinRoundTrips(true);
		auto *apply = editor.findChild<QToolButton *>(QStringLiteral("applySkinFromTextures"));
		expect(editor.hasPendingSkin() && offer->isVisible() && apply && apply->isEnabled() && !apply->accessibleName().isEmpty(),
			   "a saved skin is offered back", editor.findChild<QLabel *>("meshStatus")->text());
		const auto unpainted = editor.document().mesh().embeddedSkins[0].indexedFrames[0];
		const int steps2 = editor.document().undoSteps();
		apply->click();
		const auto &painted = editor.document().mesh().embeddedSkins[0].indexedFrames[0];
		int changed = 0;
		for (int i = 0; i < painted.size(); ++i)
			changed += painted[i] != unpainted[i];
		expect(editor.document().undoSteps() == steps2 + 1 && changed == 1 && painted[0] != unpainted[0] && !offer->isVisible() &&
				   !editor.hasPendingSkin(),
			   "Apply Skin replaces the member as one undo step, changing only the painted texel",
			   editor.findChild<QLabel *>("meshStatus")->text());
		// Dismiss keeps the model's skin.
		expect(paintRoundTrip(opened, {1, 0}, qRgb(0, 255, 0), &error), "save again", error);
		editor.checkSkinRoundTrips(true);
		const auto kept = editor.document().mesh().embeddedSkins[0].indexedFrames[0];
		editor.findChild<QToolButton *>(QStringLiteral("dismissSkinFromTextures"))->click();
		expect(!editor.hasPendingSkin() && editor.document().mesh().embeddedSkins[0].indexedFrames[0] == kept, "Dismiss keeps the model's skin");
		// With the setting, a save applies at once and the setting persists.
		auto *automatic = editor.findChild<QAction *>(QStringLiteral("meshSkinAutoApply"));
		automatic->setChecked(true);
		expect(StudioSettings(StudioSettings::AccessMode::ReadOnly).modelSkinAutoApply(), "the setting is saved");
		const int steps3 = editor.document().undoSteps();
		expect(paintRoundTrip(opened, {2, 0}, qRgb(0, 0, 255), &error), "save once more", error);
		editor.checkSkinRoundTrips(true);
		expect(!editor.hasPendingSkin() && editor.document().undoSteps() == steps3 + 1, "Apply Saved Skins Automatically applies the save");
		automatic->setChecked(false);
		// Transfer Skin after a topology edit finds the reshaped faces by
		// projection onto the surface before it.
		ModelEdit poke;
		poke.kind = ModelEditKind::PokeFaces;
		poke.selection.faces = {0};
		expect(editor.applyEdit(poke, &error) && editor.canTransferSkin(), "a face edit keeps Transfer Skin available", error);
		const int steps4 = editor.document().undoSteps();
		QTimer::singleShot(0, [&]
						   {
							   auto *dialog = dialogNamed(QStringLiteral("modelSkinTransferDialog"));
							   if (!expect(dialog != nullptr, "Transfer Skin opens after a face edit"))
								   return;
							   auto *mapping = dialog->findChild<QComboBox *>(QStringLiteral("skinTransferMapping"));
							   expect(mapping && mapping->currentIndex() == 0 && !mapping->accessibleName().isEmpty(), "the mapping defaults to automatic");
							   dialog->findChild<QPushButton *>(QStringLiteral("skinTransferApply"))->click();
						   });
		editor.findChild<QAction *>(QStringLiteral("transferMeshSkin"))->trigger();
		expect(editor.document().undoSteps() == steps4 + 1 && editor.findChild<QLabel *>("meshStatus")->text().contains(QStringLiteral("projection")),
			   "the reshaped faces are repainted by projection", editor.findChild<QLabel *>("meshStatus")->text());
	}
	// Texel density across surfaces with their own skin sizes.
	{
		auto mesh = uvIslandsFixture();
		ModelEditorDialog editor;
		editor.setAttribute(Qt::WA_DeleteOnClose, false);
		editor.findChild<QAction *>(QStringLiteral("meshRecoveryEnabled"))->setChecked(false);
		expect(editor.setMesh(mesh, &error), "open the two-surface fixture", error);
		editor.resize(1440, 960);
		editor.show();
		app.processEvents();
		const auto other = editor.document().mesh().surfaces[1].texCoords;
		QString reviewed;
		QTimer::singleShot(0, [&]
						   {
							   auto *dialog = dynamic_cast<ModelUvDensityDialog *>(dialogNamed(QStringLiteral("modelUvDensityDialog")));
							   if (!expect(dialog != nullptr, "Texel Density opens"))
								   return;
							   auto *table = dialog->findChild<QTableWidget *>(QStringLiteral("uvDensitySurfaces"));
							   dialog->findChild<QComboBox *>(QStringLiteral("uvDensityScope"))->setCurrentIndex(3);
							   expect(table && table->isVisibleTo(dialog) && table->rowCount() == 2 && !table->accessibleName().isEmpty() &&
										  !(table->item(0, 0)->flags() & Qt::ItemIsUserCheckable) && (table->item(1, 1)->flags() & Qt::ItemIsEditable),
									  "chosen surfaces list each surface with its skin size");
							   table->item(1, 0)->setCheckState(Qt::Checked);
							   table->item(1, 1)->setData(Qt::EditRole, 128);
							   table->item(1, 2)->setData(Qt::EditRole, 64);
							   dialog->preview();
							   reviewed = QStringLiteral("before %1 after %2 surfaces %3 status %4")
											  .arg(dialog->before().density)
											  .arg(dialog->after().density)
											  .arg(dialog->edit().uvDensity.surfaces.size())
											  .arg(dialog->findChild<QLabel *>(QStringLiteral("uvDensityStatus"))->text());
							   expect(dialog->ready() && dialog->edit().uvDensity.surfaces == QSet<int>{1} &&
										  dialog->edit().uvDensity.surfaceSizes.value(1) == QSize(128, 64),
									  "the preview matches the chosen surface on its own skin size");
							   dialog->accept();
						   });
		editor.findChild<QAction *>(QStringLiteral("inspectMeshUvDensity"))->trigger();
		const auto &now = editor.document().mesh().surfaces[1].texCoords;
		expect(editor.document().canUndo() && (now.size() != other.size() ||
												std::memcmp(now.constData(), other.constData(), size_t(now.size()) * sizeof(ModelTexCoord)) != 0),
			   "the chosen surface is matched too", reviewed);
	}
	std::cout << checks << " UV tools UI checks; " << failures << " failures\n";
	return failures ? 1 : 0;
}
