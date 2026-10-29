#include "app/model_editor_dialog.h"
#include "app/model_editor_frames.h"
#include "app/model_editor_tools.h"
#include "app/model_obj_import_dialog.h"
#include "app/model_skeletal_dialog.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "core/model_frame_tools.h"
#include "core/studio_settings.h"
#include "tests/model_frame_tools_test_helpers.h"
#include "tests/model_skin_provenance_test_helpers.h"
#include "tests/model_viewport_test_helpers.h"

#include <QAbstractSlider>
#include <QAccessible>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QKeyEvent>
#include <QLineEdit>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFont>
#include <QJsonDocument>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTranslator>

#include <cstdlib>
#include <iostream>
#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#endif

using namespace vibestudio;
namespace
{
bool ok = true;
bool expect(bool condition, const QString &message)
{
	if (!condition)
	{
		std::cerr << "FAIL: " << qPrintable(message) << '\n';
		ok = false;
	}
	return condition;
}
QByteArray bytes(const ModelMesh &mesh) { return QJsonDocument(editableModelJson(mesh)).toJson(QJsonDocument::Compact); }
class Expansion final : public QTranslator
{
  public:
	bool isEmpty() const override { return false; }
	QString translate(const char *context, const char *text, const char *, int) const override
	{
		if (QByteArray(context) != "VibeStudioModelEditor")
			return {};
		const auto value = QString::fromUtf8(text);
		return QStringLiteral("[%1%2]").arg(value, QString(value.size() / 2, '~'));
	}
};
ModelMesh serviceResult(const ModelMesh &mesh, ModelEditKind kind, int first, int last, int destination = -1)
{
	ModelDocument document;
	ModelEdit edit;
	edit.kind = kind;
	edit.frameRange.first = first;
	edit.frameRange.last = last;
	edit.frameRange.destination = destination;
	QString error;
	document.setMesh(mesh, &error);
	document.edit(edit, &error);
	return document.mesh();
}
} // namespace

int main(int argc, char **argv)
{
#if defined(_MSC_VER) && defined(_DEBUG)
	for (int type : {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT})
	{
		_CrtSetReportMode(type, _CRTDBG_MODE_FILE);
		_CrtSetReportFile(type, _CRTDBG_FILE_STDERR);
	}
#endif
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc, argv);
#ifdef Q_OS_WIN
	app.setFont(QFont("Segoe UI", 10));
#endif
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
		return EXIT_FAILURE;
	QTemporaryDir temporary(QDir(root).filePath("frame-tools-ui-XXXXXX"));
	if (!temporary.isValid())
		return EXIT_FAILURE;
	StudioSettings::setOverrideFilePath(QDir(temporary.path()).filePath("settings.ini"));
	QString error;
	const auto original = tests::frameRangeFixture();
	for (int scenario = 0; scenario < 3; ++scenario)
	{
		Expansion expansion;
		if (scenario > 0)
			app.installTranslator(&expansion);
		applyStudioTheme(app, studioThemeTokens(scenario == 0	? StudioTheme::Dark
												: scenario == 1 ? StudioTheme::HighContrastLight
																: StudioTheme::HighContrastDark,
												UiDensity::Standard, scenario == 0 ? 100 : 200));
		ModelEditorDialog editor;
		editor.setLayoutDirection(scenario == 1 ? Qt::RightToLeft : Qt::LeftToRight);
		editor.setAccessibility(scenario > 0, true);
		expect(editor.setMesh(original, &error), QStringLiteral("open the frame range fixture: ") + error);
		editor.resize(scenario == 0 ? 1320 : 2080, scenario == 0 ? 920 : 1360);
		editor.show();
		app.processEvents();
		auto *first = editor.findChild<QSpinBox *>("meshFrameRangeFirst");
		auto *last = editor.findChild<QSpinBox *>("meshFrameRangeLast");
		auto *destination = editor.findChild<QSpinBox *>("meshFrameRangeDestination");
		auto *start = editor.findChild<QSpinBox *>("meshFrameNameStart");
		auto *renameClips = editor.findChild<QCheckBox *>("meshFrameNameRenameClips");
		auto *move = editor.findChild<QPushButton *>("moveMeshFrames");
		auto *duplicate = editor.findChild<QPushButton *>("duplicateMeshFrameRange");
		auto *remove = editor.findChild<QPushButton *>("deleteMeshFrameRange");
		auto *reverse = editor.findChild<QPushButton *>("reverseMeshFrameRange");
		auto *pingPong = editor.findChild<QPushButton *>("pingPongMeshFrameRange");
		auto *name = editor.findChild<QPushButton *>("nameMeshFramesFromClips");
		auto *check = editor.findChild<QLabel *>("meshNativeClipCheck");
		auto *clip = editor.findChild<QComboBox *>("meshAnimationClip");
		auto *frame = editor.findChild<QComboBox *>("meshFrame");
		auto *tabs = editor.findChild<QTabWidget *>("meshInspector");
		auto *undo = editor.findChild<QAction *>("undoMesh");
		if (!expect(first && last && destination && start && renameClips && move && duplicate && remove && reverse && pingPong && name && check &&
						clip && frame && tabs && undo,
					QStringLiteral("frame range controls exist")))
			return EXIT_FAILURE;
		for (int i = 0; i < tabs->count(); ++i)
			if (tabs->widget(i)->isAncestorOf(first))
				tabs->setCurrentIndex(i);
		auto *timeline = editor.findChild<QAbstractSlider *>("meshFrameTimeline");
		auto *onion = editor.findChild<QCheckBox *>("meshOnionSkin");
		auto *targetFps = editor.findChild<QDoubleSpinBox *>("meshResampleTargetFps");
		auto *targetFrames = editor.findChild<QSpinBox *>("meshResampleTargetFrames");
		auto *resample = editor.findChild<QPushButton *>("resampleMeshClip");
		auto *closeLoop = editor.findChild<QPushButton *>("closeMeshClipLoop");
		auto *rangeTransform = editor.findChild<QPushButton *>("transformMeshFrameRange");
		auto *blendSource = editor.findChild<QSpinBox *>("meshBlendSourceFrame");
		auto *blendPose = editor.findChild<QPushButton *>("blendMeshFramePose");
		auto *relative = editor.findChild<QPushButton *>("applyMeshRelativePose");
		auto *importButton = editor.findChild<QPushButton *>("importMeshFrames");
		auto *exportObj = editor.findChild<QPushButton *>("exportMeshFramesObj");
		auto *follow = editor.findChild<QPushButton *>("followMeshTagSelection");
		auto *followResult = editor.findChild<QLabel *>("meshTagFollowResult");
		auto *tagName = editor.findChild<QLineEdit *>("meshTagName");
		auto *preview = editor.findChild<ModelViewport *>("meshPreview");
		auto *panel = static_cast<ModelEditorAnimationPanel *>(editor.findChild<QObject *>("meshAnimationPanel"));
		if (!expect(timeline && onion && targetFps && targetFrames && resample && closeLoop && rangeTransform && blendSource && blendPose &&
						relative && importButton && exportObj && follow && followResult && tagName && preview && panel,
					QStringLiteral("animation panel controls exist")))
			return EXIT_FAILURE;
		auto *followAnchor = editor.findChild<QCheckBox *>("meshTagFollowAnchor");
		auto *refitButton = editor.findChild<QPushButton *>("refitMeshAnchoredTags");
		if (!expect(followAnchor && refitButton, QStringLiteral("anchor controls exist")))
			return EXIT_FAILURE;
		const QList<QWidget *> controls{first,	last,	  destination, move,		   duplicate,  remove,	   reverse,		 pingPong,
										start,	renameClips, name,	   onion,		   targetFps,  resample,   closeLoop,	 rangeTransform,
										blendSource, blendPose, relative, importButton, exportObj, follow, followAnchor, refitButton};
		for (auto *control : controls)
		{
			auto *accessible = QAccessible::queryAccessibleInterface(control);
			expect(accessible && !accessible->text(QAccessible::Name).isEmpty() && !control->accessibleDescription().isEmpty() &&
					   control->focusPolicy() != Qt::NoFocus,
				   QStringLiteral("%1 exposes an accessible name, description and focus").arg(control->objectName()));
		}
		expect(!check->text().isEmpty() && !check->accessibleName().isEmpty(), QStringLiteral("the native clip check is shown"));

		// Move through the panel matches the document service and is one undo step.
		first->setValue(1);
		last->setValue(2);
		destination->setValue(3);
		expect(move->isEnabled(), QStringLiteral("a valid move is enabled"));
		move->click();
		expect(bytes(editor.document().mesh()) == bytes(serviceResult(original, ModelEditKind::MoveFrames, 1, 2, 3)) && frame->currentIndex() == 3,
			   QStringLiteral("Move Frames applies the shared edit and shows the moved frames"));
		undo->trigger();
		expect(bytes(editor.document().mesh()) == bytes(original), QStringLiteral("undo restores the frames"));
		first->setValue(2);
		last->setValue(2);
		expect(!reverse->isEnabled() && reverse->toolTip().contains("two frames") && !pingPong->isEnabled() && duplicate->isEnabled(),
			   QStringLiteral("refused operations are disabled and say why"));
		first->setValue(0);
		last->setValue(3);
		pingPong->click();
		expect(editor.document().mesh().frames.size() == 8 && frame->currentIndex() == 4, QStringLiteral("ping-pong adds the return frames"));
		undo->trigger();
		remove->click();
		expect(editor.document().mesh().frames.size() == 2 && editor.document().mesh().animations.size() == 2,
			   QStringLiteral("Delete Range removes frames and empty clips"));
		undo->trigger();
		expect(bytes(editor.document().mesh()) == bytes(original), QStringLiteral("undo restores deleted frames"));

		// Naming: every clip overlaps "all", so naming all is refused; one clip works.
		clip->setCurrentIndex(clip->findData(-1));
		name->click();
		expect(bytes(editor.document().mesh()) == bytes(original), QStringLiteral("overlapping clips are not named"));
		clip->setCurrentIndex(clip->findData(1));
		start->setValue(0);
		renameClips->setChecked(false);
		name->click();
		expect(editor.document().mesh().frames[3].name == "run0" && editor.document().mesh().frames[0].name == "walk1",
			   QStringLiteral("Name Frames From Clips names the chosen clip from the first number"));
		undo->trigger();

		// The timeline: a slider with a per-frame description; time runs left
		// to right in every layout direction; Shift extends the range.
		{
			auto *accessible = QAccessible::queryAccessibleInterface(timeline);
			expect(accessible && accessible->role() == QAccessible::Slider && !accessible->text(QAccessible::Name).isEmpty() &&
					   timeline->focusPolicy() == Qt::StrongFocus && timeline->maximum() == 5,
				   QStringLiteral("the timeline is an accessible slider over every frame"));
			frame->setCurrentIndex(1);
			app.processEvents();
			QKeyEvent right(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier);
			QApplication::sendEvent(timeline, &right);
			expect(frame->currentIndex() == 2 && timeline->value() == 2 && timeline->accessibleDescription().contains("walk3"),
				   QStringLiteral("Right steps to the next frame and describes it (scenario %1)").arg(scenario));
			QKeyEvent extend(QEvent::KeyPress, Qt::Key_Right, Qt::ShiftModifier);
			QApplication::sendEvent(timeline, &extend);
			QApplication::sendEvent(timeline, &extend);
			expect(frame->currentIndex() == 4 && first->value() == 2 && last->value() == 4, QStringLiteral("Shift extends the frame range"));
			QKeyEvent home(QEvent::KeyPress, Qt::Key_Home, Qt::NoModifier);
			QApplication::sendEvent(timeline, &home);
			expect(frame->currentIndex() == 0, QStringLiteral("Home goes to the first frame"));
		}
		// Onion skin draws ghosted neighbours into the 3D view.
		if (scenario != 1)
		{
			frame->setCurrentIndex(3);
			tests::settleModelViewport(*preview);
			QImage plain(preview->size(), QImage::Format_ARGB32_Premultiplied);
			preview->render(&plain);
			onion->setChecked(true);
			tests::settleModelViewport(*preview);
			QImage ghosted(preview->size(), QImage::Format_ARGB32_Premultiplied);
			preview->render(&ghosted);
			expect(panel->onionFrames() == QVector<int>({2, 4}) && plain != ghosted, QStringLiteral("the onion skin draws frames 2 and 4"));
			if (const auto folder = qEnvironmentVariable("VIBESTUDIO_MODELLER_EVIDENCE"); !folder.isEmpty())
				ghosted.save(QDir(folder).filePath(QString("mesh-onion-%1.png").arg(scenario)));
			onion->setChecked(false);
		}
		// Resample and close a vertex clip through the panel.
		clip->setCurrentIndex(clip->findData(1));
		targetFps->setValue(0);
		targetFrames->setValue(5);
		resample->click();
		expect(editor.document().mesh().frames.size() == 8 && editor.document().mesh().animations[1].frameCount == 5,
			   QStringLiteral("Resample Clip gives the clip five frames"));
		undo->trigger();
		closeLoop->click();
		expect(editor.document().mesh().surfaces[0].frames[5].positions[0].z == original.surfaces[0].frames[3].positions[0].z,
			   QStringLiteral("Close Clip Loop replaces the last frame with the first pose"));
		undo->trigger();
		targetFps->setValue(10);
		// Range sculpt on selected vertices.
		ModelSelection vertices;
		vertices.surface = 0;
		vertices.vertices = {0, 1};
		editor.tools()->applySelection(vertices);
		first->setValue(0);
		last->setValue(1);
		blendSource->setValue(5);
		expect(blendPose->isEnabled(), QStringLiteral("blending is enabled with a selection"));
		blendPose->click();
		expect(editor.document().mesh().surfaces[0].frames[0].positions[0].z == original.surfaces[0].frames[5].positions[0].z &&
				   editor.document().mesh().surfaces[0].frames[0].positions[2].z == original.surfaces[0].frames[0].positions[2].z,
			   QStringLiteral("Blend Selection From Frame moves only the selection"));
		undo->trigger();
		// Follow Selection fits a new tag to the selected vertices.
		ModelSelection plate;
		plate.surface = 0;
		plate.vertices = {0, 1, 2};
		editor.tools()->applySelection(plate);
		tagName->setText("tag_weapon");
		expect(follow->isEnabled(), QStringLiteral("Follow Selection is enabled with components and a name"));
		follow->click();
		expect(editor.document().mesh().tags.size() == 12 && !followResult->text().isEmpty(),
			   QStringLiteral("Follow Selection adds a tag in every frame and reports the fit"));
		undo->trigger();
		editor.tools()->applySelection(ModelSelection{});
		// Import a frame sequence written by the panel's own export.
		first->setValue(0);
		last->setValue(1);
		const auto pattern = QDir(temporary.path()).filePath(QString("ui%1/f_##.obj").arg(scenario));
		expect(panel->exportFramesObj(pattern, false, &error), QStringLiteral("export the range as OBJ: ") + error);
		const QStringList sequence{QDir(temporary.path()).filePath(QString("ui%1/f_01.obj").arg(scenario)),
								   QDir(temporary.path()).filePath(QString("ui%1/f_00.obj").arg(scenario))};
		expect(panel->importFrames(sequence, &error) && editor.document().mesh().frames.size() == 8 &&
				   editor.document().mesh().surfaces[0].frames[6].positions[0].z == original.surfaces[0].frames[0].positions[0].z,
			   QStringLiteral("Import Frames appends the sequence in natural order: ") + error);
		undo->trigger();
		expect(bytes(editor.document().mesh()) == bytes(original), QStringLiteral("every panel edit undoes in one step"));
		// Every new control fits the inspector without horizontal scrolling.
		QScrollArea *inspector = nullptr;
		for (auto *area : editor.findChildren<QScrollArea *>())
			if (area->isAncestorOf(first))
				inspector = area;
		if (expect(inspector, QStringLiteral("the animation inspector scrolls")))
		{
			expect(inspector->horizontalScrollBar()->maximum() == 0, QStringLiteral("the frame range group fits without horizontal overflow"));
			for (auto *control : controls)
			{
				const auto centre = control->mapTo(inspector->widget(), control->rect().center());
				inspector->verticalScrollBar()->setValue(centre.y() - inspector->viewport()->height() / 2);
				app.processEvents();
				const auto bounds = QRect(control->mapTo(inspector->viewport(), QPoint()), control->size());
				expect(bounds.left() >= 0 && bounds.right() < inspector->viewport()->width() && bounds.top() >= 0 &&
						   bounds.bottom() < inspector->viewport()->height(),
					   QStringLiteral("%1 is reachable inside the inspector (scenario %2)").arg(control->objectName()).arg(scenario));
			}
		}
		const auto evidence = qEnvironmentVariable("VIBESTUDIO_MODELLER_EVIDENCE");
		if (!evidence.isEmpty() && inspector)
		{
			inspector->ensureWidgetVisible(move);
			app.processEvents();
			QImage image(editor.size() * editor.devicePixelRatioF(), QImage::Format_ARGB32_Premultiplied);
			image.setDevicePixelRatio(editor.devicePixelRatioF());
			image.fill(Qt::transparent);
			editor.render(&image);
			image.save(QDir(evidence).filePath(QString("mesh-frame-range-%1-%2x.png").arg(scenario).arg(editor.devicePixelRatioF())));
		}
		if (scenario == 0)
		{
			// The onion skin toggles from its registered command and draws in
			// every pane of the four views.
			auto *toggle = editor.tools()->action(QStringLiteral("meshToggleOnionSkin"));
			expect(toggle && !onion->isChecked(), QStringLiteral("the onion skin command is registered"));
			if (toggle)
				toggle->trigger();
			expect(onion->isChecked(), QStringLiteral("Toggle Onion Skin turns the onion skin on"));
			onion->setChecked(false);
			editor.setViewLayout(ModelViewLayout::FourViews);
			frame->setCurrentIndex(3);
			app.processEvents();
			ModelViewport *mirror = nullptr;
			for (auto *pane : editor.panes())
				if (pane != preview)
					mirror = pane;
			if (expect(mirror, QStringLiteral("four views show mirror panes")))
			{
				editor.syncPanes();
				app.processEvents();
				tests::settleModelViewport(*mirror);
				QImage plainMirror(mirror->size(), QImage::Format_ARGB32_Premultiplied);
				mirror->render(&plainMirror);
				onion->setChecked(true);
				app.processEvents();
				tests::settleModelViewport(*mirror);
				QImage ghostMirror(mirror->size(), QImage::Format_ARGB32_Premultiplied);
				mirror->render(&ghostMirror);
				expect(plainMirror != ghostMirror, QStringLiteral("a mirror pane draws the onion skin too"));
				onion->setChecked(false);
			}
			editor.setViewLayout(ModelViewLayout::Single);
			// Geometry > Over the frame range turns a move into a range transform.
			auto *scope = editor.findChild<QCheckBox *>("meshFrameRangeScope");
			if (expect(scope && !scope->accessibleName().isEmpty(), QStringLiteral("the frame range scope exists beside Apply to")))
			{
				ModelSelection vertices;
				vertices.surface = 0;
				vertices.vertices = {0, 1};
				editor.tools()->applySelection(vertices);
				first->setValue(1);
				last->setValue(2);
				scope->setChecked(true);
				ModelEdit move;
				move.kind = ModelEditKind::Transform;
				move.selection = vertices;
				move.translation = {0, 0, 6};
				move.pivotFrame = 1;
				ModelEdit ranged = move;
				ranged.kind = ModelEditKind::TransformFrameRange;
				ranged.poseRange.first = 1;
				ranged.poseRange.last = 2;
				ModelDocument expected;
				expected.setMesh(editor.document().mesh(), &error);
				expected.edit(ranged, &error);
				expect(editor.applyEdit(move, &error) && bytes(editor.document().mesh()) == bytes(expected.mesh()),
					   QStringLiteral("a move with the range scope edits frames 1 and 2: ") + error);
				undo->trigger();
				// The live preview shows the displayed frame's falloff-weighted
				// share, in a handle drag and in a modal move.
				auto *falloff = editor.findChild<QComboBox *>("meshRangeFalloff");
				if (expect(falloff, QStringLiteral("the range falloff exists")))
				{
					falloff->setCurrentIndex(int(ModelFrameFalloff::RampIn));
					frame->setCurrentIndex(1);
					app.processEvents();
					const double half = preview->editPreviewWeight();
					frame->setCurrentIndex(2);
					app.processEvents();
					const double full = preview->editPreviewWeight();
					frame->setCurrentIndex(0);
					app.processEvents();
					expect(std::abs(half - 0.5) < 1e-9 && full == 1 && preview->editPreviewWeight() == 0,
						   QStringLiteral("a drag previews each frame's ramp-in weight, and none outside the range"));
					frame->setCurrentIndex(1);
					app.processEvents();
					const auto plain = editor.document().mesh();
					expect(editor.tools()->beginModal(ModelModalOperator::Move, QRectF(preview->rect()).center()),
						   QStringLiteral("start a modal move over the range"));
					editor.tools()->constrainModal(2, false);
					editor.tools()->typeModal(QStringLiteral("8"));
					const auto *shown = editor.tools()->modalPreview();
					const auto lift = [&](const ModelMesh &mesh, int f)
					{ return mesh.surfaces[0].frames[f].positions[0].z - plain.surfaces[0].frames[f].positions[0].z; };
					const bool weighted = editor.tools()->modalPreviewValid() && shown && lift(*shown, 0) == 0 && lift(*shown, 2) > 0 &&
										  std::abs(lift(*shown, 1) - lift(*shown, 2) / 2) < 1e-4 && lift(*shown, 3) == 0;
					if (!expect(weighted && editor.tools()->modalText().contains(QStringLiteral("50")),
								QStringLiteral("the modal move previews half the move on frame 1 of a ramp-in range")))
						std::cerr << qPrintable(editor.tools()->modalText()) << " frame " << frame->currentIndex() << " lifts "
								  << (shown ? lift(*shown, 0) : -1) << ' ' << (shown ? lift(*shown, 1) : -1) << ' '
								  << (shown ? lift(*shown, 2) : -1) << ' ' << (shown ? lift(*shown, 3) : -1) << '\n';
					const auto previewed = shown ? bytes(*shown) : QByteArray();
					expect(editor.tools()->finishModal(true, &error) && bytes(editor.document().mesh()) == previewed && frame->currentIndex() == 1,
						   QStringLiteral("confirming applies the previewed range transform and keeps the displayed frame: ") + error);
					undo->trigger();
					falloff->setCurrentIndex(0);
				}
				scope->setChecked(false);
				app.processEvents();
				expect(preview->editPreviewWeight() == 1, QStringLiteral("without the range scope a drag previews the whole move"));
				editor.tools()->applySelection(ModelSelection{});
			}
			// Refit Anchored Tags after Follow Selection remembered the components.
			auto *refit = editor.findChild<QPushButton *>("refitMeshAnchoredTags");
			auto *anchorSummary = editor.findChild<QLabel *>("meshTagAnchorSummary");
			ModelSelection plate;
			plate.surface = 0;
			plate.vertices = {0, 1, 2};
			editor.tools()->applySelection(plate);
			tagName->setText("tag_weapon");
			follow->click();
			expect(refit && anchorSummary && editor.document().mesh().tagAnchors.size() == 1 && refit->isEnabled() &&
					   anchorSummary->text().contains("tag_weapon"),
				   QStringLiteral("Follow Selection anchors the tag and enables Refit Anchored Tags"));
			if (refit)
				refit->click();
			expect(editor.document().canUndo() && !followResult->text().isEmpty(), QStringLiteral("Refit Anchored Tags reports its result"));
			editor.tools()->applySelection(ModelSelection{});
			expect(editor.setMesh(original, &error), QStringLiteral("reset after anchoring"));
			// OBJ import settings.
			ModelObjImportDialog settings;
			ModelObjImportOptions chosen;
			chosen.up = ModelObjUpAxis::Y;
			chosen.scale = 2;
			chosen.materials = ModelObjMaterials::Ignore;
			chosen.refuseLooseData = true;
			settings.setOptions(chosen);
			bool named = true;
			for (const char *id : {"objUpAxis", "objScale", "objMaterials", "objLooseData"})
			{
				auto *field = settings.findChild<QWidget *>(QString::fromLatin1(id));
				named = named && field && !field->accessibleName().isEmpty() && field->focusPolicy() != Qt::NoFocus;
			}
			expect(named && settings.options() == chosen, QStringLiteral("the OBJ settings dialog round-trips its options accessibly"));
			const auto objPath = QDir(temporary.path()).filePath("yup.obj");
			QFile objFile(objPath);
			if (objFile.open(QIODevice::WriteOnly))
			{
				objFile.write("v 0 0 0\nv 1 0 0\nv 1 2 0\nv 0 2 0\nl 1 3\nf 1 2 3 4\n");
				objFile.close();
			}
			ModelObjImportOptions yUp;
			yUp.up = ModelObjUpAxis::Y;
			yUp.scale = 2;
			bool converted = editor.openSource(objPath, yUp, &error);
			if (converted)
			{
				converted = false;
				for (const auto &p : editor.document().mesh().surfaces[0].frames[0].positions)
					converted = converted || (std::abs(p.x - 2) < 1e-5 && std::abs(p.y) < 1e-5 && std::abs(p.z - 4) < 1e-5);
			}
			expect(converted, QStringLiteral("Open with OBJ settings converts Y up and scales: ") + error);
			expect(editor.setMesh(original, &error), QStringLiteral("reset after the OBJ"));
			// Edit Skeletal Animation offers resampling and loop closing.
			const auto skinnedModel = tests::skinFixture();
			ModelSkeletalDialog skeletal(skinnedModel, {}, QString(), 1);
			auto *operation = skeletal.findChild<QComboBox *>("skeletalOperation");
			auto *resampleFps = skeletal.findChild<QDoubleSpinBox *>("skeletalResampleFps");
			if (expect(operation && resampleFps, QStringLiteral("the skeletal dialog has resampling controls")))
			{
				operation->setCurrentIndex(operation->findData(int(ModelEditKind::ResampleSkeletalClip)));
				resampleFps->setValue(20);
				const auto resampleEdit = skeletal.edit();
				expect(resampleEdit.kind == ModelEditKind::ResampleSkeletalClip && resampleEdit.resample.clip == 0 &&
						   resampleEdit.resample.targetFps == 20 && defaultModelSkeletalOptions(resampleEdit.skeletal),
					   QStringLiteral("Resample clip builds a skeletal resample edit"));
				operation->setCurrentIndex(operation->findData(int(ModelEditKind::CloseSkeletalLoop)));
				const auto loopEdit = skeletal.edit();
				expect(loopEdit.kind == ModelEditKind::CloseSkeletalLoop && loopEdit.resample.loopClose == ModelLoopClose::ReplaceLast,
					   QStringLiteral("Close clip loop builds a loop edit"));
				expect(editor.setMesh(skinnedModel, &error) && editor.applyEdit(resampleEdit, &error) &&
						   editor.document().mesh().skeleton.clips[0].frames.size() == 3,
					   QStringLiteral("the dialog's resample applies through the editor: ") + error);
				expect(editor.setMesh(original, &error), QStringLiteral("reset after the skeletal resample"));
			}
			// The timeline keeps up with the largest model: 1,024 frames.
			auto large = tests::frameRangeFixture(modelDocumentMaxFrames);
			large.collisionBoxes.clear();
			expect(editor.setMesh(large, &error), QStringLiteral("open a 1024-frame model: ") + error);
			app.processEvents();
			QElapsedTimer clock;
			clock.start();
			for (int step = 0; step < 20; ++step)
			{
				frame->setCurrentIndex(step * 50);
				timeline->repaint();
			}
			const auto elapsed = clock.elapsed();
			expect(timeline->maximum() == modelDocumentMaxFrames - 1 && elapsed < 20000,
				   QStringLiteral("twenty frame changes on a 1024-frame timeline took %1 ms").arg(elapsed));
			std::cerr << "timeline: 20 frame changes with 1024 frames in " << elapsed << " ms\n";
			// Frames a skinned skeleton drives are gated with the core's reason.
			auto skinned = tests::skinFixture();
			expect(editor.setMesh(skinned, &error), QStringLiteral("open the skinned fixture: ") + error);
			app.processEvents();
			first->setValue(1);
			last->setValue(2);
			expect(!reverse->isEnabled() && reverse->toolTip().contains("skeletal clip"),
				   QStringLiteral("reversing skeletal clip frames is disabled with the reason"));
		}
		expect(editor.setMesh(original, &error), QStringLiteral("retire the test recovery"));
		if (scenario > 0)
			app.removeTranslator(&expansion);
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
