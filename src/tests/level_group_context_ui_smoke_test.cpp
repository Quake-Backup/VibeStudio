#include "app/application_shell.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/level_scene_panel.h"
#include "app/studio_theme.h"
#include "core/level_scene.h"
#include "tests/level_geometry_test_helpers.h"
#include "tests/level_object_test_helpers.h"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QLabel>
#include <QPushButton>
#include <QShortcut>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
#include <QTreeWidget>
#include <iostream>

using namespace slopstudio;
namespace {
bool expect(bool value, const char* label)
{
	if (!value) { std::cerr << "FAIL: " << label << '\n'; } return value;
}
bool until(const std::function<bool()>& ready)
{
	QElapsedTimer timer; timer.start();
	while (!ready() && timer.elapsed() < 30000) {
		QEventLoop loop; QTimer::singleShot(20,&loop,&QEventLoop::quit); loop.exec(QEventLoop::ExcludeUserInputEvents);
	}
	return ready();
}
using Refs = QVector<LevelMapSelectionRef>;
// The view binds Tab and its relatives as shortcuts of its own, since
// QWidget::event() takes Tab for focus navigation first: activate the one
// bound to `keys`, as the key would.
bool key(MapViewport* view, const QKeySequence& keys)
{
	for (auto* shortcut : view->findChildren<QShortcut*>(QString(),Qt::FindDirectChildrenOnly)) {
		if (shortcut->key() == keys) { Q_EMIT shortcut->activated(); return true; }
	}
	return false;
}
class Expanded final : public QTranslator {
public:
	bool isEmpty() const override { return false; }
	QString translate(const char*,const char* text,const char*,int) const override { return QStringLiteral("[%1 expanded]").arg(QString::fromUtf8(text)); }
};
}
int main(int argc, char** argv)
{
	// Semantic calls and owned Qt render targets only: no native input events.
	qputenv("QT_QPA_PLATFORM","offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR",QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc,argv); QTemporaryDir temp; bool ok = true; QString error;
#ifdef Q_OS_WIN
	app.setFont(QFont(QStringLiteral("Segoe UI"),10));
#endif
	if (!temp.isValid()) { return 1; }
	StudioSettings::setOverrideFilePath(temp.filePath("settings.ini")); StudioSettings settings;
	settings.setRestoreSession(false); settings.setReducedMotion(true); settings.setSelectedEditorProfileId("trenchbroom"); settings.setLevelViewLayoutPreference("four-views"); settings.sync();
	LevelMapDocument map; if (!tests::createGeometryFixture(3,&map,&error)) { return 1; }
	QString outer,inner;
	createLevelSceneNode(&map,LevelSceneNodeKind::Group,"Outer",{},&outer);
	createLevelSceneNode(&map,LevelSceneNodeKind::Group,"Inner",outer,&inner);
	assignLevelSceneObjects(&map,outer,{"brush:0"}); assignLevelSceneObjects(&map,inner,{"brush:1"});
	const auto filePath = temp.filePath("groups.map"); QFile file(filePath);
	if (!file.open(QIODevice::WriteOnly)) { return 1; } file.write(serializeLevelMap(map).bytes); file.close();
	applyStudioTheme(app,studioThemeTokens(StudioTheme::Dark,UiDensity::Standard,100));
	ApplicationShell shell; shell.resize(1700,1100); shell.show(); shell.openPathFromCommandLine(filePath);
	shell.findChild<QAction*>("shell.mode.levels")->trigger();
	auto* plan = shell.findChild<MapViewport*>("mapViewport"); auto* camera = shell.findChild<ModelViewport*>("mapPreview3D");
	if (!plan || !camera || !until([&] { return shell.levelDocument().brushes.size() == 3 && camera->isEnabled() && camera->hasMesh(); })) { return 1; }
	const auto baseline = serializeLevelMap(shell.levelDocument()).bytes;
	const LevelMapSelectionRef first{LevelMapSelectionKind::QuakeBrush,0},second{LevelMapSelectionKind::QuakeBrush,1},outside{LevelMapSelectionKind::QuakeBrush,2};
	auto* objects = shell.findChild<LevelObjectList*>("levelMapObjects");
	if (!objects) { return 1; }
	tests::setObjectCurrentRow(objects,objects->rowForSelector("entity:0"));
	ok &= expect(shell.levelDocument().selection == QVector<LevelMapSelectionRef>{{LevelMapSelectionKind::Entity,0}},"world properties remain selectable at map level");
	tests::setObjectCurrentRow(objects,objects->rowForSelector("brush:1"));
	ok &= expect(shell.levelDocument().selection.size() == 2 && objects->selectedReferences().size() == 2,"Objects list expands closed group selection");
	tests::setObjectSelected(objects,objects->rowForSelector("brush:0"),false);
	ok &= expect(shell.levelDocument().selection.isEmpty() && objects->selectedReferences().isEmpty(),"deselecting a group row clears the whole unit");
	plan->selectSceneObjects({second},LevelScenePick::Replace);
	ok &= expect(shell.levelDocument().selection.size() == 2,"plan pick selects outer group");
	ok &= expect(shell.findChild<QAction*>("map.openGroup")->isEnabled(),"open group command follows unit selection");
	ok &= expect(!shell.findChild<QAction*>("map.editBrushComponents")->isEnabled(),"closed group cannot enter single-brush numeric editor");
	plan->selectSceneObjects({first,second},LevelScenePick::Toggle);
	ok &= expect(shell.levelDocument().selection.isEmpty(),"plan band toggle removes group once");
	plan->selectSceneObjects({first},LevelScenePick::Replace);
	shell.findChild<QAction*>("map.openGroup")->trigger();
	ok &= expect(shell.levelDocument().openSceneGroups == QStringList{outer} && shell.levelDocument().selection.isEmpty(),"open command enters group and clears selection");
	// Tab steps through what the open group holds and comes round: its own
	// brush, then the group nested in it. It used to stop dead on the first
	// object outside the group, which picking refuses.
	ok &= expect(key(plan,QKeySequence(Qt::Key_Tab)) && shell.levelDocument().selection == Refs{first},"Tab in an open group selects its first object");
	ok &= expect(key(plan,QKeySequence(Qt::Key_Tab)) && shell.levelDocument().selection == Refs{second},"Tab steps on to the group nested in it");
	ok &= expect(key(plan,QKeySequence(Qt::Key_Tab)) && shell.levelDocument().selection == Refs{first},"Tab passes over what lies outside the open group and comes round");
	ok &= expect(key(plan,QKeySequence(Qt::CTRL | Qt::Key_Tab)) && shell.levelDocument().selection == Refs{first,second},"Ctrl+Tab extends the selection inside the open group");
	ok &= expect(key(plan,QKeySequence(Qt::CTRL | Qt::Key_Tab)) && shell.levelDocument().selection.size() == 2 && !shell.levelDocument().selection.contains(outside),
		"Ctrl+Tab does not stall on, or take in, an object outside the open group");
	plan->selectSceneObjects({outside},LevelScenePick::Replace);
	ok &= expect(shell.levelDocument().selection.isEmpty(),"outside plan pick cannot escape context");
	plan->selectSceneObjects({first},LevelScenePick::Replace);
	ok &= expect(shell.levelDocument().selection == QVector<LevelMapSelectionRef>{first}
		&& shell.findChild<QAction*>("map.editBrushComponents")->isEnabled(),"open group exposes direct brush editor");
	shell.findChild<QAction*>("map.selectAll")->trigger();
	ok &= expect(shell.levelDocument().selection.size() == 2 && !shell.levelDocument().selection.contains(outside),"Select All stays in open context");
	shell.findChild<QAction*>("map.invertSelection")->trigger();
	ok &= expect(shell.levelDocument().selection.isEmpty(),"Invert Selection stays in open context");
	tests::setObjectCurrentRow(objects,objects->rowForSelector("brush:2"));
	ok &= expect(shell.levelDocument().selection.isEmpty() && objects->selectedReferences().isEmpty(),"Objects list cannot select outside group");
	plan->selectSceneObjects({first},LevelScenePick::Replace);
	shell.findChild<QAction*>("map.vertexTool")->trigger();
	ok &= expect(plan->componentHandleCount() == 8,"open group exposes component handles");
	const auto handle = plan->componentHandlePosition(0);
	plan->pickAllComponents();
	ok &= expect(plan->beginComponentMove(handle) && plan->updateComponentMove(handle+QPointF(28,-28)),"component draft starts in open group");
	shell.findChild<QAction*>("map.closeGroup")->trigger();
	plan->finishComponentMove(true);
	QEventLoop settleDraft; QTimer::singleShot(200,&settleDraft,&QEventLoop::quit); settleDraft.exec(QEventLoop::ExcludeUserInputEvents);
	ok &= expect(!plan->isMovingComponents() && serializeLevelMap(shell.levelDocument()).bytes == baseline
		&& shell.levelDocument().editState != QStringLiteral("modified"),"closing context cancels pending component commit");
	shell.findChild<QAction*>("map.vertexTool")->trigger();
	ok &= expect(plan->componentHandleCount() == 0,"closed group has no editable component handles");
	shell.findChild<QAction*>("map.openGroup")->trigger();
	plan->selectSceneObjects({second},LevelScenePick::Replace); shell.findChild<QAction*>("map.openGroup")->trigger();
	ok &= expect(shell.levelDocument().openSceneGroups == QStringList{outer,inner},"nested group opens independently");
	ok &= expect(!shell.findChild<QComboBox*>("levelSceneCreation")->isEnabled(),"open context fixes creation destination");
	shell.findChild<QPushButton*>("levelSceneCloseGroup")->click();
	ok &= expect(shell.levelDocument().openSceneGroups == QStringList{outer} && shell.levelDocument().selection == QVector<LevelMapSelectionRef>{second},"scene button closes child and selects it");
	shell.findChild<QAction*>("map.closeAllGroups")->trigger();
	ok &= expect(shell.levelDocument().openSceneGroups.isEmpty() && serializeLevelMap(shell.levelDocument()).bytes == baseline
		&& shell.levelDocument().editState != QStringLiteral("modified") && !shell.levelDocument().undoStack.isEmpty(),"TB context navigation preserves native map with non-content history");
	ok &= expect(plan->openGroupAt(plan->viewPointFor(0,0)) && shell.levelDocument().openSceneGroups == QStringList{outer},"plan group activation resolves actual hit geometry");
	ok &= expect(plan->openGroupAt(QPointF(5,5)) && shell.levelDocument().openSceneGroups.isEmpty(),"plan empty-space activation closes context");
	// Triangle zero belongs to brush zero in this original fixture. Exercise
	// camera selection semantically without mouse/key events or desktop capture.
	camera->setCameraView({-96,-120,88},50,-30);
	const auto settleCamera = [&] { return until([&] { QImage image(camera->size(),QImage::Format_ARGB32_Premultiplied); camera->render(&image); return camera->isEnabled() && !camera->isRendering(); }); };
	ok &= settleCamera();
	plan->selectSceneObjects({},LevelScenePick::Replace);
	camera->triangleClicked(0,0);
	ok &= expect(shell.levelDocument().selection.size() == 2,"camera triangle pick resolves closed group");
	camera->trianglePicked(0,0,static_cast<int>(ModelViewportPick::Toggle));
	ok &= expect(shell.levelDocument().selection.isEmpty(),"camera toggle removes entire group");
	ok &= settleCamera();
	QPointF facePoint;
	ok &= expect(camera->projectToView({0,-16,0},&facePoint) && camera->activateGroupAt(facePoint)
		&& shell.levelDocument().openSceneGroups == QStringList{outer},"camera group activation resolves presented geometry");
	ok &= settleCamera();
	ok &= expect(camera->activateGroupAt(QPointF(5,5)) && shell.levelDocument().openSceneGroups.isEmpty(),"camera empty-space activation closes context");

	// At map level a closed group is one stop of the Tab cycle, at its first
	// member, and what lies between its members in the map's order is still
	// reached: the cycle used to carry on from the group's last member.
	{
		LevelMapDocument cycle; ok &= expect(tests::createGeometryFixture(4,&cycle,&error),"cycle fixture");
		QString pair;
		ok &= expect(createLevelSceneNode(&cycle,LevelSceneNodeKind::Group,"Pair",{},&pair) && assignLevelSceneObjects(&cycle,pair,{"brush:0","brush:2"}),"cycle group");
		MapViewport view; view.resize(640,480); view.setDocument(cycle);
		const LevelMapSelectionRef world{LevelMapSelectionKind::Entity,cycle.entities.first().id};
		const LevelMapSelectionRef b0{LevelMapSelectionKind::QuakeBrush,0},b1{LevelMapSelectionKind::QuakeBrush,1},b2{LevelMapSelectionKind::QuakeBrush,2},b3{LevelMapSelectionKind::QuakeBrush,3};
		const auto tab = [&] { key(&view,QKeySequence(Qt::Key_Tab)); return view.selectionSet(); };
		const auto back = [&] { key(&view,QKeySequence(Qt::SHIFT | Qt::Key_Tab)); return view.selectionSet(); };
		ok &= expect(tab() == Refs{world},"Tab starts at the first object");
		ok &= expect(tab() == Refs{b0,b2},"Tab selects a closed group whole at its first member");
		ok &= expect(tab() == Refs{b1},"the next Tab reaches the object between the group's members");
		ok &= expect(tab() == Refs{b3},"and passes over the group's other member");
		ok &= expect(tab() == Refs{world},"then comes round");
		ok &= expect(back() == Refs{world,b3} && back() == Refs{world,b3,b1} && back() == Refs{world,b3,b1,b0,b2},
			"Shift+Tab extends backwards over the same stops, the group once");
		// A group whose first member a filter hides is reached at its next one.
		view.setSelectionSet({}); view.setFilteredObjects({b0});
		ok &= expect(tab() == Refs{world} && tab() == Refs{b1} && tab() == Refs{b0,b2} && tab() == Refs{b3},
			"a group stays one stop when its first member is hidden");
	}

	const auto output = qEnvironmentVariable("SLOPSTUDIO_TEST_CAPTURE_DIR"); if (!output.isEmpty()) { QDir().mkpath(output); }
	for (int scale : {100,200}) {
		Expanded translator; if (scale == 200) { app.installTranslator(&translator); }
		applyStudioTheme(app,studioThemeTokens(scale == 200 ? StudioTheme::HighContrastLight : StudioTheme::Dark,UiDensity::Standard,scale));
		auto copy = map; openLevelSceneGroup(&copy,inner);
		LevelScenePanel panel(&copy); panel.resize(620,1050); panel.setLayoutDirection(scale == 200 ? Qt::RightToLeft : Qt::LeftToRight); panel.show(); panel.refresh();
		QObject::connect(&panel,&LevelScenePanel::contextRequested,&panel,[&](const QString& id,bool all) { if (id.isEmpty()) { closeLevelSceneGroup(&copy,all); } else { openLevelSceneGroup(&copy,id); } panel.refresh(); });
		const auto* label = panel.findChild<QLabel*>("levelSceneContext");
		ok &= expect(label && label->text().contains("Outer / Inner") && !label->accessibleName().isEmpty(),"context breadcrumb is accessible and translated");
		QImage image(panel.size(),QImage::Format_ARGB32_Premultiplied); panel.render(&image);
		if (!output.isEmpty()) { ok &= image.save(QDir(output).filePath(QStringLiteral("group-context-%1.png").arg(scale))); }
		panel.findChild<QPushButton*>("levelSceneCloseGroup")->click();
		ok &= expect(copy.openSceneGroups == QStringList{outer},"scaled scene controls remain functional");
		if (scale == 200) { app.removeTranslator(&translator); }
	}
	return ok ? 0 : 1;
}
