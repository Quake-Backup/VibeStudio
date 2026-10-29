#include "app/model_editor_dialog.h"
#include "app/model_editor_frames.h"
#include "app/model_editor_tools.h"
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
		const QList<QWidget *> controls{first,	last,	  destination, move,		   duplicate,  remove,	   reverse,		 pingPong,
										start,	renameClips, name,	   onion,		   targetFps,  resample,   closeLoop,	 rangeTransform,
										blendSource, blendPose, relative, importButton, exportObj, follow};
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
