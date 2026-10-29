#include "app/model_editor_dialog.h"

#include "app/model_document_work.h"
#include "app/model_viewport.h"
#include "app/wrapping_check_box.h"
#include "core/document_watch.h"
#include "core/idtech_image.h"
#include "core/level_materials.h"
#include "core/model_file_io.h"
#include "core/model_skin_handoff.h"
#include "core/model_skin_roundtrip.h"
#include "core/model_skin_transfer.h"
#include "core/model_uv_layout.h"
#include "core/studio_settings.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QBuffer>
#include <QHBoxLayout>
#include <QImageWriter>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTimer>
#include <QToolButton>
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
	else if (suffix == QStringLiteral("jpg") || suffix == QStringLiteral("jpeg"))
	{
		// Quake III skins are often JPEG, which the texture exporter does not write.
		QBuffer buffer(bytes);
		QImageWriter writer(&buffer, "jpg");
		writer.setQuality(95);
		if (!buffer.open(QIODevice::WriteOnly) || !writer.write(image.convertToFormat(QImage::Format_RGB32)))
		{
			*error = Text::tr("The skin could not be encoded as JPEG.");
			return false;
		}
		return modelWorkCheckpoint(control, ModelWorkPhase::Serializing);
	}
	else
	{
		*error = Text::tr("Write the skin as .png, .tga, .jpg or .pcx.");
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

QString ModelEditorDialog::skinRoundTripDirectory() const
{
	const auto root = deliveryContext ? deliveryContext().projectRoot : QString();
	if (!root.isEmpty())
		return QDir(root).filePath(QStringLiteral(".vibestudio/model-skins"));
	return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath(QStringLiteral("model-skins"));
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
	if (!editSkinFile)
		return fail(Text::tr("Edit Skin needs the studio's Textures workbench."));
	if (m_working)
		return fail(Text::tr("Wait for the current operation to finish."));
	const auto &mesh = m_document.mesh();
	const int surface = m_document.selection().surface;
	if (surface < 0 || surface >= mesh.surfaces.size())
		return fail(Text::tr("Select a surface first."));
	ModelSkinRoundTripLink link;
	link.model = mesh.sourcePath.isEmpty() ? QStringLiteral("model") : mesh.sourcePath;
	link.surface = surface;
	QImage skin;
	QString problem;
	if (mesh.mdl.enabled && !mesh.embeddedSkins.isEmpty())
	{
		link.mdlSkin = m_mdlSkin->currentIndex();
		link.mdlMember = std::max(0, m_mdlMember->currentIndex());
		skin = modelMdlSkinMemberImage(mesh, link.mdlSkin, link.mdlMember, &problem);
	}
	else
	{
		for (const auto &material : m_materialAssets.materials)
			if (material.key == modelPreviewSurfaceMaterialKey(surface))
				link.imagePath = material.imagePath;
		if (link.imagePath.isEmpty())
			return fail(Text::tr("The surface's material has no image in the open package. Assign an image material first."));
		// The original pixels, not the downsampled preview.
		const auto files = readPackageFiles ? readPackageFiles({link.imagePath}) : QHash<QString, QByteArray>{};
		skin = files.contains(link.imagePath) ? decodeSkin(link.imagePath, files.value(link.imagePath), modelMdlPreviewPalette(mesh), &problem)
											  : m_surfaceImages.value(surface);
	}
	if (skin.isNull())
		return fail(problem.isEmpty() ? Text::tr("The skin image could not be read.") : problem);
	link.size = skin.size();
	link.indexed = skin.format() == QImage::Format_Indexed8;
	const auto directory = skinRoundTripDirectory();
	const auto path = QDir(directory).filePath(modelSkinRoundTripFileName(link));
	const auto key = normalizeDocumentWatchPath(path);
	if (key == m_skinPendingPath && !m_skinPending.isNull())
		return fail(Text::tr("Textures saved this skin and it is not applied yet. Apply or dismiss it first."));
	// The project holds the skin and, on a locked layer above it, the
	// surface's UVs drawn at the skin's size.
	const auto source = mesh;
	if (!runModelTask(
			this, Text::tr("Edit Skin"),
			[&](QString *failure, const ModelWorkControl &control)
			{
				if (!QDir().mkpath(directory))
				{
					*failure = Text::tr("%1 could not be created.").arg(QDir::toNativeSeparators(directory));
					return false;
				}
				ModelUvLayoutOptions options;
				options.edge = QColor(255, 255, 0);
				options.seam = QColor(255, 0, 255);
				QImage guide;
				return renderModelUvLayout(source, {surface}, skin.size(), options, &guide, nullptr, failure, control) &&
					   writeModelSkinRoundTrip(path, skin, guide, link, failure, control);
			},
			&problem, true))
		return fail(problem);
	if (!m_skinWatch)
		m_skinWatch = std::make_unique<DocumentWatcher>();
	m_skinWatch->registerPath(path, DocumentWatchRole::Auxiliary, QStringLiteral("model-skin"));
	m_skinLinks.insert(key, link);
	m_skinWatchTimer->start();
	if (!editSkinFile(path, &problem))
		return fail(problem.isEmpty() ? Text::tr("The skin could not be opened in Textures.") : problem);
	m_status->setText(Text::tr("Opened the skin in Textures with a UV guide layer: %1. Save the texture project there to bring the skin back to the model.")
						  .arg(QFileInfo(path).fileName()),
					  LiveStatus::Result);
	return true;
}

void ModelEditorDialog::createSkinRoundTripRow(QBoxLayout *statusRow)
{
	// The offer to bring back a skin Textures saved: one eliding line with
	// Apply Skin and Dismiss, shown only while a saved skin waits.
	m_skinOfferRow = new QWidget;
	m_skinOfferRow->setObjectName(QStringLiteral("meshSkinOffer"));
	m_skinOfferRow->setAccessibleName(Text::tr("Skin saved in Textures"));
	auto *row = new QHBoxLayout(m_skinOfferRow);
	row->setContentsMargins(0, 0, 0, 0);
	m_skinOfferStatus = new StatusLineLabel;
	m_skinOfferStatus->setObjectName(QStringLiteral("meshSkinOfferStatus"));
	m_skinOfferStatus->setAccessibleName(Text::tr("Skin saved in Textures"));
	row->addWidget(m_skinOfferStatus, 1);
	const auto button = [row](const QString &text, const QString &name, const QString &tip)
	{
		auto *item = new QToolButton;
		item->setText(text);
		item->setObjectName(name);
		item->setAccessibleName(text);
		item->setToolTip(tip);
		item->setAccessibleDescription(tip);
		item->setToolButtonStyle(Qt::ToolButtonTextOnly);
		row->addWidget(item);
		return item;
	};
	auto *apply = button(Text::tr("Apply Skin"), QStringLiteral("applySkinFromTextures"),
						 Text::tr("Replace the MDL skin member as one undo step, or stage the skin image over its package path."));
	auto *dismiss = button(Text::tr("Dismiss"), QStringLiteral("dismissSkinFromTextures"),
						   Text::tr("Keep the model's skin. The saved texture project stays where it is."));
	connect(apply, &QToolButton::clicked, this, [this] { applySkinFromTextures(); });
	connect(dismiss, &QToolButton::clicked, this, &ModelEditorDialog::dismissSkinFromTextures);
	statusRow->addWidget(m_skinOfferRow, 2);
	m_skinOfferRow->hide();
	m_skinWatchTimer = new QTimer(this);
	m_skinWatchTimer->setInterval(500);
	connect(m_skinWatchTimer, &QTimer::timeout, this, [this] { checkSkinRoundTrips(); });
}

void ModelEditorDialog::checkSkinRoundTrips(bool flush)
{
	if (!m_skinWatch || m_working)
		return;
	auto events = m_skinWatch->poll();
	if (flush)
		events += m_skinWatch->flushPendingChanges();
	// Reading runs on the worker while the event loop turns; the timer waits.
	m_skinWatchTimer->stop();
	for (const auto &event : std::as_const(events))
	{
		if (!m_skinLinks.contains(event.path))
			continue;
		if (event.kind == DocumentChangeKind::Removed)
		{
			m_skinWatch->unregisterPath(event.path);
			m_skinLinks.remove(event.path);
			continue;
		}
		if (!documentChangeKindIsSubstantive(event.kind))
			continue;
		QImage image;
		ModelSkinRoundTripLink link;
		QString problem;
		const bool read = runModelTask(
			this, Text::tr("Read Skin from Textures"),
			[&](QString *failure, const ModelWorkControl &control) { return readModelSkinRoundTrip(event.path, &image, &link, failure, control); },
			&problem);
		m_skinWatch->refreshBaseline(event.path);
		if (!read)
		{
			m_status->setText(problem, LiveStatus::Problem);
			continue;
		}
		m_skinPendingPath = event.path;
		m_skinPending = image;
		m_skinPendingLink = link;
		if (m_skinAutoApply && m_skinAutoApply->isChecked())
			applySkinFromTextures();
		else
			showSkinOffer();
	}
	if (!m_skinLinks.isEmpty())
		m_skinWatchTimer->start();
}

void ModelEditorDialog::showSkinOffer()
{
	const auto &link = m_skinPendingLink;
	const auto text = link.isMdlMember() ? Text::tr("Textures saved skin %1, member %2. Apply it to the model?").arg(link.mdlSkin).arg(link.mdlMember)
										 : Text::tr("Textures saved %1. Apply it to the model?").arg(link.imagePath);
	m_skinOfferStatus->setText(text, LiveStatus::Update);
	m_skinOfferRow->show();
	if (auto *apply = m_skinOfferRow->findChild<QToolButton *>(QStringLiteral("applySkinFromTextures")))
		apply->setEnabled(true);
}

void ModelEditorDialog::dismissSkinFromTextures()
{
	if (m_skinPending.isNull())
		return;
	const auto file = QFileInfo(m_skinPendingPath).fileName();
	m_skinPending = {};
	m_skinPendingPath.clear();
	m_skinPendingLink = {};
	m_skinOfferRow->hide();
	m_status->setText(Text::tr("Kept the model's skin. The edited skin stays in %1.").arg(file), LiveStatus::Result);
}

bool ModelEditorDialog::applySkinFromTextures(QString *error)
{
	const auto fail = [&](const QString &message)
	{
		if (error)
			*error = message;
		m_status->setText(message, LiveStatus::Problem);
		return false;
	};
	if (m_skinPending.isNull())
		return fail(Text::tr("Textures has not saved an edited skin yet."));
	if (m_working)
		return fail(Text::tr("Wait for the current operation to finish."));
	const auto link = m_skinPendingLink;
	const auto image = m_skinPending;
	const auto &mesh = m_document.mesh();
	QString problem;
	if (link.isMdlMember())
	{
		if (!mesh.mdl.enabled || link.mdlSkin >= mesh.embeddedSkins.size())
			return fail(Text::tr("The model no longer has skin %1.").arg(link.mdlSkin));
		ModelEdit edit;
		edit.kind = ModelEditKind::ReplaceMdlSkinMember;
		edit.selection = m_document.selection();
		edit.mdlSkinSlot = link.mdlSkin;
		edit.mdlSkinMember = link.mdlMember;
		if (!performWork(
				Text::tr("Apply Skin from Textures"),
				[image, edit](ModelDocument &candidate, QString *failure, const ModelWorkControl &control) mutable
				{
					return modelSkinRoundTripMdlInput(candidate.mesh(), edit.mdlSkinSlot, edit.mdlSkinMember, image, &edit.mdlSkin, failure, control) &&
						   candidate.edit(edit, failure, control);
				},
				&problem))
			return fail(problem);
		refresh();
		m_status->setText(Text::tr("Applied the skin saved in Textures to skin %1, member %2, as one undo step.").arg(link.mdlSkin).arg(link.mdlMember),
						  LiveStatus::Result);
	}
	else
	{
		if (!stageFile)
			return fail(Text::tr("Open a package in the studio to stage the edited skin."));
		const auto mismatch = modelMd2SkinSizeMismatch(mesh, image.size(), locale());
		QByteArray bytes;
		if (!runModelTask(
				this, Text::tr("Apply Skin from Textures"),
				[&](QString *failure, const ModelWorkControl &control) { return encodeSkin(image, link.imagePath, &bytes, failure, control); },
				&problem) ||
			!stageFile(link.imagePath, bytes, true, &problem))
			return fail(problem);
		if (link.surface >= 0 && link.surface < mesh.surfaces.size())
			m_surfaceImages.insert(link.surface, image.convertToFormat(QImage::Format_ARGB32));
		refresh();
		m_status->setText(mismatch.isEmpty() ? Text::tr("Staged the skin saved in Textures over %1. Save the package to keep it.").arg(link.imagePath)
											 : Text::tr("Staged the skin saved in Textures over %1. Save the package to keep it. %2").arg(link.imagePath, mismatch),
						  LiveStatus::Result);
	}
	m_skinPending = {};
	m_skinPendingPath.clear();
	m_skinPendingLink = {};
	m_skinOfferRow->hide();
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
	auto *mapping = new QComboBox;
	mapping->setObjectName(QStringLiteral("skinTransferMapping"));
	mapping->addItems({Text::tr("Automatic"), Text::tr("By face"), Text::tr("By projection")});
	mapping->setAccessibleName(Text::tr("Match faces"));
	mapping->setToolTip(Text::tr("Automatic matches faces that kept their corners by index and finds reshaped, added or renumbered "
								 "faces by projection onto the old surface. By face matches by index only; by projection finds "
								 "every texel on the old surface."));
	mapping->setAccessibleDescription(mapping->toolTip());
	form->addRow(Text::tr("Match faces"), mapping);
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
	options.mapping = ModelSkinTransferMapping(mapping->currentIndex());
	options.referenceFrame = std::clamp(m_preview->frame(), 0, int(std::max<qsizetype>(1, std::min(m_uvTransferBefore.frames.size(), mesh.surfaces[surface].frames.size()))) - 1);
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
	auto summary = Text::tr("Transferred the skin: %Ln faces repainted on a %L1 × %L2 texel skin.", nullptr, report.faces).arg(size.width()).arg(size.height());
	const int projected = int(report.projectedFaces.size());
	if (projected > 0)
		summary += QLatin1Char(' ') + Text::tr("%Ln of them were found by projection onto the old surface.", nullptr, projected);
	m_status->setText(summary, LiveStatus::Result);
}
} // namespace vibestudio
