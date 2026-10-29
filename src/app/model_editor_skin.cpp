#include "app/model_editor_dialog.h"

#include "app/model_document_work.h"
#include "app/model_viewport.h"
#include "app/wrapping_check_box.h"
#include "core/idtech_image.h"
#include "core/level_materials.h"
#include "core/model_file_io.h"
#include "core/model_skin_handoff.h"
#include "core/model_skin_transfer.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <cstring>

// Skin round trips for the Mesh Editor: Edit Skin…, MDL member export and
// replacement from any image (core/model_skin_handoff.h), and Transfer Skin
// to New UVs… (core/model_skin_transfer.h).
namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelSkinHandoff)
};
// An image file as pixels: PCX, WAL and LMP through the indexed decoder so
// palette indices survive, anything else through Qt.
QImage decodeSkin(const QString &path, const QByteArray &bytes, const IdTechPalette &fallback, QString *error)
{
	const auto suffix = QFileInfo(path).suffix().toLower();
	if (suffix == QStringLiteral("pcx") || suffix == QStringLiteral("wal") || suffix == QStringLiteral("lmp"))
	{
		ModelMdlSkinInput skin;
		if (!decodeModelMdlSkin(path, bytes, fallback, &skin, error))
			return {};
		IdTechPalette palette;
		if (!parseIdTechPaletteBytes(skin.palette, QStringLiteral("quake"), &palette))
			palette = fallback;
		QImage image(skin.size, QImage::Format_Indexed8);
		QVector<QRgb> table(palette.colors.cbegin(), palette.colors.cend());
		table.resize(256);
		image.setColorTable(table);
		for (int y = 0; y < skin.size.height(); ++y)
			std::memcpy(image.scanLine(y), skin.pixels.constData() + qint64(y) * skin.size.width(), size_t(skin.size.width()));
		return image;
	}
	auto image = QImage::fromData(bytes);
	if (image.isNull() && error)
		*error = Text::tr("%1 could not be read as an image.").arg(path);
	return image;
}
// Encodes by the path's suffix (png, tga or pcx); indexed images keep indices.
bool encodeSkin(const QImage &image, const QString &path, QByteArray *bytes, QString *error, const ModelWorkControl &control)
{
	const auto suffix = QFileInfo(path).suffix().toLower();
	TextureExportOptions options;
	options.allowGeneratedPalette = true;
	options.fullbright = TextureFullbrightMode::Allow;
	const bool indexed = image.format() == QImage::Format_Indexed8;
	if (suffix == QStringLiteral("pcx"))
	{
		options.format = TextureExportFormat::Pcx;
		options.alpha = TextureExportAlpha::Matte;
	}
	else if (suffix == QStringLiteral("tga"))
		options.format = TextureExportFormat::Targa;
	else if (suffix == QStringLiteral("png"))
		options.format = indexed ? TextureExportFormat::IndexedPng : TextureExportFormat::Png;
	else
	{
		*error = Text::tr("Write the skin as .png, .tga or .pcx.");
		return false;
	}
	IdTechPaletteResolution palette;
	palette.requestedPaletteId = QStringLiteral("quake");
	palette.palette = indexed ? IdTechPalette{} : generatedIdTechPalette(QStringLiteral("quake"));
	if (indexed)
	{
		palette.palette.id = QStringLiteral("skin");
		palette.palette.colors = image.colorTable();
		palette.palette.colors.resize(256);
	}
	const auto encoded = encodeTextureExport(image, options, palette, [&](qint64 done, qint64 total)
											 { return modelWorkCheckpoint(control, ModelWorkPhase::Serializing, done, total); });
	if (!encoded.succeeded)
	{
		*error = encoded.error;
		return false;
	}
	*bytes = encoded.bytes;
	return true;
}
} // namespace

bool ModelEditorDialog::exportMdlSkinMember(const QString &path, QString *error)
{
	const int skin = m_mdlSkin->currentIndex(), member = std::max(0, m_mdlMember->currentIndex());
	const auto format = QFileInfo(path).suffix().toLower();
	const auto mesh = m_document.mesh();
	const bool ok = runModelTask(
		this, Text::tr("Export Skin Member"),
		[&](QString *failure, const ModelWorkControl &control)
		{
			QByteArray bytes;
			if (!exportModelMdlSkinMember(mesh, skin, member, format, &bytes, failure, control))
				return false;
			const auto target = inspectModelWriteTarget(path, control);
			if (!target.isValid())
			{
				*failure = target.error;
				return false;
			}
			return writeModelFile(target, bytes, failure, control);
		},
		error, true);
	if (ok)
		m_status->setText(Text::tr("Exported skin %1, member %2, with its palette indices to %3.")
							  .arg(skin)
							  .arg(member)
							  .arg(QDir::toNativeSeparators(path)),
						  LiveStatus::Result);
	else if (error)
		m_status->setText(*error, LiveStatus::Problem);
	return ok;
}

bool ModelEditorDialog::replaceMdlSkinMemberFromImage(const QString &path, QString *error)
{
	ModelEdit edit;
	edit.kind = ModelEditKind::ReplaceMdlSkinMember;
	edit.selection = m_document.selection();
	edit.mdlSkinSlot = m_mdlSkin->currentIndex();
	edit.mdlSkinMember = std::max(0, m_mdlMember->currentIndex());
	if (!performWork(
			Text::tr("Replace Skin Member From Image"),
			[path, edit](ModelDocument &candidate, QString *failure, const ModelWorkControl &control) mutable
			{
				QByteArray bytes;
				if (!readModelFile(path, &bytes, failure, control))
					return false;
				const auto image = decodeSkin(path, bytes, modelMdlPreviewPalette(candidate.mesh()), failure);
				return !image.isNull() &&
					   modelMdlSkinInputFromImage(candidate.mesh(), image, TextureFullbrightMode::Exclude, &edit.mdlSkin, failure, control) &&
					   candidate.edit(edit, failure, control);
			},
			error))
		return false;
	refresh();
	m_status->setText(Text::tr("Replaced skin %1, member %2, from %3, mapped to the model's palette.")
						  .arg(edit.mdlSkinSlot)
						  .arg(edit.mdlSkinMember)
						  .arg(QFileInfo(path).fileName()),
					  LiveStatus::Result);
	return true;
}

bool ModelEditorDialog::editSurfaceSkin(QString *error)
{
	const auto fail = [&](const QString &message)
	{
		if (error)
			*error = message;
		m_status->setText(message, LiveStatus::Problem);
		return false;
	};
	if (!editSkin)
		return fail(Text::tr("Edit Skin needs the studio's Textures workbench."));
	const auto &mesh = m_document.mesh();
	const int surface = m_document.selection().surface;
	if (mesh.mdl.enabled && !mesh.embeddedSkins.isEmpty())
	{
		// The member opens as an indexed image; export it from Textures and use
		// Replace Member from Image to bring the edit back into the MDL.
		const int skin = m_mdlSkin->currentIndex(), member = std::max(0, m_mdlMember->currentIndex());
		QString problem;
		const auto image = modelMdlSkinMemberImage(mesh, skin, member, &problem);
		if (image.isNull())
			return fail(problem);
		const auto name = QStringLiteral("skin_%1_%2.pcx").arg(skin).arg(member);
		return editSkin(image, name, error) || fail(error ? *error : QString());
	}
	QString imagePath;
	QSize size;
	for (const auto &material : m_materialAssets.materials)
		if (material.key == modelPreviewSurfaceMaterialKey(surface))
		{
			imagePath = material.imagePath;
			size = material.sourceSize;
		}
	if (imagePath.isEmpty())
		return fail(Text::tr("The surface's material has no image in the open package. Use New Skin, or assign an image material first."));
	// The original pixels, not the downsampled preview.
	const auto files = readPackageFiles ? readPackageFiles({imagePath}) : QHash<QString, QByteArray>{};
	QString problem;
	auto image = files.contains(imagePath) ? decodeSkin(imagePath, files.value(imagePath), modelMdlPreviewPalette(mesh), &problem)
										   : m_surfaceImages.value(surface);
	if (image.isNull())
		return fail(problem.isEmpty() ? Text::tr("The skin image could not be read.") : problem);
	if (!editSkin(image, imagePath, &problem))
		return fail(problem);
	m_status->setText(Text::tr("Opened %1 in the texture editor. Stage it there to update the model's preview.").arg(imagePath), LiveStatus::Result);
	Q_UNUSED(size);
	return true;
}

void ModelEditorDialog::transferSkin()
{
	if (m_working || !canTransferSkin())
	{
		m_status->setText(Text::tr("Transfer Skin works straight after a UV edit on a surface with a skin."), LiveStatus::Problem);
		return;
	}
	const auto mesh = m_document.mesh();
	const int surface = m_uvTransferSurface;
	const bool mdl = mesh.mdl.enabled && !mesh.embeddedSkins.isEmpty();
	const int skin = mdl ? m_mdlSkin->currentIndex() : -1, member = mdl ? std::max(0, m_mdlMember->currentIndex()) : -1;
	QImage source;
	QString imagePath, problem;
	if (mdl)
		source = modelMdlSkinMemberImage(mesh, skin, member, &problem);
	else
	{
		for (const auto &material : m_materialAssets.materials)
			if (material.key == modelPreviewSurfaceMaterialKey(surface))
				imagePath = material.imagePath;
		const auto files = readPackageFiles && !imagePath.isEmpty() ? readPackageFiles({imagePath}) : QHash<QString, QByteArray>{};
		source = files.contains(imagePath) ? decodeSkin(imagePath, files.value(imagePath), modelMdlPreviewPalette(mesh), &problem)
										   : m_surfaceImages.value(surface);
	}
	if (source.isNull())
	{
		m_status->setText(problem.isEmpty() ? Text::tr("The surface has no skin image to transfer.") : problem, LiveStatus::Problem);
		return;
	}
	// Settings: filter, padding, size and where the result goes.
	QDialog dialog(this);
	dialog.setObjectName(QStringLiteral("modelSkinTransferDialog"));
	dialog.setWindowTitle(Text::tr("Transfer Skin to New UVs"));
	dialog.setAccessibleName(dialog.windowTitle());
	auto *layout = new QVBoxLayout(&dialog);
	auto *form = new QFormLayout;
	form->setRowWrapPolicy(QFormLayout::WrapLongRows);
	layout->addLayout(form);
	auto *intro = new QLabel(mdl ? Text::tr("Repaint skin %1, member %2, for the new UVs by sampling it through the old ones. Faces keep their "
											"painted texels; the member is replaced as one undo step.")
									   .arg(skin)
									   .arg(member)
								 : Text::tr("Repaint %1 for the new UVs by sampling it through the old ones.").arg(imagePath));
	intro->setWordWrap(true);
	intro->setTextFormat(Qt::PlainText);
	form->addRow(intro);
	auto *filter = new QComboBox;
	filter->setObjectName(QStringLiteral("skinTransferFilter"));
	filter->addItems({Text::tr("Nearest texel"), Text::tr("Bilinear")});
	filter->setAccessibleName(Text::tr("Sampling"));
	filter->setToolTip(Text::tr("Nearest copies texels exactly and keeps palette indices; bilinear blends neighbours and maps indexed skins "
								"to the nearest palette colour."));
	filter->setAccessibleDescription(filter->toolTip());
	form->addRow(Text::tr("Sampling"), filter);
	auto *padding = new QSpinBox;
	padding->setObjectName(QStringLiteral("skinTransferPadding"));
	padding->setRange(0, 64);
	padding->setValue(2);
	padding->setLayoutDirection(Qt::LeftToRight);
	padding->setAccessibleName(Text::tr("Padding in texels"));
	padding->setToolTip(Text::tr("Texels repeated outward around each island, for filtering and mipmaps."));
	padding->setAccessibleDescription(padding->toolTip());
	form->addRow(Text::tr("Padding"), padding);
	auto *width = new QSpinBox, *height = new QSpinBox;
	auto *dimensions = new QHBoxLayout;
	for (auto *field : {width, height})
	{
		field->setRange(1, 4096);
		field->setLayoutDirection(Qt::LeftToRight);
		field->setEnabled(!mdl);
		dimensions->addWidget(field);
	}
	width->setObjectName(QStringLiteral("skinTransferWidth"));
	height->setObjectName(QStringLiteral("skinTransferHeight"));
	width->setAccessibleName(Text::tr("Output width in pixels"));
	height->setAccessibleName(Text::tr("Output height in pixels"));
	width->setValue(source.width());
	height->setValue(source.height());
	form->addRow(Text::tr("Size"), dimensions);
	auto *destination = new QComboBox;
	destination->setObjectName(QStringLiteral("skinTransferDestination"));
	if (mdl)
		destination->addItem(Text::tr("Replace the skin member"));
	else
	{
		if (stageFile)
			destination->addItem(Text::tr("Stage over %1 in the package").arg(imagePath));
		destination->addItem(Text::tr("Save to a file"));
	}
	destination->setAccessibleName(Text::tr("Where the transferred skin goes"));
	form->addRow(Text::tr("Destination"), destination);
	auto *path = new QLineEdit(imagePath.isEmpty() ? QStringLiteral("skin.png") : QFileInfo(imagePath).fileName());
	path->setObjectName(QStringLiteral("skinTransferPath"));
	path->setAccessibleName(Text::tr("Transferred skin file"));
	path->setEnabled(!mdl && destination->count() == 1);
	form->addRow(Text::tr("File"), path);
	connect(destination, &QComboBox::currentIndexChanged, &dialog,
			[path, destination, mdl, this] { path->setEnabled(!mdl && (destination->currentIndex() == 1 || !stageFile)); });
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	buttons->button(QDialogButtonBox::Ok)->setText(Text::tr("Transfer"));
	buttons->button(QDialogButtonBox::Ok)->setObjectName(QStringLiteral("skinTransferApply"));
	layout->addWidget(buttons);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	if (dialog.exec() != QDialog::Accepted)
		return;
	ModelSkinTransferOptions options;
	options.filter = filter->currentIndex() == 1 ? ModelSkinTransferFilter::Bilinear : ModelSkinTransferFilter::Nearest;
	options.padding = padding->value();
	const QSize size = mdl ? mesh.mdl.skinSize : QSize(width->value(), height->value());
	const bool toFile = !mdl && (!stageFile || destination->currentIndex() == 1);
	const QString filePath = path->text().trimmed();
	const auto before = m_uvTransferBefore;
	const auto after = mesh.surfaces[surface];
	QImage result;
	QByteArray bytes;
	ModelSkinTransferReport report;
	QString error;
	if (!runModelTask(
			this, Text::tr("Transfer Skin"),
			[&](QString *failure, const ModelWorkControl &control)
			{
				if (!transferModelSkin(before, after, source, size, options, &result, &report, failure, control))
					return false;
				if (mdl)
					return true;
				if (!encodeSkin(result, toFile ? filePath : imagePath, &bytes, failure, control))
					return false;
				if (!toFile)
					return true;
				const auto target = inspectModelWriteTarget(filePath, control);
				if (!target.isValid())
				{
					*failure = target.error;
					return false;
				}
				if (target.existed)
				{
					*failure = Text::tr("%1 already exists. Choose another file name.").arg(QDir::toNativeSeparators(filePath));
					return false;
				}
				return writeModelFile(target, bytes, failure, control);
			},
			&error, toFile))
	{
		m_status->setText(error, LiveStatus::Problem);
		return;
	}
	if (mdl)
	{
		ModelEdit edit;
		edit.kind = ModelEditKind::ReplaceMdlSkinMember;
		edit.selection = m_document.selection();
		edit.mdlSkinSlot = skin;
		edit.mdlSkinMember = member;
		if (!modelMdlSkinInputFromImage(mesh, result, TextureFullbrightMode::Preserve, &edit.mdlSkin, &error) || !applyEdit(edit, &error))
		{
			m_status->setText(error, LiveStatus::Problem);
			return;
		}
	}
	else if (!toFile && !stageFile(imagePath, bytes, true, &error))
	{
		m_status->setText(error, LiveStatus::Problem);
		return;
	}
	m_status->setText(Text::tr("Transferred the skin: %Ln faces repainted on %L1 × %L2 texels.", nullptr, report.faces)
						  .arg(size.width())
						  .arg(size.height()),
					  LiveStatus::Result);
}
} // namespace vibestudio
