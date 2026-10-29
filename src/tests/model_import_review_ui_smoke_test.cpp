// The Mesh Editor's decode issue workflow (FMT-02): the Open dialog lists
// every decodable model format, an omission opens the import review (by
// keyboard, with accessible names, at 100% and 200% text, left to right and
// right to left), cancelling keeps the current document, accepting imports
// the model and Health lists the record.
#include "app/model_editor_dialog.h"
#include "app/model_import_review.h"
#include "app/studio_theme.h"
#include "core/studio_settings.h"
#include "tests/model_decode_issue_fixtures.h"
#include "tests/render_test_support.h"

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFont>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>

#include <algorithm>
#include <cstdlib>
#include <iostream>

using namespace vibestudio;
namespace
{
bool expect(bool value, const char *message)
{
	if (!value)
		std::cerr << "FAIL: " << message << '\n';
	return value;
}
bool write(const QString &path, const QByteArray &bytes)
{
	QFile f(path);
	return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
}
void key(QWidget *target, int code)
{
	QKeyEvent press(QEvent::KeyPress, code, Qt::NoModifier);
	QKeyEvent release(QEvent::KeyRelease, code, Qt::NoModifier);
	QCoreApplication::sendEvent(target, &press);
	QCoreApplication::sendEvent(target, &release);
}
struct Review
{
	bool seen = false;
	bool accessible = false;
	bool cancelDefault = false;
	int items = 0;
	QString firstItem;
};
// Answers the next import review: Escape cancels; otherwise Tab to the accept
// button and press Space, as a keyboard user would.
void answerReview(Review *review, bool accept)
{
	// The import runs on the document worker first, so poll until the
	// review is the active modal dialog.
	auto *poll = new QTimer;
	poll->setInterval(20);
	auto *tries = new int(0);
	QObject::connect(poll, &QTimer::timeout, [poll, tries, review, accept] {
		auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
		if (!dialog || dialog->objectName() != QStringLiteral("modelImportReview"))
		{
			if (++*tries > 1500)
			{
				poll->stop();
				poll->deleteLater();
				delete tries;
			}
			return;
		}
		poll->stop();
		poll->deleteLater();
		delete tries;
		review->seen = true;
		auto *list = dialog->findChild<QListWidget *>(QStringLiteral("modelImportReviewIssues"));
		auto *acceptButton = dialog->findChild<QPushButton *>(QStringLiteral("acceptImportOmissions"));
		auto *cancel = dialog->findChild<QPushButton *>(QStringLiteral("cancelImportReview"));
		review->accessible = list && acceptButton && cancel && !list->accessibleName().isEmpty() && !dialog->accessibleName().isEmpty() &&
							 !acceptButton->accessibleDescription().isEmpty() && acceptButton->isEnabled() &&
							 list->focusPolicy() != Qt::NoFocus && acceptButton->focusPolicy() != Qt::NoFocus;
		review->cancelDefault = cancel && cancel->isDefault() && acceptButton && !acceptButton->isDefault();
		review->items = list ? list->count() : 0;
		review->firstItem = list && list->count() ? list->item(0)->text() : QString();
		if (!accept)
		{
			key(dialog, Qt::Key_Escape);
			return;
		}
		acceptButton->setFocus(Qt::TabFocusReason);
		key(acceptButton, Qt::Key_Space);
	});
	poll->start();
}
} // namespace

int main(int argc, char **argv)
{
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc, argv);
	if (const int skip = vibestudio::test_support::exitCodeWithoutRenderer("model-import-review-ui-smoke"); skip >= 0)
		return skip;
#ifdef Q_OS_WIN
	app.setFont(QFont("Segoe UI", 10));
#endif
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
		return EXIT_FAILURE;
	QTemporaryDir temporary(QDir(root).filePath("import-review-ui-XXXXXX"));
	if (!temporary.isValid())
		return EXIT_FAILURE;
	const auto path = [&](const char *name) { return QDir(temporary.path()).filePath(QString::fromLatin1(name)); };
	StudioSettings::setOverrideFilePath(path("settings.ini"));
	bool ok = true;
	QString error;
	ok &= expect(write(path("layered.lwo"), tests::layeredLightWave(true)) && write(path("plain.lwo"), tests::layeredLightWave(false)),
				 "write the LightWave fixtures");

	// Every model format with geometry, and none of the animation-only ones.
	const auto patterns = modelEditorOpenPatterns().split(QLatin1Char(' '));
	for (const char *pattern : {"*.mesh.json", "*.obj", "*.mdl", "*.md2", "*.md3", "*.md5mesh", "*.iqm", "*.ase", "*.lwo", "*.mds", "*.glm", "*.kvx", "*.fm"})
		ok &= expect(patterns.contains(QString::fromLatin1(pattern)), "the Open filter lists every decodable model format");
	ok &= expect(!patterns.contains(QStringLiteral("*.md5anim")) && !patterns.contains(QStringLiteral("*.gla")) && !patterns.contains(QStringLiteral("*.mdx")),
				 "animation-only formats stay out of the Open filter");

	for (int scenario = 0; scenario < 2; ++scenario)
	{
		applyStudioTheme(app, studioThemeTokens(scenario ? StudioTheme::HighContrastDark : StudioTheme::Dark, UiDensity::Standard, scenario ? 200 : 100));
		ModelEditorDialog editor;
		editor.setLayoutDirection(scenario ? Qt::RightToLeft : Qt::LeftToRight);
		editor.resize(scenario ? 1700 : 1450, scenario ? 1150 : 900);
		editor.show();

		// The Open dialog the editor shows carries the same filter.
		QStringList filters;
		QTimer::singleShot(0, [&filters] {
			if (auto *picker = qobject_cast<QFileDialog *>(QApplication::activeModalWidget()))
			{
				filters = picker->nameFilters();
				picker->reject();
			}
		});
		if (auto *open = editor.findChild<QAction *>(QStringLiteral("openMesh")))
			open->trigger();
		ok &= expect(!filters.isEmpty() && filters.first().contains(QStringLiteral("*.lwo")) && filters.first().contains(QStringLiteral("*.md5mesh")) &&
						 filters.first().contains(QStringLiteral("*.ase")) && filters.first().contains(QStringLiteral("*.iqm")),
					 "Open or Import Model offers LightWave, MD5, ASE and IQM files");

		ok &= expect(editor.openSource(path("plain.lwo"), &error), "a clean LightWave object opens without a review");
		const auto revision = editor.document().revisionFingerprint();
		Review cancelled;
		answerReview(&cancelled, false);
		ok &= expect(!editor.openSource(path("layered.lwo"), &error) && cancelled.seen, "an omission opens the import review");
		ok &= expect(cancelled.accessible && cancelled.cancelDefault && cancelled.items >= 1 && cancelled.firstItem.contains(QStringLiteral("Omission")),
					 "the review lists issues by severity with accessible, keyboard-reachable controls and Cancel as default");
		ok &= expect(editor.document().revisionFingerprint() == revision && !error.isEmpty(), "cancelling the review keeps the current document");

		Review accepted;
		answerReview(&accepted, true);
		ok &= expect(editor.openSource(path("layered.lwo"), &error) && accepted.seen, "accepting the omissions imports the model");
		const auto &issues = editor.document().mesh().issues;
		const bool recorded = std::any_of(issues.cbegin(), issues.cend(), [](const ModelDecodeIssue &issue) {
			return issue.id == QStringLiteral("lwo-extra-layers") && issue.accepted;
		});
		ok &= expect(recorded, "the document records the accepted omission");
		auto *health = editor.findChild<QLabel *>(QStringLiteral("meshImportIssues"));
		ok &= expect(health && !health->accessibleName().isEmpty() &&
						 health->focusPolicy() != Qt::NoFocus && health->text().contains(QStringLiteral("LightWave")),
					 "Health lists the import record, focusable and named");
		editor.close();
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
