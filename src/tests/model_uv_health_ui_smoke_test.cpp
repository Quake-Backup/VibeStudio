#include "app/model_editor_dialog.h"
#include "app/model_uv_view.h"
#include "app/studio_theme.h"
#include "core/studio_settings.h"
#include "tests/model_uv_health_test_helpers.h"
#include "tests/model_uv_view_test_helpers.h"
#include "tests/render_test_support.h"

#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QJsonDocument>
#include <QLabel>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTranslator>
#include <iostream>

using namespace vibestudio;
using namespace vibestudio::tests;
namespace
{
int checks = 0, failures = 0;
bool expect(bool value, const char *message)
{
	++checks;
	if (!value)
	{
		++failures;
		std::cerr << "FAIL: " << message << '\n';
	}
	return value;
}
QByteArray source(const ModelMesh &mesh) { return QJsonDocument(editableModelJson(mesh)).toJson(QJsonDocument::Compact); }
class Expansion final : public QTranslator
{
  public:
	bool isEmpty() const override { return false; }
	QString translate(const char *context, const char *text, const char *, int n) const override
	{
		if (QByteArray(context) != "ModelUvHealth" && QByteArray(context) != "VibeStudioModelUvView")
			return {};
		auto value = QString::fromUtf8(text);
		if (n >= 0)
			value.replace(QStringLiteral("%Ln"), QString::number(n));
		return QStringLiteral("[%1%2]").arg(value, QString(value.size() / 2, '~'));
	}
};
void save(QWidget &widget, const QString &name)
{
	const auto root = qEnvironmentVariable("VIBESTUDIO_MODELLER_EVIDENCE");
	if (root.isEmpty())
		return;
	QDir().mkpath(root);
	QImage image(widget.size() * widget.devicePixelRatioF(), QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(widget.devicePixelRatioF());
	image.fill(Qt::transparent);
	widget.render(&image);
	image.save(QDir(root).filePath(QStringLiteral("uv-health-%1-%2x.png").arg(name).arg(widget.devicePixelRatioF())));
}
void showUvTab(ModelEditorDialog &editor, ModelUvView *view)
{
	for (auto *tabs : editor.findChildren<QTabWidget *>())
		for (int index = 0; index < tabs->count(); ++index)
			if (tabs->widget(index)->isAncestorOf(view))
				tabs->setCurrentIndex(index);
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
	QTemporaryDir temp(QDir(root).filePath(QStringLiteral("uv-health-ui-XXXXXX")));
	if (!temp.isValid())
		return 1;
	StudioSettings::setOverrideFilePath(temp.filePath(QStringLiteral("settings.ini")));
	if (const int unavailable = test_support::exitCodeWithoutRenderer("UV health UI"); unavailable >= 0)
		return unavailable;
	const auto folded = foldedStrip();
	QString error;

	// The overlay pass shades only flagged faces, at 1x and 2x.
	for (const double ratio : {1.0, 2.0})
	{
		ModelUvRenderRequest request;
		request.surface = folded.surfaces[0];
		request.surface.frames.clear();
		request.logicalSize = {300, 200};
		request.pixelRatio = ratio;
		request.background = Qt::black;
		request.foreground = Qt::white;
		request.accent = Qt::yellow;
		ModelUvRenderResult plain, shaded;
		expect(renderModelUv(request, &plain, &error), "render without an overlay");
		request.faceOverlay = QVector<quint8>(6, 0);
		request.faceOverlay[0] = ModelUvHealthOverlapping;
		request.overlayMask = ModelUvHealthOverlapping;
		request.overlayColour = QColor(255, 128, 0);
		request.showTile = true;
		expect(renderModelUv(request, &shaded, &error), "render with an overlay");
		const auto inside = shaded.camera.map(QPointF(.25, .1)) * ratio, outside = shaded.camera.map(QPointF(.25, .9)) * ratio;
		expect(shaded.image.pixelColor(inside.toPoint()) != plain.image.pixelColor(inside.toPoint()) &&
				   shaded.image.pixelColor(outside.toPoint()) == plain.image.pixelColor(outside.toPoint()),
			   "the overlay shades the flagged face only");
		request.overlayMask = ModelUvHealthFlipped;
		ModelUvRenderResult other;
		expect(renderModelUv(request, &other, &error) && other.image.pixelColor(inside.toPoint()) == plain.image.pixelColor(inside.toPoint()),
			   "faces outside the overlay mask are not shaded");
	}

	// The view measures health on its worker and names the problem.
	{
		ModelUvView view;
		view.resize(480, 320);
		view.setSource(folded.surfaces[0], {}, {});
		view.setHealthOverlay(ModelUvHealthOverlay::Overlaps, folded.surfaces[0].frames[0].positions, {30, 10});
		view.show();
		expect(settleModelUv(view) && view.health() && view.health()->overlapping.size() == 4,
			   "the view's worker measures overlaps");
		expect(view.healthLegend().contains(QStringLiteral("4")) && view.accessibleDescription().contains(view.healthLegend()),
			   "the legend counts the faces and reaches screen readers");
		view.setHealthOverlay(ModelUvHealthOverlay::Flipped, folded.surfaces[0].frames[0].positions, {30, 10});
		expect(settleModelUv(view) && view.health() && view.health()->flipped.size() == 2 && view.healthLegend().contains(QStringLiteral("2")),
			   "switching the overlay re-measures");
		view.setHealthOverlay(ModelUvHealthOverlay::None, {}, {});
		expect(settleModelUv(view) && !view.health() && view.healthLegend().isEmpty(), "None clears the overlay");
	}

	for (int scenario = 0; scenario < 2; ++scenario)
	{
		Expansion expansion;
		if (scenario)
			app.installTranslator(&expansion);
		app.setLayoutDirection(scenario ? Qt::RightToLeft : Qt::LeftToRight);
		applyStudioTheme(app, studioThemeTokens(scenario ? StudioTheme::HighContrastDark : StudioTheme::HighContrastLight, UiDensity::Standard,
												scenario ? 200 : 100));
		ModelEditorDialog editor;
		editor.setAttribute(Qt::WA_DeleteOnClose, false);
		editor.findChild<QAction *>(QStringLiteral("meshRecoveryEnabled"))->setChecked(false);
		expect(editor.setMesh(folded, &error), "open the folded strip");
		editor.resize(scenario ? 1700 : 1440, scenario ? 1100 : 960);
		editor.show();
		app.processEvents();
		auto *view = editor.findChild<ModelUvView *>(QStringLiteral("meshUvPreview"));
		auto *overlay = editor.findChild<QComboBox *>(QStringLiteral("meshUvHealthOverlay"));
		auto *stretch = editor.findChild<QDoubleSpinBox *>(QStringLiteral("meshUvHealthStretch"));
		auto *selectShown = editor.findChild<QAction *>(QStringLiteral("selectMeshUvProblems"));
		auto *check = editor.findChild<QAction *>(QStringLiteral("checkMeshUvHealth"));
		if (!view || !overlay || !stretch || !selectShown || !check)
		{
			std::cerr << "Missing UV health controls\n";
			return 1;
		}
		showUvTab(editor, view);
		app.processEvents();
		for (QWidget *control : {static_cast<QWidget *>(overlay), static_cast<QWidget *>(stretch)})
		{
			expect(!control->accessibleName().isEmpty() && !control->accessibleDescription().isEmpty() && control->focusPolicy() != Qt::NoFocus,
				   "health controls are named, described and focusable");
			// A narrow UV toolbar folds them into its extension menu, still reachable.
			expect(control->parentWidget() != nullptr, "health controls sit in the UV toolbar");
		}
		expect(!selectShown->isEnabled() && !stretch->isEnabled(), "selecting needs an overlay; the limit applies to stretch");
		overlay->setCurrentIndex(int(ModelUvHealthOverlay::Overlaps));
		expect(settleModelUv(*view) && view->health() && view->health()->overlapping.size() == 4 && selectShown->isEnabled(),
			   "the editor's overlay measures the displayed pose");
		const auto before = source(editor.document().mesh());
		selectShown->trigger();
		expect(editor.document().selection().faces == QSet<int>{2, 3, 4, 5} && source(editor.document().mesh()) == before &&
				   !editor.document().isModified(),
			   "Select Shown Problems selects the overlapping faces without editing");
		editor.findChild<QAction *>(QStringLiteral("meshUvSelectFlipped"))->trigger();
		expect(editor.document().selection().faces == QSet<int>{4, 5} && !editor.document().isModified(), "Select Flipped UVs");
		overlay->setCurrentIndex(int(ModelUvHealthOverlay::Stretch));
		expect(stretch->isEnabled() && settleModelUv(*view) && view->health() && view->health()->stretched.size() == 2,
			   "the stretch overlay uses the limit");
		stretch->setValue(2);
		expect(settleModelUv(*view) && view->health() && view->health()->stretched.isEmpty(), "raising the limit re-measures");
		save(editor, QStringLiteral("editor-%1").arg(scenario));
		ModelUvHealthReport report;
		expect(editor.checkUvHealth(&report, &error) && report.overlapping.size() == 4 && report.native.size() == 3, "Check UV Health reports");
		const auto *status = editor.findChild<QLabel *>(QStringLiteral("meshStatus"));
		expect(!status || status->text().contains(QStringLiteral("4")), "the status line carries the counts");
		overlay->setCurrentIndex(0);
		app.removeTranslator(&expansion);
	}
	std::cout << checks << " UV health UI checks; " << failures << " failures\n";
	return failures ? 1 : 0;
}
