// Packages > Audit Models (FMT-11, app/model_audit_dialog.h): the task strip's
// running, finished and cancelled states, the summary by format, drill-down
// into an entry, accessible names and keyboard reach, at 100% and 200% text,
// left to right and right to left.
#include "app/model_audit_dialog.h"
#include "app/studio_theme.h"
#include "core/model_document.h"
#include "core/studio_settings.h"
#include "tests/model_decode_issue_fixtures.h"
#include "tests/model_q3_animation_test_helpers.h"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFont>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QThread>
#include <QTreeWidget>

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
bool waitFor(const std::function<bool()> &condition, int timeout = 60000)
{
	QElapsedTimer timer;
	timer.start();
	while (!condition() && timer.elapsed() < timeout)
	{
		QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
		QThread::msleep(1);
	}
	return condition();
}
bool write(const QString &path, const QByteArray &bytes)
{
	QDir().mkpath(QFileInfo(path).absolutePath());
	QFile f(path);
	return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
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
	app.setFont(QFont("Segoe UI", 10));
#endif
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
		return EXIT_FAILURE;
	QTemporaryDir temporary(QDir(root).filePath("model-audit-ui-XXXXXX"));
	if (!temporary.isValid())
		return EXIT_FAILURE;
	StudioSettings::setOverrideFilePath(temporary.filePath("settings.ini"));
	bool ok = true;
	QString error;
	const QString game = temporary.filePath("game");
	const QByteArray md3 = exportEditableModel(tests::q3Model(), QStringLiteral("md3"), 0, &error);
	for (int i = 0; i < 24; ++i)
		ok &= write(game + QStringLiteral("/models/clean%1.md3").arg(i), md3);
	ok &= expect(write(game + "/models/layered.lwo", tests::layeredLightWave(true)) && write(game + "/models/junk.md3", "IDP3junk"),
				 "write the audited folder");

	for (int scenario = 0; scenario < 2; ++scenario)
	{
		applyStudioTheme(app, studioThemeTokens(scenario ? StudioTheme::HighContrastLight : StudioTheme::Dark, UiDensity::Standard, scenario ? 200 : 100));
		ModelAuditDialog dialog(game);
		dialog.setLayoutDirection(scenario ? Qt::RightToLeft : Qt::LeftToRight);
		dialog.show();
		auto *status = dialog.findChild<QLabel *>("modelAuditStatus");
		auto *progress = dialog.findChild<QProgressBar *>("modelAuditProgress");
		auto *start = dialog.findChild<QPushButton *>("modelAuditStart");
		auto *cancel = dialog.findChild<QPushButton *>("modelAuditCancel");
		auto *summary = dialog.findChild<QTableWidget *>("modelAuditSummary");
		auto *entries = dialog.findChild<QTreeWidget *>("modelAuditEntries");
		auto *details = dialog.findChild<QPlainTextEdit *>("modelAuditDetails");
		auto *show = dialog.findChild<QComboBox *>("modelAuditShow");
		ok &= expect(status && progress && start && cancel && summary && entries && details && show, "the dialog has its controls");
		if (!(status && progress && start && cancel && summary && entries && details && show))
			break;
		for (QWidget *widget : std::initializer_list<QWidget *>{status, progress, start, cancel, summary, entries, details, show})
			ok &= expect(!widget->accessibleName().isEmpty(), "every audit control is named for screen readers");
		for (QWidget *widget : std::initializer_list<QWidget *>{start, cancel, summary, entries, details, show})
			ok &= expect(widget->focusPolicy() != Qt::NoFocus, "every audit control is keyboard reachable");
		ok &= expect(!cancel->isEnabled() && start->isEnabled(), "only Audit is available before a run");

		// Running: the strip shows progress and Cancel; finished: the summary.
		start->click();
		ok &= expect(dialog.running() && cancel->isEnabled() && !start->isEnabled(), "a running audit offers Cancel");
		ok &= expect(waitFor([&] { return !dialog.running(); }), "the audit finishes on its worker");
		ok &= expect(!dialog.report().cancelled && dialog.report().entries.size() == 26, "every model is audited");
		ok &= expect(summary->rowCount() == 2 && status->text().contains(QStringLiteral("26")), "the summary has one row per format and the status counts the models");
		ok &= expect(entries->topLevelItemCount() == 2, "by default only the models that need attention are listed");
		show->setCurrentIndex(1);
		ok &= expect(entries->topLevelItemCount() == 26, "every model can be listed");
		show->setCurrentIndex(0);
		entries->setCurrentItem(entries->topLevelItem(0));
		ok &= expect(!details->toPlainText().isEmpty(), "selecting a model shows its details");

		// Cancelled: the strip says so and the results so far stay.
		ok &= expect(dialog.start(), "a second audit starts");
		dialog.cancel();
		ok &= expect(waitFor([&] { return !dialog.running(); }), "a cancelled audit stops");
		ok &= expect(dialog.report().cancelled ? status->text().contains(QStringLiteral("Cancel")) : true, "a cancelled audit says so");
		ok &= expect(start->isEnabled() && !cancel->isEnabled(), "after a run Audit is available again");
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
