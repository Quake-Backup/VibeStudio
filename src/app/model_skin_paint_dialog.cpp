#include "app/model_skin_paint_dialog.h"

#include "core/texture_export.h"

#include <QColorDialog>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSaveFile>
#include <QSpinBox>
#include <QVBoxLayout>

namespace vibestudio
{

// The model, drawn from the paint view, with the brush outline under the
// pointer. Left drag paints; the dialog does the work.
class ModelSkinPaintCanvas final : public QWidget
{
  public:
	explicit ModelSkinPaintCanvas(ModelSkinPaintDialog *dialog) : m_dialog(dialog)
	{
		setObjectName(QStringLiteral("modelSkinPaintCanvas"));
		setAccessibleName(QCoreApplication::translate("VibeStudioModelSkinPaint", "Paint view"));
		setAccessibleDescription(QCoreApplication::translate("VibeStudioModelSkinPaint",
			"Drag with the left button to paint the skin on the model; Escape cancels a stroke. Change the view with Yaw, Pitch and Zoom."));
		setMouseTracking(true);
		setFocusPolicy(Qt::StrongFocus);
	}
	QImage image;
	QPointF pointer{-1000, -1000};

  protected:
	QSize sizeHint() const override { return image.isNull() ? QSize(512, 512) : image.size(); }
	void paintEvent(QPaintEvent *) override
	{
		QPainter painter(this);
		painter.drawImage(0, 0, image);
		if (rect().contains(pointer.toPoint()))
		{
			// The brush outline, in two tones so it shows on any skin.
			const double radius = m_dialog->brush().radius;
			painter.setRenderHint(QPainter::Antialiasing);
			painter.setPen(QPen(Qt::black, 3));
			painter.drawEllipse(pointer, radius, radius);
			painter.setPen(QPen(Qt::white, 1));
			painter.drawEllipse(pointer, radius, radius);
		}
	}
	void mousePressEvent(QMouseEvent *event) override
	{
		if (event->button() == Qt::LeftButton)
			m_dialog->beginStroke(event->position());
	}
	void mouseMoveEvent(QMouseEvent *event) override
	{
		pointer = event->position();
		if (m_dialog->strokeActive())
			m_dialog->extendStroke(event->position());
		update();
	}
	void mouseReleaseEvent(QMouseEvent *event) override
	{
		if (event->button() == Qt::LeftButton && m_dialog->strokeActive())
			m_dialog->finishStroke(true);
	}
	void leaveEvent(QEvent *) override
	{
		pointer = {-1000, -1000};
		update();
	}

  private:
	ModelSkinPaintDialog *m_dialog;
};

ModelSkinPaintDialog::ModelSkinPaintDialog(const ModelMesh &mesh, int surface, int frame, const QImage &skin, QWidget *parent)
	: QDialog(parent), m_mesh(mesh), m_surface(surface), m_frame(frame), m_skin(skin)
{
	setObjectName(QStringLiteral("modelSkinPaintDialog"));
	setWindowTitle(QCoreApplication::translate("VibeStudioModelSkinPaint", "Paint Skin"));
	setAccessibleName(windowTitle());
	if (m_skin.isNull())
	{
		m_skin = QImage(256, 256, QImage::Format_ARGB32);
		m_skin.fill(Qt::white);
	}
	else if (m_skin.format() != QImage::Format_Indexed8)
	{
		m_skin = m_skin.convertToFormat(QImage::Format_ARGB32);
	}
	auto *layout = new QHBoxLayout(this);
	m_canvas = new ModelSkinPaintCanvas(this);
	layout->addWidget(m_canvas, 1);
	auto *side = new QVBoxLayout;
	auto *form = new QFormLayout;
	m_colourButton = new QPushButton(QCoreApplication::translate("VibeStudioModelSkinPaint", "Colour…"));
	m_colourButton->setObjectName(QStringLiteral("modelSkinPaintColour"));
	m_colourButton->setAccessibleName(QCoreApplication::translate("VibeStudioModelSkinPaint", "Brush colour"));
	form->addRow(QCoreApplication::translate("VibeStudioModelSkinPaint", "&Colour"), m_colourButton);
	m_index = new QSpinBox;
	m_index->setObjectName(QStringLiteral("modelSkinPaintIndex"));
	m_index->setRange(-1, 255);
	m_index->setValue(-1);
	m_index->setSpecialValueText(QCoreApplication::translate("VibeStudioModelSkinPaint", "Nearest to colour"));
	m_index->setAccessibleName(QCoreApplication::translate("VibeStudioModelSkinPaint", "Palette index"));
	m_index->setEnabled(m_skin.format() == QImage::Format_Indexed8);
	form->addRow(QCoreApplication::translate("VibeStudioModelSkinPaint", "&Index"), m_index);
	m_radius = new QSpinBox;
	m_radius->setObjectName(QStringLiteral("modelSkinPaintRadius"));
	m_radius->setRange(1, 256);
	m_radius->setValue(12);
	m_radius->setAccessibleName(QCoreApplication::translate("VibeStudioModelSkinPaint", "Brush radius in view pixels"));
	form->addRow(QCoreApplication::translate("VibeStudioModelSkinPaint", "&Radius"), m_radius);
	m_opacity = new QDoubleSpinBox;
	m_opacity->setObjectName(QStringLiteral("modelSkinPaintOpacity"));
	m_opacity->setRange(0.0, 1.0);
	m_opacity->setSingleStep(0.05);
	m_opacity->setValue(1.0);
	m_opacity->setAccessibleName(QCoreApplication::translate("VibeStudioModelSkinPaint", "Brush opacity"));
	form->addRow(QCoreApplication::translate("VibeStudioModelSkinPaint", "&Opacity"), m_opacity);
	m_hardness = new QDoubleSpinBox;
	m_hardness->setObjectName(QStringLiteral("modelSkinPaintHardness"));
	m_hardness->setRange(0.0, 1.0);
	m_hardness->setSingleStep(0.1);
	m_hardness->setValue(0.5);
	m_hardness->setAccessibleName(QCoreApplication::translate("VibeStudioModelSkinPaint", "Brush hardness"));
	form->addRow(QCoreApplication::translate("VibeStudioModelSkinPaint", "&Hardness"), m_hardness);
	m_mode = new QComboBox;
	m_mode->setObjectName(QStringLiteral("modelSkinPaintMode"));
	m_mode->setAccessibleName(QCoreApplication::translate("VibeStudioModelSkinPaint", "Brush blend"));
	m_mode->addItem(QCoreApplication::translate("VibeStudioModelSkinPaint", "Mix"));
	m_mode->addItem(QCoreApplication::translate("VibeStudioModelSkinPaint", "Replace"));
	form->addRow(QCoreApplication::translate("VibeStudioModelSkinPaint", "&Blend"), m_mode);
	m_mirror = new QComboBox;
	m_mirror->setObjectName(QStringLiteral("modelSkinPaintMirror"));
	m_mirror->setAccessibleName(QCoreApplication::translate("VibeStudioModelSkinPaint", "Mirror axis"));
	m_mirror->addItem(QCoreApplication::translate("VibeStudioModelSkinPaint", "No mirror"));
	m_mirror->addItem(QCoreApplication::translate("VibeStudioModelSkinPaint", "Across X"));
	m_mirror->addItem(QCoreApplication::translate("VibeStudioModelSkinPaint", "Across Y"));
	m_mirror->addItem(QCoreApplication::translate("VibeStudioModelSkinPaint", "Across Z"));
	form->addRow(QCoreApplication::translate("VibeStudioModelSkinPaint", "&Mirror"), m_mirror);
	const auto angle = [&](const QString &name, const QString &accessible, double low, double high, double value) {
		auto *spin = new QDoubleSpinBox;
		spin->setObjectName(name);
		spin->setRange(low, high);
		spin->setValue(value);
		spin->setSingleStep(15.0);
		spin->setAccessibleName(accessible);
		return spin;
	};
	m_yaw = angle(QStringLiteral("modelSkinPaintYaw"), QCoreApplication::translate("VibeStudioModelSkinPaint", "View yaw in degrees"), -360, 360, m_camera.yaw);
	m_pitch = angle(QStringLiteral("modelSkinPaintPitch"), QCoreApplication::translate("VibeStudioModelSkinPaint", "View pitch in degrees"), -89, 89, m_camera.pitch);
	m_zoom = angle(QStringLiteral("modelSkinPaintZoom"), QCoreApplication::translate("VibeStudioModelSkinPaint", "View zoom"), 0.1, 20, m_camera.zoom);
	m_zoom->setSingleStep(0.25);
	form->addRow(QCoreApplication::translate("VibeStudioModelSkinPaint", "&Yaw"), m_yaw);
	form->addRow(QCoreApplication::translate("VibeStudioModelSkinPaint", "&Pitch"), m_pitch);
	form->addRow(QCoreApplication::translate("VibeStudioModelSkinPaint", "&Zoom"), m_zoom);
	side->addLayout(form);
	m_status = new QLabel;
	m_status->setObjectName(QStringLiteral("modelSkinPaintStatus"));
	m_status->setWordWrap(true);
	m_status->setTextFormat(Qt::PlainText);
	m_status->setAccessibleName(QCoreApplication::translate("VibeStudioModelSkinPaint", "Paint status"));
	side->addWidget(m_status);
	side->addStretch(1);
	auto *buttons = new QDialogButtonBox(Qt::Vertical);
	m_undoButton = buttons->addButton(QCoreApplication::translate("VibeStudioModelSkinPaint", "&Undo Stroke"), QDialogButtonBox::ActionRole);
	m_undoButton->setObjectName(QStringLiteral("modelSkinPaintUndo"));
	m_redoButton = buttons->addButton(QCoreApplication::translate("VibeStudioModelSkinPaint", "&Redo Stroke"), QDialogButtonBox::ActionRole);
	m_redoButton->setObjectName(QStringLiteral("modelSkinPaintRedo"));
	m_save = buttons->addButton(QCoreApplication::translate("VibeStudioModelSkinPaint", "&Save Skin…"), QDialogButtonBox::ActionRole);
	m_save->setObjectName(QStringLiteral("modelSkinPaintSave"));
	auto *close = buttons->addButton(QDialogButtonBox::Close);
	close->setObjectName(QStringLiteral("modelSkinPaintClose"));
	for (auto *button : {m_undoButton, m_redoButton, m_save, close})
		button->setAccessibleName(button->text().remove(QLatin1Char('&')));
	side->addWidget(buttons);
	layout->addLayout(side);
	connect(m_colourButton, &QPushButton::clicked, this, [this] {
		const QColor chosen = QColorDialog::getColor(m_colour, this, QCoreApplication::translate("VibeStudioModelSkinPaint", "Brush Colour"));
		if (chosen.isValid())
		{
			m_colour = chosen;
			refresh();
		}
	});
	for (auto *spin : {m_yaw, m_pitch, m_zoom})
		connect(spin, &QDoubleSpinBox::valueChanged, this, [this] { rebuildView(); });
	connect(m_radius, &QSpinBox::valueChanged, this, [this] { m_canvas->update(); });
	connect(m_undoButton, &QPushButton::clicked, this, [this] { undo(); });
	connect(m_redoButton, &QPushButton::clicked, this, [this] { redo(); });
	connect(close, &QPushButton::clicked, this, [this] { reject(); });
	connect(m_save, &QPushButton::clicked, this, [this] {
		const auto path = QFileDialog::getSaveFileName(this, QCoreApplication::translate("VibeStudioModelSkinPaint", "Save Painted Skin"), QString(),
			QCoreApplication::translate("VibeStudioModelSkinPaint", "PNG images (*.png);;Targa images (*.tga)"));
		if (!path.isEmpty())
			saveTo(path);
	});
	rebuildView();
}

ModelSkinBrush ModelSkinPaintDialog::brush() const
{
	ModelSkinBrush brush;
	brush.radius = m_radius->value();
	brush.colour = m_colour;
	brush.paletteIndex = m_index->value();
	brush.opacity = m_opacity->value();
	brush.hardness = m_hardness->value();
	brush.mode = m_mode->currentIndex() == 1 ? ModelSkinPaintMode::Replace : ModelSkinPaintMode::Mix;
	brush.mirrorAxis = m_mirror->currentIndex() - 1;
	return brush;
}

void ModelSkinPaintDialog::rebuildView()
{
	if (m_active)
		finishStroke(false);
	m_camera.yaw = m_yaw->value();
	m_camera.pitch = m_pitch->value();
	m_camera.zoom = m_zoom->value();
	m_camera.size = QSize(512, 512);
	QString error;
	if (!buildModelSkinPaintView(m_mesh, m_frame, m_camera, &m_view, &error))
		m_status->setText(error);
	refresh();
}

void ModelSkinPaintDialog::refresh()
{
	m_canvas->image = preview();
	m_canvas->setFixedSize(m_canvas->image.size());
	m_canvas->update();
	m_undoButton->setEnabled(!m_undo.isEmpty() && !m_active);
	m_redoButton->setEnabled(!m_redo.isEmpty() && !m_active);
	m_colourButton->setText(QCoreApplication::translate("VibeStudioModelSkinPaint", "Colour %1…").arg(m_colour.name()));
	m_colourButton->setAccessibleDescription(m_colour.name());
	const int strokes = int(m_strokes.size());
	m_status->setText(QCoreApplication::translate("VibeStudioModelSkinPaint", "%n stroke(s) painted. Escape cancels a stroke in progress.", nullptr, strokes));
	m_status->setAccessibleDescription(m_status->text());
}

QImage ModelSkinPaintDialog::preview() const
{
	return renderModelSkinPaintPreview(m_mesh, m_frame, m_view, {{m_surface, m_skin}}, palette().color(QPalette::Base));
}

bool ModelSkinPaintDialog::beginStroke(QPointF point)
{
	if (m_active)
		return false;
	m_active = true;
	m_before = m_skin;
	m_pending = {};
	m_pending.brush = brush();
	m_pending.samples = {point};
	repaintStroke();
	return true;
}

bool ModelSkinPaintDialog::extendStroke(QPointF point)
{
	if (!m_active)
		return false;
	// Samples a third of the radius apart keep the stroke continuous.
	const QPointF last = m_pending.samples.last();
	const double spacing = std::max(1.0, m_pending.brush.radius / 3.0);
	const double distance = std::hypot(point.x() - last.x(), point.y() - last.y());
	for (int step = 1; step <= int(distance / spacing); ++step)
		m_pending.samples.append(last + (point - last) * (step * spacing / distance));
	m_pending.samples.append(point);
	repaintStroke();
	return true;
}

void ModelSkinPaintDialog::repaintStroke()
{
	// The whole stroke from the skin it started on, as model paint-skin
	// replays it, so the result never depends on pointer timing.
	QImage painted = m_before;
	QString error;
	if (!paintModelSkinStroke(m_mesh, m_surface, m_frame, m_view, m_pending, &painted, nullptr, &error))
	{
		m_status->setText(error);
		return;
	}
	m_skin = painted;
	m_canvas->image = preview();
	m_canvas->update();
}

bool ModelSkinPaintDialog::finishStroke(bool commit)
{
	if (!m_active)
		return false;
	m_active = false;
	if (!commit)
	{
		m_skin = m_before;
		refresh();
		m_status->setText(QCoreApplication::translate("VibeStudioModelSkinPaint", "Stroke cancelled."));
		return true;
	}
	m_undo.append(m_before);
	m_strokes.append(m_pending);
	m_redo.clear();
	m_redoStrokes.clear();
	refresh();
	return true;
}

void ModelSkinPaintDialog::undo()
{
	if (m_active || m_undo.isEmpty())
		return;
	m_redo.append(m_skin);
	m_redoStrokes.append(m_strokes.takeLast());
	m_skin = m_undo.takeLast();
	refresh();
}

void ModelSkinPaintDialog::redo()
{
	if (m_active || m_redo.isEmpty())
		return;
	m_undo.append(m_skin);
	m_strokes.append(m_redoStrokes.takeLast());
	m_skin = m_redo.takeLast();
	refresh();
}

QJsonObject ModelSkinPaintDialog::strokesJson() const
{
	return modelSkinPaintStrokesJson(m_camera, m_strokes);
}

bool ModelSkinPaintDialog::saveTo(const QString &path)
{
	QByteArray bytes;
	if (QFileInfo(path).suffix().compare(QStringLiteral("tga"), Qt::CaseInsensitive) == 0)
	{
		TextureExportOptions options;
		options.format = TextureExportFormat::Targa;
		const TextureExportResult encoded = encodeTextureExport(m_skin.convertToFormat(QImage::Format_ARGB32), options);
		if (!encoded.succeeded)
		{
			m_status->setText(encoded.error);
			return false;
		}
		bytes = encoded.bytes;
	}
	QSaveFile file(path);
	const bool written = file.open(QIODevice::WriteOnly) && (bytes.isEmpty() ? m_skin.save(&file, "PNG") : file.write(bytes) == bytes.size()) && file.commit();
	m_status->setText(written ? QCoreApplication::translate("VibeStudioModelSkinPaint", "Saved the skin to %1.").arg(QDir::toNativeSeparators(path))
							  : QCoreApplication::translate("VibeStudioModelSkinPaint", "The skin could not be saved to %1.").arg(QDir::toNativeSeparators(path)));
	return written;
}

void ModelSkinPaintDialog::keyPressEvent(QKeyEvent *event)
{
	// Escape cancels the stroke in progress before it closes the dialog.
	if (event->key() == Qt::Key_Escape && m_active)
	{
		finishStroke(false);
		event->accept();
		return;
	}
	QDialog::keyPressEvent(event);
}

} // namespace vibestudio
