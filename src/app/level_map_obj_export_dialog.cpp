#include "app/level_map_obj_export_dialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

// Workflow references: TrenchBroom ObjExportDialog at 4c756be1
// (GPL-3.0-or-later) and NetRadiant Custom contrib/brushexport/interface.cpp
// at 449778b3 (GPL-2.0-or-later). Original Qt implementation; see
// docs/CREDITS.md.
namespace vibestudio {

LevelMapObjExportDialog::LevelMapObjExportDialog(const QString& path, const LevelMapObjExportOptions& options, const QString& summary, QWidget* parent)
	: QDialog(parent), m_options(options)
{
	setObjectName(QStringLiteral("levelObjExportDialog"));
	setWindowTitle(options.selectionOnly ? tr("Export Selection as OBJ") : tr("Export Map as OBJ"));
	setAccessibleName(windowTitle());
	setAccessibleDescription(tr("Choose the OBJ file and how brushes and patches are written to it."));
	setWindowModality(Qt::WindowModal);
	setMinimumWidth(520);
	auto* layout = new QVBoxLayout(this);
	auto* what = new QLabel(summary, this);
	what->setObjectName(QStringLiteral("levelObjExportSummary"));
	what->setAccessibleName(tr("What is exported"));
	what->setWordWrap(true);
	layout->addWidget(what);

	auto* form = new QFormLayout;
	const auto row = [&](const QString& text, QWidget* buddy, QWidget* field) {
		auto* label = new QLabel(text, this);
		label->setBuddy(buddy);
		form->addRow(label, field);
	};
	auto* fileRow = new QWidget(this);
	auto* fileLayout = new QHBoxLayout(fileRow);
	fileLayout->setContentsMargins(0, 0, 0, 0);
	m_path = new QLineEdit(QDir::toNativeSeparators(path), fileRow);
	m_path->setObjectName(QStringLiteral("levelObjExportPath"));
	m_path->setAccessibleName(tr("OBJ file"));
	m_path->setAccessibleDescription(tr("The .obj file to write. The material library is written beside it with the .mtl extension."));
	m_path->setPlaceholderText(tr("Type a path or choose Browse"));
	fileLayout->addWidget(m_path, 1);
	auto* browse = new QPushButton(tr("&Browse…"), fileRow);
	browse->setObjectName(QStringLiteral("levelObjExportBrowse"));
	browse->setAccessibleName(tr("Browse for the OBJ file"));
	fileLayout->addWidget(browse);
	row(tr("&File:"), m_path, fileRow);

	m_grouping = new QComboBox(this);
	m_grouping->setObjectName(QStringLiteral("levelObjExportGrouping"));
	m_grouping->setAccessibleName(tr("Group faces"));
	m_grouping->setAccessibleDescription(tr("One object for each brush and patch, one for each material, or one mesh for everything."));
	m_grouping->addItem(tr("By brush and patch"), static_cast<int>(LevelMapObjGrouping::Object));
	m_grouping->addItem(tr("By material"), static_cast<int>(LevelMapObjGrouping::Material));
	m_grouping->addItem(tr("Into one mesh"), static_cast<int>(LevelMapObjGrouping::Single));
	m_grouping->setCurrentIndex(std::max(0, m_grouping->findData(static_cast<int>(options.grouping))));
	row(tr("&Group faces:"), m_grouping, m_grouping);

	const auto check = [&](const QString& name, const QString& text, const QString& accessible, const QString& tip, bool checked) {
		auto* box = new QCheckBox(text, this);
		box->setObjectName(name);
		box->setAccessibleName(accessible);
		box->setToolTip(tip);
		box->setChecked(checked);
		form->addRow(box);
		return box;
	};
	m_objects = check(QStringLiteral("levelObjExportObjects"), tr("Write &objects (o) instead of groups (g)"), tr("Write objects instead of groups"),
		tr("Blender and most modellers import each OBJ object separately."), options.objects);
	m_weld = check(QStringLiteral("levelObjExportWeld"), tr("&Weld shared corners"), tr("Weld shared corners"),
		tr("Faces of one object or group share the corners they meet at, as NetRadiant Custom's brush export welds vertices."), options.weld);
	m_triangulate = check(QStringLiteral("levelObjExportTriangulate"), tr("Split faces into &triangles"), tr("Split faces into triangles"),
		tr("Write every face as triangles instead of one polygon."), options.triangulate);
	m_library = check(QStringLiteral("levelObjExportLibrary"), tr("Write a &material library (.mtl)"), tr("Write a material library"),
		tr("Write the .mtl file with each material's colour and image path, and name the materials in the OBJ."), options.materialLibrary);
	m_shortNames = check(QStringLiteral("levelObjExportShortNames"), tr("Short material &names (20 characters)"), tr("Short material names"),
		tr("Keep the last 20 characters of each material name, for importers that cannot read longer ones."), options.shortMaterialNames);
	m_zUp = check(QStringLiteral("levelObjExportZUp"), tr("Keep the map's &Z-up axes"), tr("Keep the map's Z-up axes"),
		tr("Off writes Y up, as modellers expect and as TrenchBroom and NetRadiant Custom do."), options.keepZUp);

	m_materialPaths = new QComboBox(this);
	m_materialPaths->setObjectName(QStringLiteral("levelObjExportMaterialPaths"));
	m_materialPaths->setAccessibleName(tr("Material paths"));
	m_materialPaths->setAccessibleDescription(tr("Write each image path as the game loads it, or relative to the folder of the OBJ."));
	m_materialPaths->addItem(tr("Relative to the game folder"), static_cast<int>(LevelMapObjMaterialPaths::Game));
	m_materialPaths->addItem(tr("Relative to the export folder"), static_cast<int>(LevelMapObjMaterialPaths::Export));
	m_materialPaths->setCurrentIndex(std::max(0, m_materialPaths->findData(static_cast<int>(options.materialPaths))));
	row(tr("Material &paths:"), m_materialPaths, m_materialPaths);

	auto* ignoredRow = new QWidget(this);
	auto* ignoredLayout = new QVBoxLayout(ignoredRow);
	ignoredLayout->setContentsMargins(0, 0, 0, 0);
	m_ignored = new QPlainTextEdit(options.ignoredMaterials.join(QLatin1Char('\n')), ignoredRow);
	m_ignored->setObjectName(QStringLiteral("levelObjExportIgnored"));
	m_ignored->setAccessibleName(tr("Ignored materials"));
	m_ignored->setAccessibleDescription(tr("One material a line. Faces using it are left out; a bare name such as caulk matches common/caulk."));
	m_ignored->setPlaceholderText(tr("One material a line, such as caulk"));
	m_ignored->setTabChangesFocus(true);
	m_ignored->setMaximumHeight(fontMetrics().lineSpacing() * 7);
	ignoredLayout->addWidget(m_ignored);
	auto* restore = new QPushButton(tr("&Restore Tool Materials"), ignoredRow);
	restore->setObjectName(QStringLiteral("levelObjExportRestoreIgnored"));
	restore->setAccessibleName(tr("Restore the tool materials"));
	restore->setToolTip(tr("Ignore caulk, clip, trigger, hint, skip, origin, nodraw and the other tool materials again."));
	ignoredLayout->addWidget(restore, 0, Qt::AlignLeading);
	row(tr("&Ignored materials:"), m_ignored, ignoredRow);
	layout->addLayout(form);

	m_replace = new QLabel(this);
	m_replace->setObjectName(QStringLiteral("levelObjExportReplaces"));
	m_replace->setAccessibleName(tr("Replaced files"));
	m_replace->setWordWrap(true);
	m_replace->hide();
	layout->addWidget(m_replace);

	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
	m_export = buttons->addButton(tr("Export"), QDialogButtonBox::AcceptRole);
	m_export->setObjectName(QStringLiteral("levelObjExportButton"));
	m_export->setAccessibleName(tr("Export the OBJ"));
	m_export->setDefault(true);
	auto* cancel = buttons->button(QDialogButtonBox::Cancel);
	cancel->setObjectName(QStringLiteral("levelObjExportCancel"));
	cancel->setAccessibleName(tr("Cancel the export"));
	layout->addWidget(buttons);

	connect(browse, &QPushButton::clicked, this, [this] {
		const QString chosen = QFileDialog::getSaveFileName(this, windowTitle(), this->path(), tr("Wavefront OBJ (*.obj)"));
		if (!chosen.isEmpty()) {
			m_path->setText(QDir::toNativeSeparators(chosen));
		}
	});
	connect(restore, &QPushButton::clicked, this, [this] { m_ignored->setPlainText(levelMapObjDefaultIgnoredMaterials().join(QLatin1Char('\n'))); });
	connect(m_path, &QLineEdit::textChanged, this, [this] { refresh(); });
	connect(m_library, &QCheckBox::toggled, this, [this] { refresh(); });
	connect(buttons, &QDialogButtonBox::accepted, this, [this] {
		if (m_export->isEnabled()) {
			accept();
		}
	});
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	refresh();
}

QString LevelMapObjExportDialog::path() const
{
	QString text = QDir::fromNativeSeparators(m_path->text().trimmed());
	if (text.isEmpty()) {
		return {};
	}
	if (QFileInfo(text).suffix().compare(QLatin1String("obj"), Qt::CaseInsensitive) != 0) {
		text += QStringLiteral(".obj");
	}
	return QFileInfo(text).absoluteFilePath();
}

LevelMapObjExportOptions LevelMapObjExportDialog::options() const
{
	auto options = m_options;
	options.grouping = static_cast<LevelMapObjGrouping>(m_grouping->currentData().toInt());
	options.objects = m_objects->isChecked();
	options.weld = m_weld->isChecked();
	options.triangulate = m_triangulate->isChecked();
	options.materialLibrary = m_library->isChecked();
	options.shortMaterialNames = m_shortNames->isChecked();
	options.keepZUp = m_zUp->isChecked();
	options.materialPaths = static_cast<LevelMapObjMaterialPaths>(m_materialPaths->currentData().toInt());
	options.ignoredMaterials.clear();
	const auto lines = m_ignored->toPlainText().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
	for (const auto& line : lines) {
		if (!line.trimmed().isEmpty()) {
			options.ignoredMaterials << line.trimmed();
		}
	}
	return options;
}

void LevelMapObjExportDialog::refresh()
{
	const auto obj = path();
	const bool library = m_library->isChecked();
	m_export->setEnabled(!obj.isEmpty());
	m_materialPaths->setEnabled(library);
	m_shortNames->setEnabled(library);
	QStringList existing;
	if (!obj.isEmpty() && QFileInfo::exists(obj)) {
		existing << QFileInfo(obj).fileName();
	}
	const auto mtl = obj.isEmpty() ? QString() : levelMapObjMaterialLibraryPath(obj);
	if (library && !mtl.isEmpty() && QFileInfo::exists(mtl)) {
		existing << QFileInfo(mtl).fileName();
	}
	m_replace->setVisible(!existing.isEmpty());
	m_replace->setText(existing.isEmpty() ? QString()
										  : tr("Replaces %1; the old files are kept in .vibestudio/map-backups beside them.").arg(existing.join(QStringLiteral(", "))));
	m_replace->setAccessibleDescription(m_replace->text());
}

} // namespace vibestudio
