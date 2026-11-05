#include "app/texture_export_panel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPainter>
#include <QPushButton>
#include <QResizeEvent>
#include <QSpinBox>

#include <limits>
#include <algorithm>

namespace vibestudio {
TextureExportPanel::TextureExportPanel(QWidget* parent) : QWidget(parent)
{
	setObjectName(QStringLiteral("textureExportPanel"));
	setAccessibleName(QCoreApplication::translate("VibeStudioTextureExport", "Texture export settings and preview"));
	m_form = new QFormLayout(this); m_form->setRowWrapPolicy(QFormLayout::WrapAllRows);
	m_form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	const auto edited = [this]() { if (!m_loading) { invalidatePreview(); updateFields(); if (changed) { changed(); } } };
	const auto combo = [&](const QString& label, const QString& name) {
		auto* field = new QComboBox; field->setObjectName(name); field->setAccessibleName(label);
		field->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon); field->setMinimumContentsLength(10);
		m_form->addRow(label, field); connect(field, &QComboBox::currentIndexChanged, this, edited); return field;
	};
	const auto text = [&](const QString& label, const QString& name, int limit) {
		auto* field = new QLineEdit; field->setObjectName(name); field->setAccessibleName(label); field->setMaxLength(limit);
		m_form->addRow(label, field); connect(field, &QLineEdit::textChanged, this, edited); return field;
	};
	const auto spin = [&](const QString& label, const QString& name, int low, int high) {
		auto* field = new QSpinBox; field->setObjectName(name); field->setAccessibleName(label); field->setRange(low, high); field->setKeyboardTracking(false);
		m_form->addRow(label, field); connect(field, &QSpinBox::valueChanged, this, edited); return field;
	};
	const auto check = [&](const QString& label, const QString& name) {
		auto* field = new QCheckBox(label); field->setObjectName(name); field->setAccessibleName(label);
		m_form->addRow(field); connect(field, &QCheckBox::toggled, this, edited); return field;
	};
	const auto label = [&](const QString& name, const QString& accessible) {
		auto* field = new QLabel; field->setObjectName(name); field->setAccessibleName(accessible); field->setWordWrap(true); field->setTextFormat(Qt::PlainText);
		field->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard); m_form->addRow(field); return field;
	};
	m_loading = true;
	m_profile = combo(QCoreApplication::translate("VibeStudioTextureExport", "Output profile"), QStringLiteral("textureExportProfile"));
	for (const auto& profile : textureExportProfiles()) { m_profile->addItem(profile.name, profile.id); }
	m_description = label(QStringLiteral("textureExportDescription"), QCoreApplication::translate("VibeStudioTextureExport", "Profile requirements"));
	m_alpha = combo(QCoreApplication::translate("VibeStudioTextureExport", "Alpha handling"), QStringLiteral("textureExportAlpha"));
	m_alpha->addItem(QCoreApplication::translate("VibeStudioTextureExport", "Preserve or reject"), QStringLiteral("strict"));
	m_alpha->addItem(QCoreApplication::translate("VibeStudioTextureExport", "Composite on matte"), QStringLiteral("matte"));
	m_alpha->addItem(QCoreApplication::translate("VibeStudioTextureExport", "Binary threshold"), QStringLiteral("threshold"));
	m_matte = text(QCoreApplication::translate("VibeStudioTextureExport", "Opaque matte color"), QStringLiteral("textureExportMatte"), 16);
	m_threshold = spin(QCoreApplication::translate("VibeStudioTextureExport", "Opaque from alpha"), QStringLiteral("textureExportThreshold"), 1, 255);
	m_dither = check(QCoreApplication::translate("VibeStudioTextureExport", "Dither palette conversion"), QStringLiteral("textureExportDither"));
	m_generated = check(QCoreApplication::translate("VibeStudioTextureExport", "Allow generated palette"), QStringLiteral("textureExportGenerated"));
	m_generated->setToolTip(QCoreApplication::translate("VibeStudioTextureExport", "Generated colors are stand-ins. Review palette provenance in the Palette section before game export."));
	m_filter = combo(QCoreApplication::translate("VibeStudioTextureExport", "Mipmap filter"), QStringLiteral("textureExportMipFilter"));
	m_filter->addItem(QCoreApplication::translate("VibeStudioTextureExport", "Area average"), QStringLiteral("box"));
	m_filter->addItem(QCoreApplication::translate("VibeStudioTextureExport", "Nearest pixel"), QStringLiteral("nearest"));
	m_fullbright = combo(QCoreApplication::translate("VibeStudioTextureExport", "Quake fullbright colors"), QStringLiteral("textureExportFullbright"));
	m_fullbright->addItem(QCoreApplication::translate("VibeStudioTextureExport", "Preserve authored indices"), QStringLiteral("preserve"));
	m_fullbright->addItem(QCoreApplication::translate("VibeStudioTextureExport", "Exclude"), QStringLiteral("exclude"));
	m_fullbright->addItem(QCoreApplication::translate("VibeStudioTextureExport", "Allow during conversion"), QStringLiteral("allow"));
	m_fullbright->setToolTip(QCoreApplication::translate("VibeStudioTextureExport", "Quake indices 224–255 glow without lighting. Preserve keeps matching indexed pixels; RGBA conversion excludes the band unless Allow is chosen."));
	m_name = text(QCoreApplication::translate("VibeStudioTextureExport", "Native texture name"), QStringLiteral("textureExportName"), 128);
	m_animation = text(QCoreApplication::translate("VibeStudioTextureExport", "Next WAL animation"), QStringLiteral("textureExportAnimation"), 128);
	m_flags = text(QCoreApplication::translate("VibeStudioTextureExport", "WAL surface flags"), QStringLiteral("textureExportFlags"), 16);
	m_contents = text(QCoreApplication::translate("VibeStudioTextureExport", "WAL content flags"), QStringLiteral("textureExportContents"), 16);
	for (auto* field : {m_flags, m_contents}) { field->setToolTip(QCoreApplication::translate("VibeStudioTextureExport", "Unsigned 32-bit decimal value, or hexadecimal with a 0x prefix.")); }
	m_value = spin(QCoreApplication::translate("VibeStudioTextureExport", "WAL surface value"), QStringLiteral("textureExportValue"), std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
	m_spriteType = combo(QCoreApplication::translate("VibeStudioTextureExport", "Sprite orientation"), QStringLiteral("textureExportSpriteType"));
	m_spriteType->addItem(QCoreApplication::translate("VibeStudioTextureExport", "Faces the view, stays upright"), QStringLiteral("parallel-upright"));
	m_spriteType->addItem(QCoreApplication::translate("VibeStudioTextureExport", "Faces the viewer, stays upright"), QStringLiteral("facing-upright"));
	m_spriteType->addItem(QCoreApplication::translate("VibeStudioTextureExport", "Faces the view"), QStringLiteral("parallel"));
	m_spriteType->addItem(QCoreApplication::translate("VibeStudioTextureExport", "Oriented by the entity"), QStringLiteral("oriented"));
	m_spriteType->addItem(QCoreApplication::translate("VibeStudioTextureExport", "Faces the view, entity roll"), QStringLiteral("parallel-oriented"));
	m_spriteTexture = combo(QCoreApplication::translate("VibeStudioTextureExport", "Half-Life sprite rendering"), QStringLiteral("textureExportSpriteTexture"));
	m_spriteTexture->addItem(QCoreApplication::translate("VibeStudioTextureExport", "Normal (opaque)"), QStringLiteral("normal"));
	m_spriteTexture->addItem(QCoreApplication::translate("VibeStudioTextureExport", "Additive"), QStringLiteral("additive"));
	m_spriteTexture->addItem(QCoreApplication::translate("VibeStudioTextureExport", "Index alpha (one color, alpha coverage)"), QStringLiteral("index-alpha"));
	m_spriteTexture->addItem(QCoreApplication::translate("VibeStudioTextureExport", "Alpha test (index 255 transparent)"), QStringLiteral("alpha-test"));
	m_randomSync = check(QCoreApplication::translate("VibeStudioTextureExport", "Random start frame"), QStringLiteral("textureExportRandomSync"));
	m_randomSync->setToolTip(QCoreApplication::translate("VibeStudioTextureExport", "Start each sprite in the level at a random frame instead of in step."));
	m_storedPalette = combo(QCoreApplication::translate("VibeStudioTextureExport", "Stored palette"), QStringLiteral("textureExportStoredPalette"));
	m_storedPalette->addItem(QCoreApplication::translate("VibeStudioTextureExport", "Built from the image"), QStringLiteral("image"));
	m_storedPalette->addItem(QCoreApplication::translate("VibeStudioTextureExport", "Selected game palette"), QStringLiteral("selected"));
	m_storedPalette->setToolTip(QCoreApplication::translate("VibeStudioTextureExport", "WAD3, Half-Life sprites and SWL carry their own 256 colors. Built palettes keep the image's colors as closely as 256 entries allow."));
	m_centered = check(QCoreApplication::translate("VibeStudioTextureExport", "Centre the origin"), QStringLiteral("textureExportCentered"));
	m_centered->setToolTip(QCoreApplication::translate("VibeStudioTextureExport", "Place the origin at each frame's centre. Off: the offsets below give the origin from the frame's top-left corner."));
	m_left = spin(QCoreApplication::translate("VibeStudioTextureExport", "Left offset"), QStringLiteral("textureExportLeft"), -32768, 32767);
	m_top = spin(QCoreApplication::translate("VibeStudioTextureExport", "Top offset"), QStringLiteral("textureExportTop"), -32768, 32767);
	for (auto* field : {m_left, m_top}) { field->setToolTip(QCoreApplication::translate("VibeStudioTextureExport", "Doom patch offsets, or the sprite origin measured from the frame's top-left corner.")); }
	m_doomSprite = check(QCoreApplication::translate("VibeStudioTextureExport", "Stage as a sprite frame"), QStringLiteral("textureExportDoomSprite"));
	m_doomSprite->setToolTip(QCoreApplication::translate("VibeStudioTextureExport", "Stage the patch between S_START and S_END instead of P_START and P_END, for sprite frames such as TROOA1."));
	m_wall = text(QCoreApplication::translate("VibeStudioTextureExport", "Define wall texture"), QStringLiteral("textureExportWallTexture"), 8);
	m_wall->setToolTip(QCoreApplication::translate("VibeStudioTextureExport", "When staging the patch, also define this TEXTURE1 wall texture as the patch at 0, 0, adding it to PNAMES. Leave empty to stage the patch only."));
	m_extended = check(QCoreApplication::translate("VibeStudioTextureExport", "Use source-port limits"), QStringLiteral("textureExportExtended"));
	m_extended->setToolTip(QCoreApplication::translate("VibeStudioTextureExport", "Permit tall Doom posts and larger miptextures. Verify the target engine and compiler; warnings still report known limits."));
	const auto button = [&](const QString& caption, const QString& name, auto callback) {
		auto* field = new QPushButton(caption); field->setObjectName(name); field->setAccessibleName(caption); field->setAutoDefault(false);
		m_form->addRow(field); connect(field, &QPushButton::clicked, this, callback);
	};
	button(QCoreApplication::translate("VibeStudioTextureExport", "Preview and Validate"), QStringLiteral("previewTextureExport"), [this]() { if (previewRequested) { previewRequested(); } });
	button(QCoreApplication::translate("VibeStudioTextureExport", "Export File…"), QStringLiteral("saveTextureExport"), [this]() { if (exportRequested) { exportRequested(); } });
	m_mip = new QComboBox; m_mip->setObjectName(QStringLiteral("textureExportMipLevel"));
	m_mip->setAccessibleName(QCoreApplication::translate("VibeStudioTextureExport", "Preview mip level")); m_form->addRow(m_mip->accessibleName(), m_mip);
	connect(m_mip, &QComboBox::currentIndexChanged, this, [this]() { updatePreview(); });
	m_preview = label(QStringLiteral("textureExportPreview"), QCoreApplication::translate("VibeStudioTextureExport", "Encoded texture preview"));
	m_preview->setAlignment(Qt::AlignCenter); m_preview->setMinimumWidth(1); m_preview->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
	m_report = label(QStringLiteral("textureExportReport"), QCoreApplication::translate("VibeStudioTextureExport", "Export validation and palette provenance"));
	setOptions({});
}

QJsonObject TextureExportPanel::settings() const
{
	const auto flags = [](const QLineEdit* field) -> QJsonValue {
		const auto text = field->text().trimmed(); bool ok = false;
		const auto value = text.toULongLong(&ok, text.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive) ? 16 : 10);
		return ok && value <= 0xffffffffull ? QJsonValue(double(value)) : QJsonValue(text);
	};
	return {{QStringLiteral("version"), 1}, {QStringLiteral("profile"), m_profile->currentData().toString()}, {QStringLiteral("alpha"), m_alpha->currentData().toString()},
		{QStringLiteral("matte"), m_matte->text()}, {QStringLiteral("alphaThreshold"), m_threshold->value()}, {QStringLiteral("dither"), m_dither->isChecked()},
		{QStringLiteral("allowGeneratedPalette"), m_generated->isChecked()}, {QStringLiteral("extendedLimits"), m_extended->isChecked()}, {QStringLiteral("mipFilter"), m_filter->currentData().toString()},
		{QStringLiteral("fullbright"), m_fullbright->currentData().toString()}, {QStringLiteral("name"), m_name->text()}, {QStringLiteral("animationNext"), m_animation->text()},
		{QStringLiteral("surfaceFlags"), flags(m_flags)}, {QStringLiteral("contentFlags"), flags(m_contents)}, {QStringLiteral("surfaceValue"), m_value->value()},
		{QStringLiteral("leftOffset"), m_left->value()}, {QStringLiteral("topOffset"), m_top->value()},
		{QStringLiteral("spriteType"), m_spriteType->currentData().toString()}, {QStringLiteral("spriteTexture"), m_spriteTexture->currentData().toString()},
		{QStringLiteral("spriteRandomSync"), m_randomSync->isChecked()}, {QStringLiteral("centeredOrigin"), m_centered->isChecked()},
		{QStringLiteral("embeddedPalette"), m_storedPalette->currentData().toString()}, {QStringLiteral("doomSprite"), m_doomSprite->isChecked()},
		{QStringLiteral("wallTexture"), m_wall->text().trimmed().toUpper()}};
}
bool TextureExportPanel::options(TextureExportOptions* options, QString* error) const { return textureExportOptionsFromJson(settings(), options, error); }
void TextureExportPanel::setOptions(const TextureExportOptions& options)
{
	m_loading = true; const auto object = textureExportOptionsJson(options);
	m_profile->setCurrentIndex(m_profile->findData(object.value(QStringLiteral("profile")).toString()));
	m_alpha->setCurrentIndex(m_alpha->findData(object.value(QStringLiteral("alpha")).toString()));
	m_filter->setCurrentIndex(m_filter->findData(object.value(QStringLiteral("mipFilter")).toString()));
	m_fullbright->setCurrentIndex(m_fullbright->findData(object.value(QStringLiteral("fullbright")).toString()));
	m_matte->setText(options.matte.name(QColor::HexArgb)); m_threshold->setValue(options.alphaThreshold); m_dither->setChecked(options.dither);
	m_generated->setChecked(options.allowGeneratedPalette); m_extended->setChecked(options.extendedLimits); m_name->setText(options.name); m_animation->setText(options.animationNext);
	m_flags->setText(QString::number(options.surfaceFlags)); m_contents->setText(QString::number(options.contentFlags)); m_value->setValue(options.surfaceValue);
	m_left->setValue(options.leftOffset); m_top->setValue(options.topOffset);
	m_spriteType->setCurrentIndex(m_spriteType->findData(object.value(QStringLiteral("spriteType")).toString()));
	m_spriteTexture->setCurrentIndex(m_spriteTexture->findData(object.value(QStringLiteral("spriteTexture")).toString()));
	m_storedPalette->setCurrentIndex(m_storedPalette->findData(object.value(QStringLiteral("embeddedPalette")).toString()));
	m_randomSync->setChecked(options.spriteRandomSync); m_centered->setChecked(options.centeredOrigin); m_doomSprite->setChecked(options.doomSprite); m_wall->setText(options.wallTexture);
	m_loading = false; updateFields(); invalidatePreview();
}
void TextureExportPanel::updateFields()
{
	const auto profiles = textureExportProfiles(); const int index = m_profile->currentIndex(); if (index < 0 || index >= profiles.size()) { return; }
	const auto& profile = profiles[index]; const bool quake = profile.format == TextureExportFormat::QuakeMiptex || profile.format == TextureExportFormat::QuakeWad2;
	const bool wal = profile.format == TextureExportFormat::Quake2Wal, patch = profile.format == TextureExportFormat::DoomPatch;
	const bool swl = profile.format == TextureExportFormat::SinSwl, quake2Sprite = profile.format == TextureExportFormat::Quake2Sprite;
	const bool halfLifeSprite = profile.format == TextureExportFormat::HalfLifeSprite, idspSprite = profile.format == TextureExportFormat::QuakeSprite || halfLifeSprite;
	const bool ownPalette = swl || halfLifeSprite || profile.format == TextureExportFormat::HalfLifeMiptex || profile.format == TextureExportFormat::HalfLifeWad3;
	m_description->setText(profile.description);
	const auto visible = [this](QWidget* field, bool show) { m_form->setRowVisible(field, show); };
	visible(m_matte, m_alpha->currentData() == QStringLiteral("matte")); visible(m_threshold, m_alpha->currentData() == QStringLiteral("threshold"));
	const bool selectedPalette = profile.indexed && (!ownPalette || m_storedPalette->currentData() == QStringLiteral("selected"));
	visible(m_dither, profile.indexed); visible(m_generated, selectedPalette); visible(m_filter, profile.mipmapped); visible(m_fullbright, quake);
	visible(m_name, profile.mipmapped || quake2Sprite); visible(m_animation, wal || swl); visible(m_flags, wal || swl); visible(m_contents, wal || swl); visible(m_value, wal || swl);
	visible(m_spriteType, idspSprite); visible(m_spriteTexture, halfLifeSprite); visible(m_randomSync, idspSprite); visible(m_storedPalette, ownPalette);
	visible(m_centered, profile.frames); visible(m_left, patch || (profile.frames && !m_centered->isChecked())); visible(m_top, patch || (profile.frames && !m_centered->isChecked()));
	visible(m_doomSprite, patch); visible(m_wall, patch && !m_doomSprite->isChecked());
	visible(m_extended, quake || wal || patch || quake2Sprite);
	m_name->setToolTip(quake2Sprite ? QCoreApplication::translate("VibeStudioTextureExport", "Package path the PCX frames are named from: sprites/flame writes sprites/flame_0.pcx and onwards.") :
		QCoreApplication::translate("VibeStudioTextureExport", "The name stored in the texture. WAD lumps must use the same name."));
}
void TextureExportPanel::invalidatePreview()
{
	m_images.clear(); m_mip->clear(); m_mip->setEnabled(false); m_preview->clear();
	m_preview->setAccessibleDescription(QCoreApplication::translate("VibeStudioTextureExport", "No current export preview."));
	m_report->setText(QCoreApplication::translate("VibeStudioTextureExport", "Preview required for the current pixels, palette, and settings."));
}
void TextureExportPanel::showResult(const TextureExportResult& result, const IdTechPaletteResolution& palette)
{
	invalidatePreview();
	if (!result.succeeded) { m_report->setText(result.error); return; }
	m_framesShown = !result.frames.isEmpty();
	m_images = m_framesShown ? result.frames : (result.mipLevels.isEmpty() ? QVector<QImage>{result.preview} : result.mipLevels);
	for (int level = 0; level < m_images.size(); ++level) {
		const auto& image = m_images[level];
		m_mip->addItem(m_framesShown ? QCoreApplication::translate("VibeStudioTextureExport", "Frame %1 · %2 × %3").arg(level + 1).arg(image.width()).arg(image.height()) :
			QCoreApplication::translate("VibeStudioTextureExport", "Level %1 · %2 × %3").arg(level).arg(image.width()).arg(image.height()));
	}
	m_mip->setEnabled(m_images.size() > 1); updatePreview();
	const QLocale locale;
	QStringList lines{QCoreApplication::translate("VibeStudioTextureExport", "Size: %1 · RGB changes: %2 · Alpha changes: %3")
		.arg(locale.formattedDataSize(result.bytes.size()), locale.toString(result.colorChangedPixels), locale.toString(result.alphaChangedPixels))};
	TextureExportOptions options; this->options(&options);
	const bool ownPalette = !result.embeddedPalette.isEmpty() && options.imagePalette;
	if (ownPalette) { lines << QCoreApplication::translate("VibeStudioTextureExport", "Palette: built from the image and stored with it."); }
	else if (options.format != TextureExportFormat::Png && options.format != TextureExportFormat::Targa && options.format != TextureExportFormat::Dds &&
		options.format != TextureExportFormat::DdsMipmapped && options.format != TextureExportFormat::Ftx) {
		lines << idTechPaletteSummaryLines(palette);
		if (result.preservedIndices) { lines << QCoreApplication::translate("VibeStudioTextureExport", "Matching source indices preserved."); }
	}
	for (const auto& companion : result.companions) { lines << QCoreApplication::translate("VibeStudioTextureExport", "Also writes %1 (%2)").arg(companion.path, locale.formattedDataSize(companion.bytes.size())); }
	lines << result.warnings; m_report->setText(lines.join(QLatin1Char('\n')));
}
QImage TextureExportPanel::previewImage() const { return m_images.value(m_mip->currentIndex()); }
void TextureExportPanel::updatePreview()
{
	if (!m_preview) { return; }
	m_preview->setFixedHeight(fontMetrics().height() * 10);
	const auto image = previewImage(); if (image.isNull()) { return; }
	const QSize size = image.size().scaled(QSize(std::max(1, m_preview->width()), m_preview->height()), Qt::KeepAspectRatio);
	QPixmap pixmap(size); QPainter painter(&pixmap);
	const int cell = std::max(4, fontMetrics().height() / 2);
	for (int y = 0; y < size.height(); y += cell) { for (int x = 0; x < size.width(); x += cell) { painter.fillRect(QRect(x, y, cell, cell), ((x / cell + y / cell) % 2) ? palette().color(QPalette::Base) : palette().color(QPalette::AlternateBase)); } }
	painter.drawImage(QRect(QPoint(), size), image); painter.end(); m_preview->setPixmap(pixmap);
	m_preview->setAccessibleDescription(m_framesShown ? QCoreApplication::translate("VibeStudioTextureExport", "Encoded frame %1, %2 by %3 pixels. Checkerboard shows transparency.").arg(m_mip->currentIndex() + 1).arg(image.width()).arg(image.height()) :
		QCoreApplication::translate("VibeStudioTextureExport", "Encoded level %1, %2 by %3 pixels. Checkerboard shows transparency.").arg(m_mip->currentIndex()).arg(image.width()).arg(image.height()));
}
void TextureExportPanel::resizeEvent(QResizeEvent* event) { QWidget::resizeEvent(event); updatePreview(); }
} // namespace vibestudio
