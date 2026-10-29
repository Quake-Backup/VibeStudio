#include "app/model_declarations_dialog.h"
#include "app/dialog_screen_fit.h"
#include "app/model_document_work.h"
#include "core/model_file_io.h"

#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGuiApplication>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QVBoxLayout>

namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelDeclarationsDialog)
};
void accessible(QWidget *widget, const QString &name, const QString &description)
{
	widget->setAccessibleName(name);
	widget->setAccessibleDescription(description);
	widget->setToolTip(description);
}
QString baseName(QString name)
{
	for (const auto &suffix : {QStringLiteral(".mesh.json"), QStringLiteral(".json"), QStringLiteral(".kvx")})
		if (name.endsWith(suffix, Qt::CaseInsensitive))
		{
			name.chop(suffix.size());
			break;
		}
	static const QRegularExpression invalid(QStringLiteral("[^A-Za-z0-9_]"));
	name.replace(invalid, QStringLiteral("_"));
	if (name.isEmpty() || name[0].isDigit())
		name.prepend(QLatin1Char('_'));
	return name.toLower();
}
} // namespace

ModelDeclarationsDialog::ModelDeclarationsDialog(const ModelMesh &mesh, const QString &sourceName, QWidget *parent)
	: QDialog(parent), m_mesh(mesh), m_sourceName(sourceName)
{
	setObjectName("modelDeclarationsDialog");
	setWindowTitle(Text::tr("Game Declarations"));
	accessible(this, windowTitle(), Text::tr("Generate the text game code and engines need to use this model, from its frames, clips and events."));
	auto *layout = new QVBoxLayout(this);
	auto *form = new QFormLayout;
	form->setRowWrapPolicy(QFormLayout::WrapLongRows);
	m_form = form;
	layout->addLayout(form);
	m_target = new QComboBox;
	m_target->setObjectName("declarationsTarget");
	m_target->addItems({Text::tr("Doom 3 / Quake 4 model declaration"), Text::tr("QuakeC frame macros"), Text::tr("Quake II frame header"),
						Text::tr("GZDoom MODELDEF"), Text::tr("GZDoom VOXELDEF")});
	accessible(m_target, Text::tr("Declaration"),
			   Text::tr("Doom 3 declares the mesh and every clip with its events; QuakeC and Quake II name every frame for game code; GZDoom "
						"MODELDEF maps sprite frames to model frames; VOXELDEF replaces sprites with a KVX voxel."));
	form->addRow(Text::tr("&Declaration"), m_target);
	const auto line = [&](const char *id, const QString &label, const QString &description)
	{
		auto *field = new QLineEdit;
		field->setObjectName(id);
		field->setLayoutDirection(Qt::LeftToRight);
		accessible(field, label, description);
		form->addRow(label, field);
		connect(field, &QLineEdit::textChanged, this, [this] { refresh(); });
		return field;
	};
	const auto name = baseName(sourceName.isEmpty() ? QStringLiteral("model") : sourceName);
	m_name = line("declarationsName", Text::tr("Name"), Text::tr("The model declaration's name, or the GZDoom actor class."));
	m_path = line("declarationsPath", Text::tr("Path"),
				  Text::tr("Doom 3: the md5mesh path. QuakeC and Quake II: the model folder. GZDoom: the folder of the model files."));
	m_model = line("declarationsModel", Text::tr("Model file"), Text::tr("GZDoom: the model file in that folder."));
	m_skin = line("declarationsSkin", Text::tr("Skin"), Text::tr("Optional: the Doom 3 skin declaration or the GZDoom skin file."));
	m_sprite = line("declarationsSprite", Text::tr("Sprite"), Text::tr("GZDoom: three upper-case letters or digits; each clip takes sprite names of these and one more."));
	m_sprites = line("declarationsSprites", Text::tr("Replaced sprites"),
					 Text::tr("VOXELDEF: the sprites the voxel replaces, separated by spaces: four characters for every frame, or five for one "
							  "frame."));
	const auto number = [&](QDoubleSpinBox *field, const char *id, const QString &label, const QString &description, double minimum, double maximum,
							double value)
	{
		field->setObjectName(id);
		field->setRange(minimum, maximum);
		field->setDecimals(3);
		field->setValue(value);
		field->setLayoutDirection(Qt::LeftToRight);
		accessible(field, label, description);
		form->addRow(label, field);
		connect(field, &QDoubleSpinBox::valueChanged, this, [this] { refresh(); });
	};
	m_scale = new QDoubleSpinBox;
	number(m_scale, "declarationsScale", Text::tr("Voxel scale"), Text::tr("VOXELDEF: the voxel's size factor; 1 draws it at its own size."), .001,
		   1000, 1);
	m_angle = new QDoubleSpinBox;
	number(m_angle, "declarationsAngle", Text::tr("Angle offset"),
		   Text::tr("VOXELDEF: degrees added to the voxel's facing. GZDoom adds its own 90 degrees as well."), -360, 360, 0);
	m_spin = new QSpinBox;
	m_spin->setObjectName("declarationsSpin");
	m_spin->setRange(-3600, 3600);
	m_spin->setLayoutDirection(Qt::LeftToRight);
	accessible(m_spin, Text::tr("Spin"), Text::tr("VOXELDEF: GZDoom's spin speed for the item; zero does not spin, a negative value turns the other way."));
	form->addRow(Text::tr("Spi&n"), m_spin);
	connect(m_spin, &QSpinBox::valueChanged, this, [this] { refresh(); });
	m_palette = new QCheckBox(Text::tr("Override the palette"));
	m_palette->setObjectName("declarationsPalette");
	accessible(m_palette, m_palette->text(), Text::tr("VOXELDEF: remap the voxel's own palette to the game's."));
	form->addRow(m_palette);
	connect(m_palette, &QCheckBox::toggled, this, [this] { refresh(); });
	m_replace = new QCheckBox(Text::tr("Replace files already in the package"));
	m_replace->setObjectName("declarationsReplace");
	accessible(m_replace, m_replace->text(), Text::tr("Stage in Package: replace files already at the model set's paths instead of refusing."));
	form->addRow(m_replace);
	m_name->setText(name);
	m_model->setText(name + QStringLiteral(".md3"));
	auto sprite = name.toUpper();
	static const QRegularExpression notSprite(QStringLiteral("[^A-Z0-9]"));
	sprite.remove(notSprite);
	m_sprite->setText((sprite + QStringLiteral("XXX")).left(3));
	m_sprites->setText((sprite + QStringLiteral("XXXX")).left(4));
	m_preview = new QPlainTextEdit;
	m_preview->setObjectName("declarationsPreview");
	m_preview->setReadOnly(true);
	m_preview->setLayoutDirection(Qt::LeftToRight);
	m_preview->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
	m_preview->setLineWrapMode(QPlainTextEdit::NoWrap);
	accessible(m_preview, Text::tr("Generated declaration"), Text::tr("The text that Copy and Save As write. Read only."));
	layout->addWidget(m_preview, 1);
	m_status = new LiveStatusLabel;
	m_status->setObjectName("declarationsStatus");
	m_status->setWordWrap(true);
	m_status->setTextFormat(Qt::PlainText);
	accessible(m_status, Text::tr("Declaration check"), Text::tr("Whether the declaration was generated, or what to fix."));
	layout->addWidget(m_status);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
	m_copy = buttons->addButton(Text::tr("&Copy"), QDialogButtonBox::ActionRole);
	m_copy->setObjectName("declarationsCopy");
	accessible(m_copy, Text::tr("Copy declaration"), Text::tr("Copy the generated text to the clipboard."));
	m_save = buttons->addButton(Text::tr("&Save As…"), QDialogButtonBox::ActionRole);
	m_save->setObjectName("declarationsSave");
	accessible(m_save, Text::tr("Save declaration"), Text::tr("Write the generated text to a file. An existing file is replaced only after you confirm."));
	m_set = buttons->addButton(Text::tr("Export &Model Set…"), QDialogButtonBox::ActionRole);
	m_set->setObjectName("declarationsModelSet");
	accessible(m_set, Text::tr("Export Doom 3 model set"),
			   Text::tr("Write the md5mesh, every clip as an md5anim and the .def under a game folder, at the paths the declaration names."));
	m_stage = buttons->addButton(Text::tr("S&tage in Package"), QDialogButtonBox::ActionRole);
	m_stage->setObjectName("declarationsStage");
	accessible(m_stage, Text::tr("Stage Doom 3 model set in package"),
			   Text::tr("Stage the md5mesh, every clip's md5anim and the .def into the active package as one package step, which one undo "
						"removes. Save the package to write them."));
	connect(m_stage, &QPushButton::clicked, this, [this] { stageModelSet(); });
	layout->addWidget(buttons);
	connect(buttons, &QDialogButtonBox::rejected, this, &ModelDeclarationsDialog::reject);
	connect(m_copy, &QPushButton::clicked, this,
			[this]
			{
				QGuiApplication::clipboard()->setText(m_text);
				m_status->setText(Text::tr("Copied the declaration."), LiveStatus::Result);
			});
	connect(m_save, &QPushButton::clicked, this,
			[this]
			{
				const auto path = QFileDialog::getSaveFileName(this, Text::tr("Save Declaration"), m_name->text());
				if (!path.isEmpty())
					saveText(path);
			});
	connect(m_set, &QPushButton::clicked, this,
			[this]
			{
				const auto folder = QFileDialog::getExistingDirectory(this, Text::tr("Export Model Set"));
				if (!folder.isEmpty())
					exportModelSet(folder);
			});
	connect(m_target, &QComboBox::currentIndexChanged, this, [this] { refresh(); });
	fitDialogToScreen(this, QSize(std::max(720, fontMetrics().averageCharWidth() * 90), 680));
	setTarget(m_mesh.skeleton.isEmpty() ? 1 : 0);
	refresh();
}

void ModelDeclarationsDialog::setTarget(int target)
{
	if (target < 0 || target >= m_target->count())
		return;
	if (target == m_target->currentIndex())
		refresh();
	m_target->setCurrentIndex(target);
}

ModelDoom3DefOptions ModelDeclarationsDialog::doom3() const
{
	ModelDoom3DefOptions o;
	o.name = m_name->text();
	o.meshPath = m_path->text().isEmpty() ? QStringLiteral("models/md5/%1/%1.md5mesh").arg(o.name) : m_path->text();
	o.skin = m_skin->text();
	o.sourceName = m_sourceName;
	return o;
}

void ModelDeclarationsDialog::refresh()
{
	// The fields report changes while the dialog is still being built.
	if (!m_preview || !m_status || !m_copy || !m_save || !m_set || !m_stage)
		return;
	const int target = m_target->currentIndex();
	const bool doom3Target = target == 0, gzdoom = target == 3, voxel = target == 4;
	for (auto *field : {m_model, m_sprite})
		field->setEnabled(gzdoom);
	m_skin->setEnabled(doom3Target || gzdoom);
	m_name->setEnabled(!voxel);
	for (QWidget *field : std::initializer_list<QWidget *>{m_sprites, m_scale, m_angle, m_spin, m_palette})
		m_form->setRowVisible(field, voxel);
	m_form->setRowVisible(m_replace, doom3Target && bool(m_stager));
	m_set->setEnabled(doom3Target && !m_busy);
	m_stage->setVisible(bool(m_stager));
	m_stage->setEnabled(doom3Target && !m_busy);
	QString error;
	if (doom3Target)
		m_text = modelDoom3ModelDefText(m_mesh, doom3(), &error);
	else if (target == 1)
	{
		ModelQuakeCFrameOptions o;
		o.modelDirectory = m_path->text();
		o.sourceName = m_sourceName;
		m_text = modelQuakeCFrameMacrosText(m_mesh, o, &error);
	}
	else if (target == 2)
	{
		ModelQuake2FrameOptions o;
		o.modelDirectory = m_path->text();
		o.sourceName = m_sourceName;
		m_text = modelQuake2FrameHeaderText(m_mesh, o, &error);
	}
	else if (voxel)
	{
		ModelGzdoomVoxeldefOptions o;
		o.sprites = m_sprites->text().toUpper().split(QRegularExpression(QStringLiteral("[,\\s]+")), Qt::SkipEmptyParts);
		o.path = m_path->text().isEmpty() ? QStringLiteral("voxels/%1.kvx").arg(m_name->text()) : m_path->text();
		o.scale = m_scale->value();
		o.angleOffset = m_angle->value();
		o.placedSpin = o.droppedSpin = m_spin->value();
		o.overridePalette = m_palette->isChecked();
		o.sourceName = m_sourceName;
		m_text = modelGzdoomVoxeldefText(m_mesh, o, &error);
	}
	else
	{
		ModelGzdoomModeldefOptions o;
		o.actor = m_name->text();
		o.path = m_path->text().isEmpty() ? QStringLiteral("models/%1").arg(m_name->text()) : m_path->text();
		o.model = m_model->text();
		o.skin = m_skin->text();
		o.sprite = m_sprite->text();
		o.sourceName = m_sourceName;
		m_text = modelGzdoomModeldefText(m_mesh, o, &error);
	}
	m_preview->setPlainText(m_text);
	m_copy->setEnabled(!m_text.isEmpty());
	m_save->setEnabled(!m_text.isEmpty());
	if (m_text.isEmpty())
	{
		m_set->setEnabled(false);
		m_stage->setEnabled(false);
		m_status->setText(error, LiveStatus::Problem);
	}
	else
		m_status->setText(Text::tr("Generated. Copy it, save it, or export the model set."), LiveStatus::Result);
}

bool ModelDeclarationsDialog::saveText(const QString &path)
{
	const auto target = inspectModelWriteTarget(path);
	QString error = target.error;
	if (m_text.isEmpty() || !target.isValid() || !writeModelFile(target, m_text.toUtf8(), &error))
	{
		m_status->setText(m_text.isEmpty() ? Text::tr("Nothing was generated to save.") : error, LiveStatus::Problem);
		return false;
	}
	m_status->setText(Text::tr("Saved %1.").arg(QDir::toNativeSeparators(path)), LiveStatus::Finished);
	return true;
}

bool ModelDeclarationsDialog::exportModelSet(const QString &folder)
{
	if (m_busy || m_target->currentIndex() != 0)
		return false;
	const auto mesh = m_mesh;
	const auto options = doom3();
	QVector<ModelGameFile> files;
	QString error;
	m_busy = true;
	m_set->setEnabled(false);
	m_status->setText(Text::tr("Writing the model set…"));
	const bool ok = runModelTask(
		this, Text::tr("Export Model Set"),
		[mesh, options, folder, &files](QString *failure, const ModelWorkControl &control)
		{
			if (!modelDoom3ModelSet(mesh, options, &files, failure, control))
				return false;
			// Every destination is checked before anything is written.
			QVector<ModelWriteTarget> targets;
			for (const auto &file : files)
			{
				auto target = inspectModelWriteTarget(QDir(folder).filePath(file.path), control);
				if (!target.isValid())
				{
					if (failure)
						*failure = target.error;
					return false;
				}
				if (target.existed)
				{
					if (failure)
						*failure = Text::tr("%1 already exists. Choose an empty folder, or remove the old set first.").arg(QDir::toNativeSeparators(target.path));
					return false;
				}
				targets.append(target);
			}
			for (int i = 0; i < files.size(); ++i)
			{
				QDir().mkpath(QFileInfo(targets[i].path).absolutePath());
				const auto target = inspectModelWriteTarget(targets[i].path, control);
				if (!target.isValid() || !writeModelFile(target, files[i].bytes, failure, control))
				{
					if (failure && !target.isValid())
						*failure = target.error;
					return false;
				}
			}
			return true;
		},
		&error, true, &m_cancel);
	m_busy = false;
	m_set->setEnabled(true);
	if (!ok)
	{
		m_status->setText(error, LiveStatus::Problem);
		return false;
	}
	m_status->setText(Text::tr("Wrote %n file(s) under %1.", nullptr, int(files.size())).arg(QDir::toNativeSeparators(folder)), LiveStatus::Finished);
	return true;
}

void ModelDeclarationsDialog::setStager(std::function<bool(const QVector<ModelGameFile> &, bool, QString *)> stager)
{
	m_stager = std::move(stager);
	refresh();
}
bool ModelDeclarationsDialog::stageModelSet()
{
	if (m_busy || m_target->currentIndex() != 0 || !m_stager)
		return false;
	const auto mesh = m_mesh;
	const auto options = doom3();
	QVector<ModelGameFile> files;
	QString error;
	m_busy = true;
	refresh();
	m_status->setText(Text::tr("Preparing the model set…"));
	const bool ok = runModelTask(
		this, Text::tr("Stage Model Set"), [mesh, options, &files](QString *failure, const ModelWorkControl &control)
		{ return modelDoom3ModelSet(mesh, options, &files, failure, control); }, &error, false, &m_cancel);
	m_busy = false;
	refresh();
	if (!ok || !m_stager(files, m_replace->isChecked(), &error))
	{
		m_status->setText(error, LiveStatus::Problem);
		return false;
	}
	m_status->setText(Text::tr("Staged %n file(s) in the package as one step. Save the package to write them.", nullptr, int(files.size())),
					  LiveStatus::Finished);
	return true;
}
void ModelDeclarationsDialog::reject()
{
	if (m_busy)
	{
		if (m_cancel)
			m_cancel();
		return;
	}
	QDialog::reject();
}
} // namespace vibestudio
