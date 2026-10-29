// The Mesh Editor's Game preview (UVSKIN-08): Shading > Game draws each
// surface with what the engine would put on it (core/model_game_preview.h),
// with the renderer, player colours and flood fill chosen here and the time
// running while the preview animates (held still under reduced motion).
#include "app/model_editor_dialog.h"
#include "app/model_viewport.h"
#include "core/material_library.h"
#include "core/material_script.h"
#include "core/model_game_preview.h"

#include <QBoxLayout>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QLabel>
#include <QPointer>
#include <QSpinBox>
#include <QThread>
#include <QTimer>

namespace vibestudio
{

struct ModelEditorDialog::GameShaders
{
	QString key;
	QHash<int, MaterialDefinition> definitions;
	QHash<int, MaterialImageSet> images;
};

namespace
{
// The material name a surface asks for: its first skin path without the
// image extension, as Quake III looks shaders up.
QString shaderName(const ModelSurface &surface)
{
	QString name = surface.skinPaths.value(0);
	name.replace(QLatin1Char('\\'), QLatin1Char('/'));
	const int dot = int(name.lastIndexOf(QLatin1Char('.')));
	if (dot > int(name.lastIndexOf(QLatin1Char('/'))))
		name.truncate(dot);
	return name;
}
} // namespace

void ModelEditorDialog::addGamePreviewControls(QBoxLayout *row)
{
	m_gameRow = new QWidget;
	m_gameRow->setObjectName(QStringLiteral("meshGamePreview"));
	m_gameRow->setAccessibleName(QCoreApplication::translate("VibeStudioModelEditor", "Game preview"));
	auto *layout = new QHBoxLayout(m_gameRow);
	layout->setContentsMargins(0, 0, 0, 0);
	m_gameRenderer = new QComboBox;
	m_gameRenderer->setObjectName(QStringLiteral("meshGameRenderer"));
	m_gameRenderer->setAccessibleName(QCoreApplication::translate("VibeStudioModelEditor", "Quake renderer"));
	m_gameRenderer->setToolTip(QCoreApplication::translate("VibeStudioModelEditor",
		"How a Quake model's skin is drawn: GLQuake lights fullbright colours like the rest; modern ports keep them bright; "
		"the software renderer also samples point-wise."));
	m_gameRenderer->addItem(QCoreApplication::translate("VibeStudioModelEditor", "Modern port"), int(MaterialQuakeRenderer::ModernPort));
	m_gameRenderer->addItem(QCoreApplication::translate("VibeStudioModelEditor", "GLQuake"), int(MaterialQuakeRenderer::GLQuake));
	m_gameRenderer->addItem(QCoreApplication::translate("VibeStudioModelEditor", "Software"), int(MaterialQuakeRenderer::Software));
	layout->addWidget(m_gameRenderer);
	const auto colour = [&](const QString &name, const QString &accessible, const QString &prefix) {
		auto *spin = new QSpinBox;
		spin->setObjectName(name);
		spin->setRange(-1, 13);
		spin->setValue(-1);
		// -1 shows the skin's own colours.
		spin->setSpecialValueText(QCoreApplication::translate("VibeStudioModelEditor", "Skin colours"));
		spin->setPrefix(prefix);
		spin->setAccessibleName(accessible);
		spin->setToolTip(QCoreApplication::translate("VibeStudioModelEditor", "A Quake player colour row (0 to 13), as the color command sets it."));
		layout->addWidget(spin);
		return spin;
	};
	m_gameTop = colour(QStringLiteral("meshGameTopColour"), QCoreApplication::translate("VibeStudioModelEditor", "Player top colour"),
		QCoreApplication::translate("VibeStudioModelEditor", "Top "));
	m_gameBottom = colour(QStringLiteral("meshGameBottomColour"), QCoreApplication::translate("VibeStudioModelEditor", "Player bottom colour"),
		QCoreApplication::translate("VibeStudioModelEditor", "Bottom "));
	m_gameFloodFill = new QCheckBox(QCoreApplication::translate("VibeStudioModelEditor", "Flood fill"));
	m_gameFloodFill->setObjectName(QStringLiteral("meshGameFloodFill"));
	m_gameFloodFill->setAccessibleName(QCoreApplication::translate("VibeStudioModelEditor", "GLQuake flood fill"));
	m_gameFloodFill->setChecked(true);
	m_gameFloodFill->setToolTip(QCoreApplication::translate("VibeStudioModelEditor",
		"GLQuake and the GL ports fill the skin's background colour region from its edges when they load it; the software renderer does not."));
	layout->addWidget(m_gameFloodFill);
	m_gameStatus = new QLabel;
	m_gameStatus->setObjectName(QStringLiteral("meshGameStatus"));
	m_gameStatus->setTextFormat(Qt::PlainText);
	m_gameStatus->setAccessibleName(QCoreApplication::translate("VibeStudioModelEditor", "Game preview status"));
	m_gameStatus->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	layout->addWidget(m_gameStatus, 1);
	m_gameTimer = new QTimer(this);
	m_gameTimer->setInterval(100);
	connect(m_gameTimer, &QTimer::timeout, this, [this] { refreshGamePreview(); });
	connect(m_gameRenderer, &QComboBox::currentIndexChanged, this, [this] { refreshGamePreview(); });
	connect(m_gameTop, &QSpinBox::valueChanged, this, [this] { refreshGamePreview(); });
	connect(m_gameBottom, &QSpinBox::valueChanged, this, [this] { refreshGamePreview(); });
	connect(m_gameFloodFill, &QCheckBox::toggled, this, [this] { refreshGamePreview(); });
	m_gameRow->hide();
	row->addWidget(m_gameRow, 2);
}

void ModelEditorDialog::resolveGameShaders()
{
	const auto &mesh = m_document.mesh();
	if (!m_materialSource.archive || (mesh.mdl.enabled && !mesh.embeddedSkins.isEmpty()))
		return;
	QStringList names;
	for (const auto &surface : mesh.surfaces)
		names << shaderName(surface);
	const QString key = m_materialSource.revision + QLatin1Char('|') + names.join(QLatin1Char('|'));
	if (m_gameShaders && m_gameShaders->key == key)
		return;
	m_gameShaders = std::make_shared<GameShaders>();
	m_gameShaders->key = key;
	// The library scan and image reads run on a worker; the result arrives
	// as a whole, so a half-read set never shows.
	const auto source = m_materialSource;
	const QPointer<ModelEditorDialog> self(this);
	auto *thread = QThread::create([self, source, names, key] {
		auto shaders = std::make_shared<GameShaders>();
		shaders->key = key;
		MaterialLibraryOptions options;
		options.engines = {MaterialEngine::Quake3};
		const MaterialLibrary library = scanMaterialLibrary(*source.archive, options);
		MaterialImageSource images;
		images.archive = source.archive;
		images.revision = source.revision;
		images.paletteId = source.paletteId;
		images.maximumDimension = 512;
		for (int surface = 0; surface < names.size(); ++surface) {
			if (names.at(surface).isEmpty())
				continue;
			const int entry = library.indexOf(names.at(surface), MaterialEngine::Quake3);
			const MaterialDefinition definition = entry >= 0 ? library.definition(entry)
															 : implicitQuake3Material(names.at(surface), MaterialSurfaceContext::Model);
			shaders->definitions.insert(surface, definition);
			shaders->images.insert(surface, resolveMaterialImages(definition, images));
		}
		QMetaObject::invokeMethod(
			qApp, [self, shaders] {
				if (self && self->m_gameShaders && self->m_gameShaders->key == shaders->key) {
					self->m_gameShaders = shaders;
					self->refreshGamePreview();
				}
			},
			Qt::QueuedConnection);
	});
	connect(thread, &QThread::finished, thread, &QObject::deleteLater);
	thread->start();
}

void ModelEditorDialog::refreshGamePreview()
{
	const bool active = m_renderMode && m_renderMode->currentIndex() == 4;
	if (m_gameRow)
		m_gameRow->setVisible(active);
	auto views = panes();
	if (!views.contains(m_preview))
		views.append(m_preview);
	if (!active) {
		if (m_gameTimer)
			m_gameTimer->stop();
		for (auto *pane : std::as_const(views))
			if (pane && pane->gameSkinsActive())
				pane->setGameSkins({}, {}, false);
		return;
	}
	resolveGameShaders();
	const auto &mesh = m_document.mesh();
	ModelGamePreviewOptions options;
	options.quakeRenderer = MaterialQuakeRenderer(m_gameRenderer->currentData().toInt());
	options.topColor = m_gameTop->value();
	options.bottomColor = m_gameBottom->value();
	options.floodFill = m_gameFloodFill->isChecked();
	// Time runs while the preview animates; reduced motion holds it at zero.
	const bool still = m_preview->reducedMotion();
	if (!m_gameClock.isValid())
		m_gameClock.start();
	options.time = still ? 0.0 : double(m_gameClock.elapsed()) / 1000.0;
	QHash<int, QImage> skins, glows;
	QStringList notes;
	bool nearest = false;
	bool animated = false;
	for (int surface = 0; surface < mesh.surfaces.size(); ++surface) {
		const MaterialDefinition *definition = m_gameShaders && m_gameShaders->definitions.contains(surface) ? &m_gameShaders->definitions[surface] : nullptr;
		const MaterialImageSet *images = m_gameShaders && m_gameShaders->images.contains(surface) ? &m_gameShaders->images[surface] : nullptr;
		ModelGameSkin skin;
		QString error;
		if (!composeModelGameSkin(mesh, surface, definition, images, m_surfaceImages.value(surface), options, &skin, &error)) {
			if (!error.isEmpty() && !notes.contains(error))
				notes << error;
			continue;
		}
		skins.insert(surface, skin.base);
		if (!skin.glow.isNull())
			glows.insert(surface, skin.glow);
		nearest = nearest || skin.nearest;
		for (const QString &note : std::as_const(skin.notes))
			if (!notes.contains(note))
				notes << note;
		animated = animated || skin.frame >= 0;
		if (definition) {
			for (const auto &stage : definition->stages)
				animated = animated || !stage.tcMods.isEmpty() || stage.rgbGen.source == MaterialColorSource::Wave
						   || stage.alphaGen.source == MaterialColorSource::Wave;
		}
	}
	for (auto *pane : std::as_const(views))
		if (pane)
			pane->setGameSkins(skins, glows, nearest);
	if (animated && !still) {
		if (!m_gameTimer->isActive())
			m_gameTimer->start();
	} else {
		m_gameTimer->stop();
	}
	const QString summary = skins.isEmpty() ? QCoreApplication::translate("VibeStudioModelEditor", "Nothing to draw as the game would; Material images are shown.")
											: notes.join(QLatin1Char(' '));
	if (m_gameStatus->text() != summary) {
		m_gameStatus->setText(summary);
		m_gameStatus->setToolTip(summary);
		m_gameStatus->setAccessibleDescription(summary);
	}
}

} // namespace vibestudio
