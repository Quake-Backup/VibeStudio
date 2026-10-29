#include "app/model_assembly_dialog.h"
#include "app/dialog_screen_fit.h"

#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QTableWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <optional>

namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelQ2PlayerDialog)
};
QString cell(const QTableWidget *table, int row, int column)
{
	const auto *item = table->item(row, column);
	return item ? item->text().trimmed() : QString();
}
} // namespace

bool ModelAssemblyDialog::prepareQ2PlayerBundle(const ModelQ2PlayerOptions &options, ModelQ2PlayerBundle *result, QString *error)
{
	if (!result)
		return false;
	play(false);
	const auto directory = m_document.directory();
	const auto archive = m_materialSource.archive;
	ModelQ2PlayerBundle candidate;
	if (!performWork(
			Text::tr("Review Quake II Player Package"),
			[&](QString *failure, const ModelWorkControl &control) {
				return prepareModelQ2PlayerBundle(options, directory, archive, &candidate, failure, control);
			},
			error))
		return false;
	*result = std::move(candidate);
	report(Text::tr("Quake II player package ready for review: %Ln file(s).", nullptr, int(result->package.files.size())), LiveStatus::Result);
	return true;
}

bool ModelAssemblyDialog::exportQ2PlayerBundle(const ModelQ2PlayerBundle &bundle, const QString &path, bool overwrite,
											   PackageWriteReport *result, QString *error)
{
	if (result)
		*result = {};
	play(false);
	PackageWriteReport written;
	const bool completed = performWork(
		Text::tr("Export Quake II Player Package"),
		[&](QString *failure, const ModelWorkControl &control) {
			written = writeModelQ2PlayerBundle(bundle, path, overwrite, false, control);
			if (!written.succeeded())
				*failure = packageWriteReportText(written);
			return written.outputCommitted || written.succeeded();
		},
		error, true);
	if (result)
		*result = written;
	if (completed)
		report(packageWriteReportText(written));
	return completed;
}

void ModelAssemblyDialog::chooseQ2PlayerBundle()
{
	if (m_working)
		return;
	play(false);
	QDialog dialog(this);
	dialog.setObjectName(QStringLiteral("q2PlayerDialog"));
	dialog.setWindowTitle(Text::tr("Quake II Player Package"));
	dialog.setAccessibleName(dialog.windowTitle());
	auto *layout = new QVBoxLayout(&dialog);
	auto *scroll = new QScrollArea;
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	auto *body = new QWidget;
	auto *content = new QVBoxLayout(body);
	auto *form = new QFormLayout;
	form->setRowWrapPolicy(QFormLayout::WrapAllRows);
	form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	content->addLayout(form);
	const auto field = [&](QWidget *control, const char *id, const QString &label, const QString &description) {
		control->setObjectName(QString::fromLatin1(id));
		control->setAccessibleName(label);
		control->setAccessibleDescription(description);
		control->setToolTip(description);
		auto *text = new QLabel(label);
		text->setTextFormat(Qt::PlainText);
		text->setWordWrap(true);
		text->setBuddy(control);
		form->addRow(text, control);
	};
	auto *name = new QLineEdit;
	name->setMaxLength(24);
	name->setLayoutDirection(Qt::LeftToRight);
	field(name, "q2PlayerName", Text::tr("Player ID"), Text::tr("The folder under players/. Starts with a letter; ASCII letters, digits, "
																 "underscores and hyphens."));
	const auto fileField = [&](const char *id, const QString &label, const QString &description, const QString &filter) {
		auto *row = new QWidget;
		auto *rowLayout = new QHBoxLayout(row);
		rowLayout->setContentsMargins(0, 0, 0, 0);
		auto *edit = new QLineEdit;
		edit->setLayoutDirection(Qt::LeftToRight);
		auto *browse = new QPushButton(Text::tr("Browse…"));
		browse->setAutoDefault(false);
		browse->setAccessibleName(Text::tr("Browse for %1").arg(label));
		rowLayout->addWidget(edit, 1);
		rowLayout->addWidget(browse);
		field(edit, id, label, description);
		form->addRow(row);
		connect(browse, &QPushButton::clicked, &dialog, [&dialog, edit, label, filter] {
			const auto chosen = QFileDialog::getOpenFileName(&dialog, label, edit->text(), filter);
			if (!chosen.isEmpty())
				edit->setText(chosen);
		});
		return edit;
	};
	const auto models = Text::tr("Models (*.mesh.json *.md2 *.md3 *.obj *.mdl)");
	const auto images = Text::tr("Images (*.png *.tga *.pcx *.jpg *.bmp)");
	auto *bodyModel = fileField("q2PlayerBody", Text::tr("Body model"),
								Text::tr("tris.md2: one surface with the 198 frames of Quake II's player animation. A file, or package:PATH for "
											  "an entry of the open package."), models);
	auto *weaponModel = fileField("q2PlayerWeapon", Text::tr("Weapon model"),
								  Text::tr("weapon.md2: one surface, in frame lock with the body."), models);
	auto *weaponSkin = fileField("q2PlayerWeaponSkin", Text::tr("Weapon skin"),
								 Text::tr("Optional image for weapon.pcx, converted to the Quake II palette."), images);
	const auto table = [&](const char *id, const QString &label, const QStringList &headers) {
		auto *widget = new QTableWidget(0, int(headers.size()));
		widget->setObjectName(QString::fromLatin1(id));
		widget->setAccessibleName(label);
		widget->setHorizontalHeaderLabels(headers);
		widget->horizontalHeader()->setStretchLastSection(true);
		widget->setMinimumHeight(widget->fontMetrics().lineSpacing() * 6);
		auto *title = new QLabel(label);
		title->setBuddy(widget);
		content->addWidget(title);
		content->addWidget(widget);
		return widget;
	};
	auto *skins = table("q2PlayerSkins", Text::tr("Skins"), {Text::tr("Skin ID"), Text::tr("Image"), Text::tr("Icon")});
	skins->setAccessibleDescription(Text::tr("One row per skin: its ID, its image and an optional icon image, each a file or package:PATH. Every skin has "
											 "one size."));
	auto *rows = new QHBoxLayout;
	auto *addSkin = new QPushButton(Text::tr("Add Skin"));
	auto *removeSkin = new QPushButton(Text::tr("Remove Skin"));
	for (auto *button : {addSkin, removeSkin})
	{
		button->setAutoDefault(false);
		button->setAccessibleName(button->text());
		rows->addWidget(button);
	}
	rows->addStretch(1);
	content->addLayout(rows);
	connect(addSkin, &QPushButton::clicked, &dialog, [skins] {
		const int row = skins->rowCount();
		skins->insertRow(row);
		for (int column = 0; column < 3; ++column)
			skins->setItem(row, column, new QTableWidgetItem);
		skins->setCurrentCell(row, 0);
	});
	connect(removeSkin, &QPushButton::clicked, &dialog, [skins] {
		if (skins->currentRow() >= 0)
			skins->removeRow(skins->currentRow());
	});
	auto *sounds = table("q2PlayerSounds", Text::tr("Sounds"), {Text::tr("Sound"), Text::tr("File")});
	sounds->setAccessibleDescription(Text::tr("Optional custom sounds, each a file or package:PATH; the game plays the male sound for each one "
											  "left empty."));
	for (const auto &sound : modelQ2PlayerSoundNames())
	{
		const int row = sounds->rowCount();
		sounds->insertRow(row);
		auto *label = new QTableWidgetItem(sound);
		label->setFlags(label->flags() & ~Qt::ItemIsEditable);
		sounds->setItem(row, 0, label);
		sounds->setItem(row, 1, new QTableWidgetItem);
	}
	auto *views = table("q2PlayerViewWeapons", Text::tr("View weapons"), {Text::tr("Weapon"), Text::tr("Model")});
	views->setAccessibleDescription(Text::tr("Optional w_<weapon>.md2 models, each a file or package:PATH, in frame lock with the body."));
	views->setRowCount(4);
	for (int row = 0; row < 4; ++row)
		for (int column = 0; column < 2; ++column)
			views->setItem(row, column, new QTableWidgetItem);
	auto *files = new QTreeWidget;
	files->setObjectName(QStringLiteral("q2PlayerFiles"));
	files->setAccessibleName(Text::tr("Reviewed Quake II player files"));
	files->setHeaderLabels({Text::tr("Package path"), Text::tr("Role"), Text::tr("Bytes")});
	files->setRootIsDecorated(false);
	files->header()->setSectionResizeMode(0, QHeaderView::Stretch);
	content->addWidget(files);
	auto *details = new QPlainTextEdit;
	details->setObjectName(QStringLiteral("q2PlayerDetails"));
	details->setAccessibleName(Text::tr("Quake II player review details"));
	details->setReadOnly(true);
	details->setTabChangesFocus(true);
	content->addWidget(details);
	scroll->setWidget(body);
	layout->addWidget(scroll, 1);
	auto *status = new QLabel(Text::tr("Choose the models and skins, then review the package. The open package supplies the palette; type "
									"package:PATH in any path to use one of its entries."));
	status->setObjectName(QStringLiteral("q2PlayerStatus"));
	status->setWordWrap(true);
	status->setAccessibleName(Text::tr("Quake II player package status"));
	layout->addWidget(status);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
	auto *review = buttons->addButton(Text::tr("Review Package"), QDialogButtonBox::ActionRole);
	auto *publish = buttons->addButton(Text::tr("Export Package…"), QDialogButtonBox::ActionRole);
	review->setObjectName(QStringLiteral("q2PlayerReview"));
	publish->setObjectName(QStringLiteral("q2PlayerExport"));
	for (auto *button : {review, publish})
	{
		button->setAutoDefault(false);
		button->setAccessibleName(button->text());
	}
	publish->setEnabled(false);
	layout->addWidget(buttons);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	std::optional<ModelQ2PlayerBundle> prepared;
	connect(review, &QPushButton::clicked, &dialog, [&] {
		prepared.reset();
		files->clear();
		details->clear();
		publish->setEnabled(false);
		ModelQ2PlayerOptions options;
		options.modelName = name->text().trimmed();
		options.bodySource = bodyModel->text().trimmed();
		options.weaponSource = weaponModel->text().trimmed();
		options.weaponSkin = weaponSkin->text().trimmed();
		for (int row = 0; row < skins->rowCount(); ++row)
			if (!cell(skins, row, 0).isEmpty())
				options.skins.append({cell(skins, row, 0), cell(skins, row, 1), cell(skins, row, 2)});
		for (int row = 0; row < sounds->rowCount(); ++row)
			if (!cell(sounds, row, 1).isEmpty())
				options.sounds.append({cell(sounds, row, 0), cell(sounds, row, 1)});
		for (int row = 0; row < views->rowCount(); ++row)
			if (!cell(views, row, 0).isEmpty())
				options.viewWeapons.append({cell(views, row, 0), cell(views, row, 1)});
		ModelQ2PlayerBundle candidate;
		QString error;
		if (!prepareQ2PlayerBundle(options, &candidate, &error))
		{
			status->setText(error);
			return;
		}
		for (const auto &file : candidate.package.files)
			new QTreeWidgetItem(files, {file.path, file.role, QLocale().toString(file.bytes.size())});
		details->setPlainText(candidate.package.notes.join(QStringLiteral("\n\n")));
		prepared = std::move(candidate);
		publish->setEnabled(true);
		status->setText(Text::tr("Review ready: %Ln file(s). Export writes exactly these bytes.", nullptr, int(prepared->package.files.size())));
	});
	connect(publish, &QPushButton::clicked, &dialog, [&] {
		if (!prepared)
			return;
		const auto path = QFileDialog::getSaveFileName(&dialog, Text::tr("Export Quake II Player Package"), {},
													   Text::tr("Quake II package (*.pak);;Zip package (*.pk2 *.zip)"));
		if (path.isEmpty())
			return;
		PackageWriteReport written;
		QString error;
		status->setText(exportQ2PlayerBundle(*prepared, path, true, &written, &error) ? packageWriteReportText(written) : error);
	});
	fitDialogToScreen(&dialog, QSize(680, 760));
	dialog.exec();
}
} // namespace vibestudio
