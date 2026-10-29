#include "app/model_bake_dialog.h"

#include "core/model_document.h"
#include "core/texture_export.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QSaveFile>
#include <QSpinBox>
#include <QThread>
#include <QVBoxLayout>

namespace vibestudio
{

ModelBakeDialog::ModelBakeDialog(const ModelMesh &mesh, int surface, int frame, QWidget *parent)
	: QDialog(parent), m_mesh(mesh), m_surface(surface), m_frame(frame)
{
	setObjectName(QStringLiteral("modelBakeDialog"));
	setWindowTitle(QCoreApplication::translate("VibeStudioModelBake", "Bake Maps"));
	setAccessibleName(windowTitle());
	auto *layout = new QVBoxLayout(this);
	auto *form = new QFormLayout;
	m_type = new QComboBox;
	m_type->setObjectName(QStringLiteral("modelBakeType"));
	m_type->setAccessibleName(QCoreApplication::translate("VibeStudioModelBake", "Map to bake"));
	for (const auto type : {ModelBakeType::AmbientOcclusion, ModelBakeType::TangentNormal, ModelBakeType::ObjectNormal, ModelBakeType::Lighting})
		m_type->addItem(modelBakeTypeDisplayName(type), int(type));
	form->addRow(QCoreApplication::translate("VibeStudioModelBake", "&Map"), m_type);
	m_scope = new QComboBox;
	m_scope->setObjectName(QStringLiteral("modelBakeScope"));
	m_scope->setAccessibleName(QCoreApplication::translate("VibeStudioModelBake", "Surfaces to bake"));
	m_scope->addItem(QCoreApplication::translate("VibeStudioModelBake", "Active surface"));
	m_scope->addItem(QCoreApplication::translate("VibeStudioModelBake", "Every surface"));
	form->addRow(QCoreApplication::translate("VibeStudioModelBake", "S&urfaces"), m_scope);
	m_size = new QComboBox;
	m_size->setObjectName(QStringLiteral("modelBakeSize"));
	m_size->setAccessibleName(QCoreApplication::translate("VibeStudioModelBake", "Map size in texels"));
	for (const int side : {64, 128, 256, 512, 1024, 2048})
		m_size->addItem(QStringLiteral("%1 × %1").arg(QLocale().toString(side)), side);
	m_size->setCurrentIndex(2);
	form->addRow(QCoreApplication::translate("VibeStudioModelBake", "Si&ze"), m_size);
	m_samples = new QSpinBox;
	m_samples->setObjectName(QStringLiteral("modelBakeSamples"));
	m_samples->setRange(1, 1024);
	m_samples->setValue(64);
	m_samples->setAccessibleName(QCoreApplication::translate("VibeStudioModelBake", "Occlusion rays per texel"));
	form->addRow(QCoreApplication::translate("VibeStudioModelBake", "&Rays"), m_samples);
	m_distance = new QDoubleSpinBox;
	m_distance->setObjectName(QStringLiteral("modelBakeDistance"));
	m_distance->setRange(0, 100000);
	m_distance->setDecimals(2);
	m_distance->setSpecialValueText(QCoreApplication::translate("VibeStudioModelBake", "Automatic"));
	m_distance->setAccessibleName(QCoreApplication::translate("VibeStudioModelBake", "Occlusion distance"));
	form->addRow(QCoreApplication::translate("VibeStudioModelBake", "&Distance"), m_distance);
	m_high = new QLineEdit;
	m_high->setObjectName(QStringLiteral("modelBakeHigh"));
	m_high->setPlaceholderText(QCoreApplication::translate("VibeStudioModelBake", "None: bake from this model"));
	m_high->setAccessibleName(QCoreApplication::translate("VibeStudioModelBake", "High-poly model"));
	m_browse = new QPushButton(QCoreApplication::translate("VibeStudioModelBake", "Browse…"));
	m_browse->setObjectName(QStringLiteral("modelBakeBrowse"));
	m_browse->setAccessibleName(QCoreApplication::translate("VibeStudioModelBake", "Choose a high-poly model"));
	auto *highRow = new QHBoxLayout;
	highRow->addWidget(m_high, 1);
	highRow->addWidget(m_browse);
	form->addRow(QCoreApplication::translate("VibeStudioModelBake", "&High-poly"), highRow);
	m_cage = new QDoubleSpinBox;
	m_cage->setObjectName(QStringLiteral("modelBakeCage"));
	m_cage->setRange(0, 100000);
	m_cage->setDecimals(2);
	m_cage->setSpecialValueText(QCoreApplication::translate("VibeStudioModelBake", "Automatic"));
	m_cage->setAccessibleName(QCoreApplication::translate("VibeStudioModelBake", "Cage distance"));
	form->addRow(QCoreApplication::translate("VibeStudioModelBake", "&Cage"), m_cage);
	m_padding = new QSpinBox;
	m_padding->setObjectName(QStringLiteral("modelBakePadding"));
	m_padding->setRange(0, 64);
	m_padding->setValue(4);
	m_padding->setAccessibleName(QCoreApplication::translate("VibeStudioModelBake", "Padding in texels"));
	form->addRow(QCoreApplication::translate("VibeStudioModelBake", "&Padding"), m_padding);
	m_seed = new QSpinBox;
	m_seed->setObjectName(QStringLiteral("modelBakeSeed"));
	m_seed->setRange(0, 0x7fffffff);
	m_seed->setValue(1);
	m_seed->setAccessibleName(QCoreApplication::translate("VibeStudioModelBake", "Random seed"));
	form->addRow(QCoreApplication::translate("VibeStudioModelBake", "S&eed"), m_seed);
	m_strength = new QDoubleSpinBox;
	m_strength->setObjectName(QStringLiteral("modelBakeStrength"));
	m_strength->setRange(0.0, 1.0);
	m_strength->setSingleStep(0.1);
	m_strength->setValue(1.0);
	m_strength->setAccessibleName(QCoreApplication::translate("VibeStudioModelBake", "How strongly Apply to Model shades the skin"));
	form->addRow(QCoreApplication::translate("VibeStudioModelBake", "Shade s&trength"), m_strength);
	layout->addLayout(form);
	auto *strip = new QHBoxLayout;
	m_status = new QLabel(QCoreApplication::translate("VibeStudioModelBake", "Choose a map, then Bake."));
	m_status->setObjectName(QStringLiteral("modelBakeStatus"));
	m_status->setWordWrap(true);
	m_status->setTextFormat(Qt::PlainText);
	m_status->setAccessibleName(QCoreApplication::translate("VibeStudioModelBake", "Bake status"));
	m_progress = new QProgressBar;
	m_progress->setObjectName(QStringLiteral("modelBakeProgress"));
	m_progress->setAccessibleName(QCoreApplication::translate("VibeStudioModelBake", "Bake progress"));
	m_progress->setRange(0, 1);
	m_progress->setValue(0);
	strip->addWidget(m_status, 1);
	strip->addWidget(m_progress);
	layout->addLayout(strip);
	m_preview = new QLabel;
	m_preview->setObjectName(QStringLiteral("modelBakePreview"));
	m_preview->setAccessibleName(QCoreApplication::translate("VibeStudioModelBake", "Baked map preview"));
	m_preview->setMinimumSize(192, 192);
	m_preview->setAlignment(Qt::AlignCenter);
	layout->addWidget(m_preview, 1);
	auto *buttons = new QDialogButtonBox;
	m_bake = buttons->addButton(QCoreApplication::translate("VibeStudioModelBake", "&Bake"), QDialogButtonBox::ActionRole);
	m_bake->setObjectName(QStringLiteral("modelBakeStart"));
	m_bake->setDefault(true);
	m_cancel = buttons->addButton(QCoreApplication::translate("VibeStudioModelBake", "Cancel &Bake"), QDialogButtonBox::ActionRole);
	m_cancel->setObjectName(QStringLiteral("modelBakeCancel"));
	m_save = buttons->addButton(QCoreApplication::translate("VibeStudioModelBake", "&Save Map…"), QDialogButtonBox::ActionRole);
	m_save->setObjectName(QStringLiteral("modelBakeSave"));
	m_apply = buttons->addButton(QCoreApplication::translate("VibeStudioModelBake", "&Apply to Model"), QDialogButtonBox::ActionRole);
	m_apply->setObjectName(QStringLiteral("modelBakeApply"));
	m_close = buttons->addButton(QDialogButtonBox::Close);
	m_close->setObjectName(QStringLiteral("modelBakeClose"));
	for (auto *button : {m_bake, m_cancel, m_save, m_apply, m_close})
		button->setAccessibleName(button->text().remove(QLatin1Char('&')));
	setApplyTarget(QCoreApplication::translate("VibeStudioModelBake", "This surface has no skin in the model or the open package to hand the map to; use Save Map…"), {});
	connect(m_apply, &QPushButton::clicked, this, [this] { applyToModel(); });
	layout->addWidget(buttons);
	connect(m_bake, &QPushButton::clicked, this, [this] { start(); });
	connect(m_cancel, &QPushButton::clicked, this, [this] { cancel(); });
	connect(m_close, &QPushButton::clicked, this, [this] { reject(); });
	connect(m_browse, &QPushButton::clicked, this, [this] {
		const auto path = QFileDialog::getOpenFileName(this, QCoreApplication::translate("VibeStudioModelBake", "Choose a High-Poly Model"), m_high->text(),
			QCoreApplication::translate("VibeStudioModelBake", "Models (*.mesh.json *.obj *.lwo *.ase *.md3 *.md5mesh *.iqm *.gltf *.glb);;All files (*)"));
		if (!path.isEmpty())
			m_high->setText(QDir::toNativeSeparators(path));
	});
	connect(m_save, &QPushButton::clicked, this, [this] {
		const auto path = QFileDialog::getSaveFileName(this, QCoreApplication::translate("VibeStudioModelBake", "Save Baked Map"), QString(),
			QCoreApplication::translate("VibeStudioModelBake", "PNG images (*.png);;Targa images (*.tga)"));
		if (!path.isEmpty())
			saveTo(path);
	});
	setRunning(false);
}

ModelBakeDialog::~ModelBakeDialog()
{
	waitForWorker();
}

void ModelBakeDialog::waitForWorker()
{
	if (!m_thread)
		return;
	if (m_cancelled)
		m_cancelled->store(true);
	m_thread->wait();
	delete m_thread;
	m_thread = nullptr;
}

void ModelBakeDialog::setRunning(bool running)
{
	for (QWidget *widget : std::initializer_list<QWidget *>{m_type, m_scope, m_size, m_samples, m_distance, m_high, m_browse, m_cage, m_padding, m_seed, m_bake})
		widget->setEnabled(!running);
	m_cancel->setEnabled(running);
	m_save->setEnabled(!running && !m_result.image.isNull());
	m_apply->setEnabled(!running && !m_result.image.isNull() && bool(m_applyHandler));
	setProperty("operationRunning", running);
}

bool ModelBakeDialog::start()
{
	if (m_thread)
		return false;
	ModelBakeOptions options;
	options.type = ModelBakeType(m_type->currentData().toInt());
	const int side = m_size->currentData().toInt();
	options.size = QSize(side, side);
	options.samples = m_samples->value();
	options.maxDistance = m_distance->value();
	options.cageDistance = m_cage->value();
	options.padding = m_padding->value();
	options.seed = quint32(m_seed->value());
	m_lastOptions = options;
	const QVector<int> surfaces = m_scope->currentIndex() == 0 ? QVector<int>{m_surface} : QVector<int>{};
	const QString highPath = m_high->text().trimmed();
	m_cancelled = std::make_shared<std::atomic_bool>(false);
	m_progress->setRange(0, 0);
	m_status->setText(QCoreApplication::translate("VibeStudioModelBake", "Baking %1…").arg(modelBakeTypeDisplayName(options.type)));
	setRunning(true);
	const QPointer<ModelBakeDialog> self(this);
	const auto cancelled = m_cancelled;
	const ModelMesh mesh = m_mesh;
	const int frame = m_frame;
	m_thread = QThread::create([self, mesh, frame, surfaces, highPath, options, cancelled] {
		ModelWorkControl control;
		control.cancelled = [cancelled] { return cancelled->load(); };
		auto result = std::make_shared<ModelBakeResult>();
		QString error;
		bool ok = true;
		ModelMesh high;
		if (!highPath.isEmpty())
		{
			QFile file(highPath);
			if (!file.open(QIODevice::ReadOnly) || file.size() > modelDocumentMaxSourceBytes)
			{
				ok = false;
				error = QCoreApplication::translate("VibeStudioModelBake", "The high-poly model cannot be read: %1").arg(highPath);
			}
			else
			{
				ok = importEditableModel(QFileInfo(highPath).absoluteFilePath(), file.readAll(), &high, &error, nullptr, control);
			}
		}
		if (ok)
			ok = bakeModelMaps(mesh, surfaces, frame, highPath.isEmpty() ? nullptr : &high, options, result.get(), &error, control);
		QMetaObject::invokeMethod(
			qApp, [self, result, error, ok] {
				if (self)
					self->finished(*result, error, ok);
			},
			Qt::QueuedConnection);
	});
	m_thread->start();
	return true;
}

void ModelBakeDialog::cancel()
{
	if (m_thread && m_cancelled)
	{
		m_cancelled->store(true);
		m_cancel->setEnabled(false);
		m_status->setText(QCoreApplication::translate("VibeStudioModelBake", "Cancelling the bake…"));
	}
}

void ModelBakeDialog::finished(const ModelBakeResult &result, const QString &error, bool ok)
{
	const bool cancelled = m_cancelled && m_cancelled->load();
	if (m_thread)
	{
		m_thread->wait();
		delete m_thread;
		m_thread = nullptr;
	}
	m_progress->setRange(0, 1);
	m_progress->setValue(1);
	if (ok)
	{
		m_result = result;
		m_resultType = m_lastOptions.type;
		m_preview->setPixmap(QPixmap::fromImage(result.image.scaled(m_preview->size().boundedTo(QSize(512, 512)), Qt::KeepAspectRatio, Qt::SmoothTransformation)));
		QString text = QCoreApplication::translate("VibeStudioModelBake", "Baked %n texel(s).", nullptr, result.texels);
		if (!result.notes.isEmpty())
			text += QLatin1Char(' ') + result.notes.join(QLatin1Char(' '));
		m_status->setText(text);
	}
	else if (cancelled)
	{
		m_status->setText(QCoreApplication::translate("VibeStudioModelBake", "The bake was cancelled; the previous map is kept."));
	}
	else
	{
		m_status->setText(QCoreApplication::translate("VibeStudioModelBake", "The bake failed: %1").arg(error));
	}
	m_status->setAccessibleDescription(m_status->text());
	setRunning(false);
}

void ModelBakeDialog::setApplyTarget(const QString &description, std::function<bool(const QImage &, ModelBakeType, double, QString *)> apply)
{
	m_applyHandler = std::move(apply);
	m_apply->setToolTip(description);
	m_apply->setAccessibleDescription(description);
	m_apply->setEnabled(!running() && !m_result.image.isNull() && bool(m_applyHandler));
}

bool ModelBakeDialog::applyToModel()
{
	if (!m_applyHandler || m_result.image.isNull() || running())
		return false;
	QString error;
	const bool applied = m_applyHandler(m_result.image, m_resultType, m_strength->value(), &error);
	m_status->setText(applied ? QCoreApplication::translate("VibeStudioModelBake", "Applied the %1 map: %2").arg(modelBakeTypeDisplayName(m_resultType), m_apply->toolTip())
							  : (error.isEmpty() ? QCoreApplication::translate("VibeStudioModelBake", "The map was not applied.") : error));
	m_status->setAccessibleDescription(m_status->text());
	return applied;
}

bool ModelBakeDialog::saveTo(const QString &path)
{
	if (m_result.image.isNull())
		return false;
	QByteArray bytes;
	if (QFileInfo(path).suffix().compare(QStringLiteral("tga"), Qt::CaseInsensitive) == 0)
	{
		TextureExportOptions options;
		options.format = TextureExportFormat::Targa;
		const TextureExportResult encoded = encodeTextureExport(m_result.image, options);
		if (!encoded.succeeded)
		{
			m_status->setText(encoded.error);
			return false;
		}
		bytes = encoded.bytes;
	}
	QSaveFile file(path);
	const bool written = file.open(QIODevice::WriteOnly) && (bytes.isEmpty() ? m_result.image.save(&file, "PNG") : file.write(bytes) == bytes.size()) && file.commit();
	m_status->setText(written ? QCoreApplication::translate("VibeStudioModelBake", "Saved the map to %1.").arg(QDir::toNativeSeparators(path))
							  : QCoreApplication::translate("VibeStudioModelBake", "The map could not be saved to %1.").arg(QDir::toNativeSeparators(path)));
	return written;
}

void ModelBakeDialog::reject()
{
	waitForWorker();
	QDialog::reject();
}

} // namespace vibestudio
