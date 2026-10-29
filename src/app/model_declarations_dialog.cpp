#include "app/model_declarations_dialog.h"
#include "app/dialog_screen_fit.h"
#include "app/model_document_work.h"
#include "core/model_file_io.h"

#include <QClipboard>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGuiApplication>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
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
	for (const auto &suffix : {QStringLiteral(".mesh.json"), QStringLiteral(".json")})
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
	layout->addLayout(form);
	m_target = new QComboBox;
	m_target->setObjectName("declarationsTarget");
	m_target->addItems({Text::tr("Doom 3 / Quake 4 model declaration"), Text::tr("QuakeC frame macros"), Text::tr("Quake II frame header"),
						Text::tr("GZDoom MODELDEF")});
	accessible(m_target, Text::tr("Declaration"),
			   Text::tr("Doom 3 declares the mesh and every clip with its events; QuakeC and Quake II name every frame for game code; GZDoom "
						"maps sprite frames to model frames."));
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
	m_name->setText(name);
	m_model->setText(name + QStringLiteral(".md3"));
	auto sprite = name.toUpper();
	static const QRegularExpression notSprite(QStringLiteral("[^A-Z0-9]"));
	sprite.remove(notSprite);
	m_sprite->setText((sprite + QStringLiteral("XXX")).left(3));
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
	if (!m_preview || !m_status || !m_copy || !m_save || !m_set)
		return;
	const int target = m_target->currentIndex();
	const bool doom3Target = target == 0, gzdoom = target == 3;
	for (auto *field : {m_model, m_sprite})
		field->setEnabled(gzdoom);
	m_skin->setEnabled(doom3Target || gzdoom);
	m_set->setEnabled(doom3Target && !m_busy);
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
