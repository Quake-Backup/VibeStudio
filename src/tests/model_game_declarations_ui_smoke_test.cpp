#include "app/model_declarations_dialog.h"
#include "app/model_events_dialog.h"
#include "app/studio_theme.h"
#include "core/studio_settings.h"
#include "tests/model_pose_clipboard_test_helpers.h"
#include "tests/render_test_support.h"

#include <QAccessible>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>
#include <iostream>

using namespace vibestudio;
using namespace vibestudio::tests;
namespace
{
int checks = 0, failures = 0;
bool expect(bool ok, const char *name)
{
	++checks;
	if (!ok)
	{
		++failures;
		std::cerr << "FAIL: " << name << '\n';
	}
	return ok;
}
bool accessibleControl(QWidget *w)
{
	return w && QAccessible::queryAccessibleInterface(w) && (w->focusPolicy() & Qt::TabFocus) && !w->accessibleName().isEmpty() &&
		   !w->accessibleDescription().isEmpty();
}
} // namespace

int main(int argc, char **argv)
{
	qputenv("QT_QPA_PLATFORM", "offscreen");
	QApplication app(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
		return 1;
	QTemporaryDir tmp(QDir(root).filePath("declarations-ui-XXXXXX"));
	if (!tmp.isValid())
		return 1;
	StudioSettings::setOverrideFilePath(QDir(tmp.path()).filePath("settings.ini"));
	if (const int unavailable = test_support::exitCodeWithoutRenderer("game declarations UI"); unavailable >= 0)
		return unavailable;
	QString error;
	const auto mesh = symmetricRigFixture();
	for (int scenario = 0; scenario < 2; ++scenario)
	{
		app.setLayoutDirection(scenario ? Qt::RightToLeft : Qt::LeftToRight);
		applyStudioTheme(app, studioThemeTokens(scenario ? StudioTheme::HighContrastDark : StudioTheme::Dark, UiDensity::Standard, scenario ? 200 : 100));

		// Animation Events.
		const int frame = mesh.skeleton.bakedClipForFrame.indexOf(0) + 2;
		ModelEventsDialog events(mesh, frame);
		events.show();
		QCoreApplication::processEvents();
		for (const char *id : {"eventsClip", "eventsTable", "eventsAdd", "eventsRemove"})
			expect(accessibleControl(events.findChild<QWidget *>(id)), "event control has an accessible name, description and keyboard focus");
		auto *table = events.findChild<QTableWidget *>("eventsTable");
		auto *apply = events.findChild<QPushButton *>("eventsApply");
		events.addEvent();
		expect(table->rowCount() == 1 && table->item(0, 0)->text() == "2" && !apply->isEnabled(), "a new event starts at the shown frame and needs a command");
		table->item(0, 1)->setText("sound_body snd_footstep");
		expect(apply->isEnabled(), "a complete event can be applied");
		table->item(0, 0)->setText("9");
		expect(!apply->isEnabled(), "an event past the clip cannot be applied");
		table->item(0, 0)->setText("2");
		const auto edit = events.edit();
		ModelMesh candidate = mesh;
		expect(edit.kind == ModelEditKind::EditAnimationEvents && edit.animation.events.events.size() == 1 &&
				   applyModelEdit(&candidate, edit, nullptr, &error) && candidate.skeleton.clips[0].events[0].command == "sound_body",
			   "the dialog's edit replaces the clip's events");
		events.reject();

		// Game Declarations.
		ModelDeclarationsDialog declarations(candidate, "test.mesh.json");
		declarations.show();
		QCoreApplication::processEvents();
		for (const char *id : {"declarationsTarget", "declarationsName", "declarationsPath", "declarationsPreview", "declarationsCopy",
							   "declarationsSave", "declarationsModelSet"})
			expect(accessibleControl(declarations.findChild<QWidget *>(id)), "declaration control has an accessible name, description and keyboard focus");
		expect(declarations.text().contains("frame 3\tsound_body snd_footstep") && declarations.text().contains("model test {"),
			   "the Doom 3 declaration is previewed with the clip's events");
		declarations.findChild<QPushButton *>("declarationsCopy")->click();
		expect(QGuiApplication::clipboard()->text() == declarations.text(), "Copy puts the declaration on the clipboard");
		const auto folder = QDir(tmp.path()).filePath(QStringLiteral("set%1").arg(scenario));
		expect(declarations.exportModelSet(folder) && QFile::exists(QDir(folder).filePath("def/test.def")) &&
				   QFile::exists(QDir(folder).filePath("models/md5/test/walk.md5anim")),
			   "Export Model Set writes the mesh, every clip and the declaration");
		expect(!declarations.exportModelSet(folder), "an existing set is not overwritten");
		declarations.setTarget(3);
		declarations.findChild<QLineEdit *>("declarationsName")->setText("TestModel");
		expect(declarations.text().startsWith("// Generated") && declarations.text().contains("Model TestModel") &&
				   !declarations.findChild<QPushButton *>("declarationsModelSet")->isEnabled(),
			   "the GZDoom MODELDEF previews; the model set is Doom 3 only");
		declarations.findChild<QLineEdit *>("declarationsName")->setText("1 bad");
		expect(declarations.text().isEmpty() && !declarations.findChild<QPushButton *>("declarationsCopy")->isEnabled(),
			   "an invalid actor name explains itself instead of generating");
		declarations.setTarget(1);
		declarations.findChild<QLineEdit *>("declarationsPath")->setText("progs/test");
		const auto qc = QDir(tmp.path()).filePath(QStringLiteral("frames%1.qc").arg(scenario));
		expect(declarations.text().contains("$cd progs/test") && declarations.saveText(qc) && QFile::exists(qc), "Save As writes the QuakeC macros");
		declarations.reject();
	}
	std::cout << checks << " game declaration UI checks; " << failures << " failures\n";
	return failures ? 1 : 0;
}
