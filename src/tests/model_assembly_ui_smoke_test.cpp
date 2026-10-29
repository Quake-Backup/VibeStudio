#include "app/model_assembly_dialog.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "core/package_draft.h"
#include "tests/model_assembly_test_helpers.h"
#include "tests/model_viewport_test_helpers.h"

#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
#include <QTreeWidget>

#include <cstdlib>
#include <iostream>

using namespace vibestudio;
namespace
{
bool expect(bool value, const char *message)
{
	if (!value)
	{
		std::cerr << "FAIL: " << message << '\n';
	}
	return value;
}
bool write(const QString &path, const QByteArray &bytes)
{
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
class Expansion final : public QTranslator
{
  public:
	bool isEmpty() const override { return false; }
	QString translate(const char *context, const char *source, const char *, int) const override
	{
		return QByteArray(context) == "ModelAssemblyDialog" ? QStringLiteral("[ %1 / %1 ]").arg(QString::fromUtf8(source)) : QString();
	}
};
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
	{
		return EXIT_FAILURE;
	}
	QTemporaryDir temporary(QDir(root).filePath("assembly-ui-XXXXXX"));
	if (!temporary.isValid())
	{
		return EXIT_FAILURE;
	}
	const auto path = [&](const char *name) { return QDir(temporary.path()).filePath(QString::fromLatin1(name)); };
	StudioSettings::setOverrideFilePath(path("settings.ini"));
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	const auto fixture = QJsonDocument(editableModelJson(tests::assemblyModel())).toJson();
	bool ok = expect(write(path("part.mesh.json"), fixture), "original model fixture");
	QString error;
	ModelAssemblyDialog editor;
	editor.setAttribute(Qt::WA_DeleteOnClose, false);
	editor.show();
	const auto recipe = tests::assemblyRecipe(path("part.mesh.json"));
	ok &= expect(editor.setAssembly(recipe, temporary.path(), &error) && editor.previewReady(), "assembly resolves and previews in GUI");
	ok &= expect(editor.setTime(.5, &error) && editor.pose().mesh.surfaces[1].frames[0].positions[0].x == 15 &&
					 editor.pose().mesh.surfaces[1].frames[0].positions[0].z == 1,
				 "GUI independently samples child and moving parent tag");
	auto *viewport = editor.findChild<ModelViewport *>("assemblyPreview");
	auto *tree = editor.findChild<QTreeWidget *>("assemblyParts");
	auto *partId = editor.findChild<QLineEdit *>("assemblyPartId");
	auto *apply = editor.findChild<QPushButton *>("assemblyApplyPart");
	auto *play = editor.findChild<QAction *>("assemblyPlay");
	auto *time = editor.findChild<QDoubleSpinBox *>("assemblyTime");
	ok &= expect(viewport && tree && partId && apply && play && time, "document controls exposed with stable identities");
	if (!ok)
	{
		std::cerr << error.toStdString() << '\n';
		return EXIT_FAILURE;
	}
	ok &= expect(tree->topLevelItemCount() == 1 && tree->topLevelItem(0)->childCount() == 1, "hierarchical tree follows parent links");
	tree->setCurrentItem(tree->topLevelItem(0)->child(0));
	ok &= expect(editor.document().selectedPart() == "child" && viewport->highlightedTriangleCount() == 1,
				 "semantic tree selection highlights matching geometry");
	partId->setText("tool");
	apply->click();
	ok &= expect(editor.document().selectedPart() == "tool" && editor.document().assembly().parts[1].id == "tool",
				 "Apply Part commits inspector fields");
	ok &= expect(editor.undo(&error) && editor.document().selectedPart() == "child" && editor.redo(&error) &&
					 editor.document().selectedPart() == "tool",
				 "selection-aware document undo and redo");
	ok &= expect(editor.saveSource(path("saved.assembly.json"), false, &error) && !editor.document().isModified(),
				 "GUI source save marks exact recipe clean");
	const auto fingerprint = modelAssemblyFingerprint(editor.document().assembly());
	auto changed = editor.document().assembly().parts[1];
	changed.translation.x = 25;
	bool cancelledWhileBusy = false;
	QTimer::singleShot(0, &editor,
					   [&]
					   {
						   cancelledWhileBusy = editor.operationBusy();
						   editor.cancelOperation();
					   });
	ok &= expect(!editor.applyPart("tool", changed, &error) && cancelledWhileBusy &&
					 modelAssemblyFingerprint(editor.document().assembly()) == fingerprint && !editor.document().isModified() &&
					 editor.previewReady(),
				 "cancelled assembly preparation leaves recipe, saved state and preview unchanged");
	changed.source = path("missing.md3");
	ok &= expect(editor.applyPart("tool", changed, &error) && !editor.previewReady() && !error.isEmpty() &&
					 !editor.findChild<QAction *>("assemblyBake")->isEnabled(),
				 "broken references remain editable with bake disabled");
	ok &= expect(editor.undo(&error) && editor.previewReady(), "undo repairs a missing input without losing history");
	ok &= expect(editor.exportPose(path("baked.obj"), false, &error), "GUI derivative export uses shared guarded writer");
	ok &= expect(!editor.exportPose(path("part.mesh.json"), true, &error), "GUI export cannot replace input model");
	ok &= expect(editor.removeBranch("root", &error) && !editor.previewReady() && editor.undo(&error),
				 "branch removal and undo preserve descendants");
	{
		// Attachment blend: an accessible, undoable assembly-level setting.
		auto *blend = editor.findChild<QComboBox *>("assemblyTagInterpolation");
		bool labelled = false;
		for (auto *label : editor.findChildren<QLabel *>())
			labelled |= blend && label->buddy() == blend;
		ok &= expect(blend && labelled && !blend->accessibleName().isEmpty() && !blend->accessibleDescription().isEmpty() &&
						 blend->currentData().toInt() == int(ModelTagInterpolation::ShortestArc),
					 "attachment blend control is labelled, described and shows the stored mode");
		if (blend)
		{
			blend->setCurrentIndex(blend->findData(int(ModelTagInterpolation::Quake3Renderer)));
			ok &= expect(editor.document().assembly().tagInterpolation == ModelTagInterpolation::Quake3Renderer && editor.previewReady() &&
							 editor.findChild<QPlainTextEdit *>("assemblyDependencyDetails")->toPlainText().contains("Quake III renderer"),
						 "choosing the Quake III blend resamples and reports it in Details");
			ok &= expect(editor.undo(&error) && editor.document().assembly().tagInterpolation == ModelTagInterpolation::ShortestArc &&
							 blend->currentData().toInt() == int(ModelTagInterpolation::ShortestArc),
						 "undo restores the blend and the control follows");
		}
		// Part collision in bakes (ATT-ASM-COLL-01): a session choice.
		auto *carry = editor.findChild<QCheckBox *>("assemblyIncludeCollision");
		ok &= expect(carry && !carry->accessibleName().isEmpty() && !carry->accessibleDescription().isEmpty() && !carry->isChecked(),
					 "the collision choice is named, described and off by default");
		editor.setIncludeCollision(true);
		ok &= expect(editor.includesCollision() && editor.exportPose(path("carried.mesh.json"), false, &error) &&
						 QFileInfo::exists(path("carried.mesh.json")),
					 "a pose bake with part collision writes an editable source");
		editor.setIncludeCollision(false);
		// Weapon packages (ATT-WEAPON-01): reachable, and refused clearly without a package.
		ModelWeaponBundle weapon;
		ok &= expect(editor.findChild<QAction *>("assemblyWeaponBundle") &&
						 !editor.prepareWeaponBundle({QStringLiteral("rail"), editor.document().assembly().parts.value(0).id, {}, {}, {}},
													 &weapon, &error) &&
						 error.contains("package"),
					 "Weapon Package is on the toolbar and needs the project package");
		// Quake II players (ATT-Q2PLAYER-01): reachable, and refused clearly without a palette package.
		ModelQ2PlayerOptions q2;
		q2.modelName = QStringLiteral("vamp");
		q2.bodySource = path("part.mesh.json");
		q2.weaponSource = path("part.mesh.json");
		q2.skins = {{QStringLiteral("red"), path("part.mesh.json"), {}}};
		ModelQ2PlayerBundle q2Bundle;
		ok &= expect(editor.findChild<QAction *>("assemblyQ2Player") && !editor.prepareQ2PlayerBundle(q2, &q2Bundle, &error) &&
						 error.contains("colormap.pcx"),
					 "Quake II Player is on the toolbar and asks for the palette package");
	}
	{
		// Muzzle flash visibility: preview only, for parts on a tag_flash.
		auto flashModel = tests::assemblyModel();
		for (auto &tag : flashModel.tags)
			tag.name = QStringLiteral("tag_flash");
		ok &= expect(write(path("flashgun.mesh.json"), QJsonDocument(editableModelJson(flashModel)).toJson()), "flash fixture");
		auto flashRecipe = tests::assemblyRecipe(path("flashgun.mesh.json"));
		flashRecipe.parts[1].tag = QStringLiteral("tag_flash");
		ModelAssemblyDialog gun;
		gun.setAttribute(Qt::WA_DeleteOnClose, false);
		gun.show();
		auto *showFlash = gun.findChild<QCheckBox *>("assemblyShowFlash");
		auto *gunView = gun.findChild<ModelViewport *>("assemblyPreview");
		ok &= expect(gun.setAssembly(flashRecipe, temporary.path(), &error) && showFlash && gunView && showFlash->isChecked() &&
						 !showFlash->accessibleDescription().isEmpty() && gunView->mesh().surfaces.size() == 2,
					 "the flash toggle is described and on by default");
		if (showFlash && gunView)
		{
			const auto recipeBefore = modelAssemblyFingerprint(gun.document().assembly());
			showFlash->setChecked(false);
			ok &= expect(gunView->mesh().surfaces.size() == 1 && gun.pose().mesh.surfaces.size() == 2 &&
							 modelAssemblyFingerprint(gun.document().assembly()) == recipeBefore,
						 "hiding the flash leaves it out of the preview only");
			gun.selectPart(QStringLiteral("child"));
			const int hiddenHighlight = gunView->highlightedTriangleCount();
			gun.selectPart(QStringLiteral("root"));
			ok &= expect(hiddenHighlight == 0 && gunView->highlightedTriangleCount() == 1,
						 "selection highlights only drawn surfaces while the flash is hidden");
			showFlash->setChecked(true);
			ok &= expect(gunView->mesh().surfaces.size() == 2, "showing the flash draws it again");
		}
		// A saved, unmodified recipe closes without asking.
		ok &= expect(gun.saveSource(path("flashgun.assembly.json"), false, &error) && !gun.document().isModified(), "flash recipe saved");
		gun.close();
	}
	const auto beforePlayback = modelAssemblyFingerprint(editor.document().assembly());
	const double startTime = editor.timeSeconds();
	// Playback holds its next sample while a frame renders, and this test's
	// first frame waits for the GPU renderer to start (the shell starts it at
	// launch). Measure playback from a drawn view, not from renderer start-up.
	ok &= expect(tests::settleModelViewport(*viewport), "the assembly view has drawn before playback");
	play->setChecked(true);
	QEventLoop playback;
	QTimer::singleShot(300, &playback, &QEventLoop::quit);
	playback.exec();
	play->setChecked(false);
	ok &= expect(editor.timeSeconds() > startTime && modelAssemblyFingerprint(editor.document().assembly()) == beforePlayback &&
					 !editor.operationBusy() && tests::settleModelViewport(*viewport),
				 "elapsed playback advances rendered poses without editing the recipe");
	editor.setAccessibility(true, true);
	ok &= expect(!play->isEnabled(), "reduced motion disables automatic assembly playback");
	time->setValue(.25);
	ok &= expect(editor.timeSeconds() == .25, "manual time sampling works with reduced motion");
	auto *accessibleTree = QAccessible::queryAccessibleInterface(tree);
	auto *accessibleId = QAccessible::queryAccessibleInterface(partId);
	ok &= expect(accessibleTree && !accessibleTree->text(QAccessible::Name).isEmpty() && accessibleId &&
					 !accessibleId->text(QAccessible::Name).isEmpty() && partId->focusPolicy() != Qt::NoFocus &&
					 apply->focusPolicy() != Qt::NoFocus,
				 "standard widgets expose names, hierarchy roles and keyboard focus");
	QApplication::processEvents();
	ok &= expect(tests::settleModelViewport(*viewport), "assembly viewport worker settles");
	const auto capture = qEnvironmentVariable("VIBESTUDIO_MODELLER_CAPTURE_DIR");
	if (!capture.isEmpty())
	{
		QDir().mkpath(capture);
		QImage image(editor.size() * editor.devicePixelRatioF(), QImage::Format_ARGB32_Premultiplied);
		image.setDevicePixelRatio(editor.devicePixelRatioF());
		editor.render(&image);
		ok &= expect(image.save(QDir(capture).filePath("assembly-standard.png")), "widget-owned standard render");
	}
	for (auto theme : {StudioTheme::HighContrastDark, StudioTheme::HighContrastLight})
	{
		applyStudioTheme(app, studioThemeTokens(theme, UiDensity::Comfortable, 200));
		QApplication::processEvents();
		ok &= expect(tests::settleModelViewport(*viewport) && editor.minimumSizeHint().width() <= editor.width(),
					 "200% high-contrast layout remains reachable");
	}
	Expansion expansion;
	app.installTranslator(&expansion);
	ModelAssemblyDialog expanded;
	expanded.setAttribute(Qt::WA_DeleteOnClose, false);
	expanded.setLayoutDirection(Qt::RightToLeft);
	expanded.setAccessibility(true, true);
	expanded.resize(1480, 940);
	expanded.show();
	ok &= expect(expanded.openSource(path("saved.assembly.json"), &error) && expanded.previewReady(), "expanded RTL source reopen");
	QApplication::processEvents();
	auto *expandedPreview = expanded.findChild<ModelViewport *>("assemblyPreview");
	auto *expandedScroll = expanded.findChild<QScrollArea *>("assemblyInspectorScroll");
	ok &= expect(expandedScroll && expandedScroll->horizontalScrollBar()->maximum() == 0,
				 "expanded RTL inspector fields fit without horizontal clipping");
	if (expandedScroll)
	{
		for (auto *label : expandedScroll->widget()->findChildren<QLabel *>())
		{
			ok &= expect(label->width() > 0 && label->height() <= label->fontMetrics().lineSpacing() * 5,
						 "expanded inspector labels retain usable wrapped geometry");
		}
	}
	ok &= expect(expandedPreview && tests::settleModelViewport(*expandedPreview), "expanded RTL viewport completes");
	if (!capture.isEmpty())
	{
		QImage image(expanded.size() * expanded.devicePixelRatioF(), QImage::Format_ARGB32_Premultiplied);
		image.setDevicePixelRatio(expanded.devicePixelRatioF());
		expanded.render(&image);
		ok &= expect(image.save(QDir(capture).filePath("assembly-expanded-rtl.png")), "widget-owned expanded RTL render");
	}
	expanded.close();
	app.removeTranslator(&expansion);
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	PackageStagingModel staging;
	ok &= expect(staging.createEmpty(PackageArchiveFormat::Pak, {}, &error) && staging.addBytes(fixture, "models/part.mesh.json", &error),
				 "staged model context");
	auto packageRecipe = tests::assemblyRecipe("models/part.mesh.json");
	for (auto &part : packageRecipe.parts)
	{
		part.sourceKind = ModelAssemblySource::Package;
	}
	editor.setMaterialSource({std::make_shared<PackageArchive>(packagePlannedArchive(staging)), "staged-1", "quake"});
	ok &= expect(editor.setAssembly(packageRecipe, temporary.path(), &error) && editor.previewReady(),
				 "staged package models compose directly");
	ok &= expect(staging.deleteEntry("models/part.mesh.json", &error), "delete staged input from next snapshot");
	editor.setMaterialSource({std::make_shared<PackageArchive>(packagePlannedArchive(staging)), "staged-2", "quake"});
	ok &= expect(!editor.previewReady() && editor.document().assembly().parts.size() == 2,
				 "context revision clears obsolete geometry while retaining repairable recipe");
	ok &= expect(editor.openSource(path("saved.assembly.json"), &error) && !editor.document().isModified(),
				 "reload clean source before closing");
	editor.close();
	QPointer<ModelAssemblyDialog> closing = new ModelAssemblyDialog;
	ok &= expect(closing->openSource(path("saved.assembly.json"), &error), "deferred close fixture");
	closing->show();
	bool continuation = false;
	QTimer::singleShot(0, closing, [&] { closing->requestClose([&] { continuation = true; }); });
	ok &= expect(!closing->reloadInputs(&error), "close cancels the active assembly worker");
	QApplication::processEvents();
	QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
	QApplication::processEvents();
	ok &= expect(!closing && continuation, "deferred close destroys only after worker completion and resumes caller once");
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
