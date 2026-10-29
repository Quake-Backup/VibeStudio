#include "app/model_uv_layout_dialog.h"

#include "app/locale_number.h"
#include "app/model_document_work.h"
#include "app/model_editor_dialog.h"
#include "app/wrapping_check_box.h"
#include "core/idtech_image.h"
#include "core/model_file_io.h"
#include "core/model_mdl.h"
#include "core/texture_export.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <algorithm>

namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelUvLayoutDialog)
};
void describe(QWidget *widget, const QString &name, const QString &description)
{
	widget->setAccessibleName(name);
	widget->setAccessibleDescription(description);
	widget->setToolTip(description);
}
} // namespace

ModelUvLayoutDialog::ModelUvLayoutDialog(const ModelMesh &mesh, int surface, QSize skinSize, bool canStage, QWidget *parent)
	: QDialog(parent), m_mesh(mesh), m_surface(surface), m_skinSize(skinSize)
{
	setObjectName(QStringLiteral("modelUvLayoutDialog"));
	setWindowTitle(Text::tr("Export UV Layout"));
	setAccessibleName(windowTitle());
	auto *layout = new QVBoxLayout(this);
	auto *form = new QFormLayout;
	form->setRowWrapPolicy(QFormLayout::WrapLongRows);
	form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	layout->addLayout(form);
	auto *intro = new QLabel(Text::tr("Draw the UV layout as a skin template to paint over. Nothing in the model changes."));
	intro->setWordWrap(true);
	intro->setTextFormat(Qt::PlainText);
	form->addRow(intro);
	auto *dimensions = new QHBoxLayout;
	m_width = new QSpinBox;
	m_height = new QSpinBox;
	for (auto *field : {m_width, m_height})
	{
		field->setRange(1, modelUvLayoutMaxSize);
		field->setLayoutDirection(Qt::LeftToRight);
		dimensions->addWidget(field);
	}
	m_width->setObjectName(QStringLiteral("uvLayoutWidth"));
	m_height->setObjectName(QStringLiteral("uvLayoutHeight"));
	describe(m_width, Text::tr("Layout width in pixels"), Text::tr("Match the skin's original width, in pixels from 1 to %L1.").arg(modelUvLayoutMaxSize));
	describe(m_height, Text::tr("Layout height in pixels"), Text::tr("Match the skin's original height, in pixels from 1 to %L1.").arg(modelUvLayoutMaxSize));
	auto *useSkin = new QPushButton(Text::tr("Use Skin Size"));
	useSkin->setObjectName(QStringLiteral("uvLayoutUseSkinSize"));
	describe(useSkin, useSkin->text(), Text::tr("Set the size to the displayed material's original size, or the MDL or MD2 skin size: %L1 × %L2.")
										   .arg(skinSize.width())
										   .arg(skinSize.height()));
	dimensions->addWidget(useSkin);
	form->addRow(Text::tr("Size"), dimensions);
	connect(useSkin, &QPushButton::clicked, this,
			[this]
			{
				m_width->setValue(m_skinSize.width());
				m_height->setValue(m_skinSize.height());
			});
	m_width->setValue(std::clamp(skinSize.width(), 1, modelUvLayoutMaxSize));
	m_height->setValue(std::clamp(skinSize.height(), 1, modelUvLayoutMaxSize));
	m_scope = new QComboBox;
	m_scope->setObjectName(QStringLiteral("uvLayoutScope"));
	m_scope->addItems({Text::tr("Active surface"), Text::tr("Surfaces sharing its material"), Text::tr("All surfaces")});
	const int sharing = int(modelUvLayoutMaterialSurfaces(mesh, surface).size());
	describe(m_scope, Text::tr("Surfaces to draw"),
			 Text::tr("Surfaces sharing the active surface's material slot draw together on one skin; %Ln surfaces share it.", nullptr, sharing));
	form->addRow(Text::tr("Surfaces"), m_scope);
	m_background = new QComboBox;
	m_background->setObjectName(QStringLiteral("uvLayoutBackground"));
	m_background->addItems({Text::tr("Transparent"), Text::tr("White"), Text::tr("Black")});
	describe(m_background, Text::tr("Layout background"), Text::tr("PCX has no transparency; a transparent background is written white there."));
	form->addRow(Text::tr("Background"), m_background);
	m_fill = new QDoubleSpinBox;
	m_fill->setObjectName(QStringLiteral("uvLayoutFill"));
	m_fill->setRange(0, 100);
	m_fill->setSuffix(QStringLiteral(" %"));
	m_fill->setLayoutDirection(Qt::LeftToRight);
	describe(m_fill, Text::tr("Island fill opacity"), Text::tr("Shade each island under its lines; 0 draws lines only."));
	form->addRow(Text::tr("Island Fill"), m_fill);
	m_tints = new WrappingCheckBox(Text::tr("A different tint for each island"));
	m_tints->setObjectName(QStringLiteral("uvLayoutTints"));
	m_tints->setChecked(true);
	describe(m_tints, m_tints->text(), Text::tr("Islands are told apart by their outlines too, so tints are optional."));
	form->addRow(m_tints);
	m_seams = new WrappingCheckBox(Text::tr("Draw island boundaries in magenta"));
	m_seams->setObjectName(QStringLiteral("uvLayoutSeams"));
	m_seams->setChecked(true);
	describe(m_seams, m_seams->text(), Text::tr("Marked seams and split UV edges, drawn above the other edges."));
	form->addRow(m_seams);
	m_lineWidth = new QDoubleSpinBox;
	m_lineWidth->setObjectName(QStringLiteral("uvLayoutLineWidth"));
	m_lineWidth->setRange(0.25, 64);
	m_lineWidth->setValue(1);
	m_lineWidth->setSingleStep(0.5);
	m_lineWidth->setLayoutDirection(Qt::LeftToRight);
	describe(m_lineWidth, Text::tr("Line width in pixels"), Text::tr("One pixel suits low-resolution idTech skins."));
	form->addRow(Text::tr("Line Width"), m_lineWidth);
	m_antialias = new WrappingCheckBox(Text::tr("Smooth lines"));
	m_antialias->setObjectName(QStringLiteral("uvLayoutAntialias"));
	describe(m_antialias, m_antialias->text(), Text::tr("Off draws crisp single-texel lines for pixel skins."));
	form->addRow(m_antialias);
	m_wrap = new WrappingCheckBox(Text::tr("Draw faces outside the tile inside it"));
	m_wrap->setObjectName(QStringLiteral("uvLayoutWrap"));
	describe(m_wrap, m_wrap->text(), Text::tr("Faces beyond 0–1 move by whole tiles into the image; otherwise they are cut off at its edge."));
	form->addRow(m_wrap);
	m_destination = new QComboBox;
	m_destination->setObjectName(QStringLiteral("uvLayoutDestination"));
	m_destination->addItem(Text::tr("Save to a file"));
	if (canStage)
		m_destination->addItem(Text::tr("Stage in the open package"));
	describe(m_destination, Text::tr("Where to write the layout"), Text::tr("A staged layout is saved with the package; a file is written at once."));
	form->addRow(Text::tr("Destination"), m_destination);
	auto *pathRow = new QHBoxLayout;
	m_path = new QLineEdit;
	m_path->setObjectName(QStringLiteral("uvLayoutPath"));
	describe(m_path, Text::tr("Layout path"), Text::tr("A .png, .tga or .pcx file, or a package path when staging."));
	m_browse = new QPushButton(Text::tr("Browse…"));
	m_browse->setObjectName(QStringLiteral("uvLayoutBrowse"));
	describe(m_browse, Text::tr("Choose the layout file"), Text::tr("Choose where to save the layout image."));
	pathRow->addWidget(m_path, 1);
	pathRow->addWidget(m_browse);
	form->addRow(Text::tr("Path"), pathRow);
	connect(m_browse, &QPushButton::clicked, this, &ModelUvLayoutDialog::browse);
	connect(m_destination, &QComboBox::currentIndexChanged, this, [this](int index) { m_browse->setEnabled(index == 0); });
	connect(m_path, &QLineEdit::textEdited, this, [this] { m_overwrite = false; });
	const auto name = mesh.surfaces.value(surface).name;
	m_path->setText((name.isEmpty() ? QStringLiteral("skin") : name) + QStringLiteral("_uv.png"));
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	auto *ok = buttons->button(QDialogButtonBox::Ok);
	ok->setText(Text::tr("Export"));
	ok->setObjectName(QStringLiteral("uvLayoutExport"));
	describe(ok, ok->text(), Text::tr("Draw, encode and write the layout on a worker with progress and Cancel."));
	layout->addWidget(buttons);
	connect(buttons, &QDialogButtonBox::accepted, this, &ModelUvLayoutDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &ModelUvLayoutDialog::reject);
}

void ModelUvLayoutDialog::browse()
{
	const auto path = QFileDialog::getSaveFileName(this, Text::tr("Export UV Layout"), m_path->text(),
												   Text::tr("Images (*.png *.tga *.pcx)"));
	if (path.isEmpty())
		return;
	m_path->setText(path);
	// The save dialog already asked before replacing an existing file.
	m_overwrite = true;
}

QString ModelUvLayoutDialog::problem() const
{
	const auto suffix = QFileInfo(m_path->text().trimmed()).suffix().toLower();
	if (suffix != QStringLiteral("png") && suffix != QStringLiteral("tga") && suffix != QStringLiteral("pcx"))
		return Text::tr("Name a .png, .tga or .pcx path.");
	return {};
}

void ModelUvLayoutDialog::accept()
{
	const auto message = problem();
	if (!message.isEmpty())
	{
		m_path->setToolTip(message);
		m_path->setAccessibleDescription(message);
		m_path->setFocus(Qt::OtherFocusReason);
		return;
	}
	QDialog::accept();
}

ModelUvLayoutRequest ModelUvLayoutDialog::request() const
{
	ModelUvLayoutRequest request;
	request.size = {m_width->value(), m_height->value()};
	request.surfaces = m_scope->currentIndex() == 0 ? QVector<int>{m_surface}
					   : m_scope->currentIndex() == 1 ? modelUvLayoutMaterialSurfaces(m_mesh, m_surface)
													  : QVector<int>{};
	if (m_scope->currentIndex() == 2)
		for (int s = 0; s < m_mesh.surfaces.size(); ++s)
			request.surfaces.append(s);
	auto &options = request.options;
	options.background = m_background->currentIndex() == 1 ? QColor(Qt::white) : m_background->currentIndex() == 2 ? QColor(Qt::black) : QColor(0, 0, 0, 0);
	if (m_background->currentIndex() == 2)
		options.edge = Qt::white;
	options.fillOpacity = m_fill->value() / 100;
	options.islandTints = m_tints->isChecked();
	options.seams = m_seams->isChecked();
	options.lineWidth = m_lineWidth->value();
	options.antialias = m_antialias->isChecked();
	options.wrap = m_wrap->isChecked();
	request.path = m_path->text().trimmed();
	request.format = QFileInfo(request.path).suffix().toLower();
	request.stage = m_destination->currentIndex() == 1;
	request.overwrite = m_overwrite;
	return request;
}

void ModelEditorDialog::exportUvLayout()
{
	if (m_working || m_document.mesh().surfaces.isEmpty())
		return;
	const int surface = m_document.selection().surface;
	ModelUvLayoutDialog dialog(m_document.mesh(), surface, uvSkinSize(surface), bool(stageFile), this);
	if (dialog.exec() != QDialog::Accepted)
		return;
	const auto request = dialog.request();
	const auto mesh = m_document.mesh();
	QByteArray bytes;
	ModelUvLayoutReport report;
	QString error;
	const auto title = QCoreApplication::translate("ModelUvLayoutDialog", "Export UV Layout");
	const bool ok = runModelTask(
		this, title,
		[&](QString *failure, const ModelWorkControl &control)
		{
			QImage image;
			if (!renderModelUvLayout(mesh, request.surfaces, request.size, request.options, &image, &report, failure, control))
				return false;
			TextureExportOptions options;
			options.format = request.format == QStringLiteral("pcx")   ? TextureExportFormat::Pcx
							 : request.format == QStringLiteral("tga") ? TextureExportFormat::Targa
																		: TextureExportFormat::Png;
			options.alpha = request.format == QStringLiteral("pcx") ? TextureExportAlpha::Matte : TextureExportAlpha::Strict;
			options.matte = request.options.background.alpha() == 255 ? request.options.background : QColor(Qt::white);
			options.allowGeneratedPalette = true;
			options.fullbright = TextureFullbrightMode::Allow;
			IdTechPaletteResolution palette;
			palette.requestedPaletteId = QStringLiteral("quake");
			palette.palette = mesh.mdl.enabled ? modelMdlPreviewPalette(mesh) : generatedIdTechPalette(QStringLiteral("quake"));
			const auto encoded = encodeTextureExport(image, options, palette, [&](qint64 done, qint64 total)
													 { return modelWorkCheckpoint(control, ModelWorkPhase::Serializing, done, total); });
			if (!encoded.succeeded)
			{
				*failure = encoded.error;
				return false;
			}
			bytes = encoded.bytes;
			if (request.stage)
				return true;
			const auto target = inspectModelWriteTarget(request.path, control);
			if (!target.isValid())
			{
				*failure = target.error;
				return false;
			}
			if (target.existed && !request.overwrite)
			{
				*failure = QCoreApplication::translate("ModelUvLayoutDialog", "%1 already exists. Choose it with Browse… to replace it.")
							   .arg(QDir::toNativeSeparators(request.path));
				return false;
			}
			return writeModelFile(target, bytes, failure, control);
		},
		&error, !request.stage);
	if (ok && request.stage && !stageFile(request.path, bytes, true, &error))
	{
		m_status->setText(error, LiveStatus::Problem);
		return;
	}
	if (!ok)
	{
		m_status->setText(error, LiveStatus::Problem);
		return;
	}
	m_status->setText(QCoreApplication::translate("ModelUvLayoutDialog", "Exported the UV layout of %Ln faces as a %L1 × %L2 image to %3.", nullptr,
												  report.faces)
						  .arg(report.size.width())
						  .arg(report.size.height())
						  .arg(request.stage ? request.path : QDir::toNativeSeparators(request.path)),
					  LiveStatus::Result);
}
} // namespace vibestudio
