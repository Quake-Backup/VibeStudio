#include "app/model_assembly_dialog.h"
#include "app/dialog_screen_fit.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
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
	Q_DECLARE_TR_FUNCTIONS(ModelWeaponBundleDialog)
};
} // namespace

bool ModelAssemblyDialog::prepareWeaponBundle(const ModelWeaponBundleOptions &options, ModelWeaponBundle *result, QString *error)
{
	if (!result)
		return false;
	play(false);
	const auto assembly = m_document.assembly();
	const auto resolved = m_resolved;
	const ModelAssemblyContext context{m_document.directory(), m_materialSource.archive, m_materialSource.paletteId};
	const auto revision = m_materialSource.revision;
	const auto source = m_document.recoverySource().isEmpty() ? m_document.path() : m_document.recoverySource();
	ModelWeaponBundle candidate;
	if (!performWork(
			Text::tr("Review Weapon Package"),
			[&](QString *failure, const ModelWorkControl &control) {
				return prepareModelWeaponBundle(assembly, resolved, context, options, source, &candidate, failure, control);
			},
			error))
		return false;
	candidate.package.contextRevision = revision;
	*result = std::move(candidate);
	report(Text::tr("Weapon package ready for review: %Ln file(s).", nullptr, int(result->package.files.size())), LiveStatus::Result);
	return true;
}

bool ModelAssemblyDialog::exportWeaponBundle(const ModelWeaponBundle &bundle, const QString &path, bool overwrite, PackageWriteReport *result,
											 QString *error)
{
	if (result)
		*result = {};
	if (error)
		error->clear();
	if (bundle.package.recipeSha256 != modelAssemblyFingerprint(m_document.assembly()) ||
		bundle.package.sourceArchive != m_materialSource.archive || bundle.package.contextRevision != m_materialSource.revision)
	{
		if (error)
			*error = Text::tr("The assembly or package context changed. Review the weapon package again before exporting.");
		return false;
	}
	play(false);
	PackageWriteReport written;
	const bool completed = performWork(
		Text::tr("Export Weapon Package"),
		[&](QString *failure, const ModelWorkControl &control) {
			written = writeModelWeaponBundle(bundle, path, overwrite, false, control);
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

void ModelAssemblyDialog::chooseWeaponBundle()
{
	if (m_working)
		return;
	play(false);
	QDialog dialog(this);
	dialog.setObjectName(QStringLiteral("weaponBundleDialog"));
	dialog.setWindowTitle(Text::tr("Quake III Weapon Package"));
	dialog.setAccessibleName(dialog.windowTitle());
	auto *layout = new QVBoxLayout(&dialog);
	// The choices scroll, so detail levels and sounds stay reachable at large text sizes.
	auto *scroll = new QScrollArea;
	scroll->setObjectName(QStringLiteral("weaponBundleOptions"));
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	auto *body = new QWidget;
	auto *form = new QFormLayout(body);
	form->setRowWrapPolicy(QFormLayout::WrapAllRows);
	form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	scroll->setWidget(body);
	layout->addWidget(scroll, 2);
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
	field(name, "weaponBundleName", Text::tr("Weapon ID"),
		  Text::tr("The folder and file name under models/weapons2. Starts with a letter; ASCII letters, digits, underscores and hyphens."));
	const auto partChoice = [&](const char *id, const QString &label, const QString &description, const QString &tagHint, bool optional) {
		auto *combo = new QComboBox;
		combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
		combo->setMinimumContentsLength(8);
		if (optional)
			combo->addItem(Text::tr("None"), QString());
		for (const auto &part : m_document.assembly().parts)
		{
			combo->addItem(part.id, part.id);
			if (!tagHint.isEmpty() && part.tag == tagHint && combo->currentIndex() <= 0)
				combo->setCurrentIndex(combo->count() - 1);
		}
		field(combo, id, label, description);
		return combo;
	};
	auto *weapon = partChoice("weaponBundleWeapon", Text::tr("Weapon part"),
							  Text::tr("The gun: the root, or on a tag_weapon. It needs tag_flash, and tag_barrel for a barrel."),
							  QStringLiteral("tag_weapon"), false);
	auto *hand = partChoice("weaponBundleHand", Text::tr("Hand part"),
							Text::tr("Optional first-person hand: the root, holding the weapon on its tag_weapon."), QString(), true);
	auto *barrel = partChoice("weaponBundleBarrel", Text::tr("Barrel part"),
							  Text::tr("Optional barrel on the weapon's tag_barrel; the game rolls it while firing."),
							  QStringLiteral("tag_barrel"), true);
	auto *flash = partChoice("weaponBundleFlash", Text::tr("Flash part"),
							 Text::tr("Optional single-pose muzzle flash on the weapon's tag_flash."), QStringLiteral("tag_flash"), true);
	auto *detail = new QComboBox;
	detail->addItems({Text::tr("None"), Text::tr("Generate"), Text::tr("Each part's stored detail levels")});
	field(detail, "weaponBundleDetail", Text::tr("Detail levels"),
		  Text::tr("Quake III loads a model's _1.md3 and _2.md3 files beside it as lower-detail versions by distance. Generate "
				   "reduces each role model; stored detail levels use each part's own recipe."));
	auto *levels = new QSpinBox;
	levels->setRange(1, 2);
	levels->setValue(2);
	levels->setLayoutDirection(Qt::LeftToRight);
	field(levels, "weaponBundleLodLevels", Text::tr("Generated levels"),
		  Text::tr("How many detail levels to generate per model, up to the 2 Quake III loads."));
	auto *ratio = new QDoubleSpinBox;
	ratio->setRange(0.05, 0.95);
	ratio->setSingleStep(0.05);
	ratio->setDecimals(2);
	ratio->setValue(0.5);
	ratio->setLayoutDirection(Qt::LeftToRight);
	field(ratio, "weaponBundleLodRatio", Text::tr("Share kept per level"),
		  Text::tr("The share of the level above's triangles each generated level keeps."));
	const auto syncDetail = [=] {
		levels->setEnabled(detail->currentIndex() == 1);
		ratio->setEnabled(detail->currentIndex() == 1);
	};
	syncDetail();
	auto *sounds = new QTableWidget(0, 2);
	sounds->setObjectName(QStringLiteral("weaponBundleSounds"));
	sounds->setHorizontalHeaderLabels({Text::tr("Sound"), Text::tr("Source")});
	sounds->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed | QAbstractItemView::AnyKeyPressed);
	sounds->setSelectionBehavior(QAbstractItemView::SelectItems);
	sounds->setSelectionMode(QAbstractItemView::SingleSelection);
	sounds->setTabKeyNavigation(false);
	sounds->setTextElideMode(Qt::ElideMiddle);
	sounds->verticalHeader()->setVisible(false);
	sounds->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
	sounds->setLayoutDirection(Qt::LeftToRight);
	sounds->setMinimumHeight(sounds->fontMetrics().lineSpacing() * 5);
	{
		const auto label = Text::tr("Weapon sounds");
		sounds->setAccessibleName(label);
		sounds->setAccessibleDescription(Text::tr("One row per sound: its name, published under sound/weapons/ in a folder named after the weapon ID, and a sound "
												  "file or package:PATH. Mono PCM WAV is kept; other audio is converted to 22,050 Hz mono "
												  "16-bit PCM. The weapon's game code chooses which sound plays when. Press F2 to edit a cell."));
		sounds->setToolTip(sounds->accessibleDescription());
		auto *text = new QLabel(label);
		text->setTextFormat(Qt::PlainText);
		text->setWordWrap(true);
		text->setBuddy(sounds);
		form->addRow(text);
		form->addRow(sounds);
	}
	auto *soundButtons = new QHBoxLayout;
	auto *addSound = new QPushButton(Text::tr("Add Sound"));
	auto *removeSound = new QPushButton(Text::tr("Remove Sound"));
	addSound->setObjectName(QStringLiteral("weaponBundleAddSound"));
	removeSound->setObjectName(QStringLiteral("weaponBundleRemoveSound"));
	addSound->setAccessibleDescription(Text::tr("Add a row for another weapon sound; type its name and source."));
	removeSound->setAccessibleDescription(Text::tr("Remove the selected sound row."));
	for (auto *control : {addSound, removeSound})
	{
		control->setAutoDefault(false);
		control->setAccessibleName(control->text());
		control->setToolTip(control->accessibleDescription());
		soundButtons->addWidget(control);
	}
	soundButtons->addStretch();
	form->addRow(soundButtons);
	auto *files = new QTreeWidget;
	files->setObjectName(QStringLiteral("weaponBundleFiles"));
	files->setAccessibleName(Text::tr("Reviewed weapon package files"));
	files->setAccessibleDescription(Text::tr("Output path, role and byte count. Details show each file's content fingerprint."));
	files->setHeaderLabels({Text::tr("Package path"), Text::tr("Role"), Text::tr("Bytes")});
	files->setRootIsDecorated(false);
	files->setTextElideMode(Qt::ElideMiddle);
	files->setUniformRowHeights(true);
	files->header()->setSectionResizeMode(0, QHeaderView::Stretch);
	layout->addWidget(files, 1);
	auto *details = new QPlainTextEdit;
	details->setObjectName(QStringLiteral("weaponBundleDetails"));
	details->setAccessibleName(Text::tr("Weapon package review details"));
	details->setReadOnly(true);
	details->setTabChangesFocus(true);
	layout->addWidget(details, 1);
	auto *status = new QLabel(Text::tr("Choose the weapon ID and parts, then review the package."));
	status->setObjectName(QStringLiteral("weaponBundleStatus"));
	status->setWordWrap(true);
	status->setTextFormat(Qt::PlainText);
	status->setAccessibleName(Text::tr("Weapon package status"));
	layout->addWidget(status);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
	auto *review = buttons->addButton(Text::tr("Review Package"), QDialogButtonBox::ActionRole);
	auto *publish = buttons->addButton(Text::tr("Export PK3…"), QDialogButtonBox::ActionRole);
	review->setObjectName(QStringLiteral("weaponBundleReview"));
	publish->setObjectName(QStringLiteral("weaponBundleExport"));
	for (auto *button : {review, publish})
	{
		button->setAutoDefault(false);
		button->setAccessibleName(button->text());
	}
	publish->setEnabled(false);
	layout->addWidget(buttons);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	std::optional<ModelWeaponBundle> prepared;
	const auto invalidate = [&] {
		prepared.reset();
		files->clear();
		details->clear();
		publish->setEnabled(false);
	};
	connect(name, &QLineEdit::textChanged, &dialog, invalidate);
	for (auto *combo : {weapon, hand, barrel, flash, detail})
		connect(combo, &QComboBox::currentIndexChanged, &dialog, invalidate);
	connect(detail, &QComboBox::currentIndexChanged, &dialog, syncDetail);
	connect(levels, &QSpinBox::valueChanged, &dialog, invalidate);
	connect(ratio, &QDoubleSpinBox::valueChanged, &dialog, invalidate);
	connect(sounds, &QTableWidget::itemChanged, &dialog, invalidate);
	connect(addSound, &QPushButton::clicked, &dialog, [&] {
		const int row = sounds->rowCount();
		sounds->insertRow(row);
		sounds->setItem(row, 0, new QTableWidgetItem);
		sounds->setItem(row, 1, new QTableWidgetItem);
		sounds->setCurrentCell(row, 0);
		sounds->setFocus();
		invalidate();
	});
	connect(removeSound, &QPushButton::clicked, &dialog, [&] {
		if (sounds->currentRow() >= 0)
			sounds->removeRow(sounds->currentRow());
		invalidate();
	});
	connect(files, &QTreeWidget::itemSelectionChanged, &dialog, [&] {
		if (!prepared)
			return;
		QString text;
		if (const auto *row = files->currentItem())
			text = row->data(0, Qt::AccessibleDescriptionRole).toString() + QStringLiteral("\n\n");
		details->setPlainText(text + prepared->package.notes.join(QStringLiteral("\n\n")));
	});
	connect(review, &QPushButton::clicked, &dialog, [&] {
		invalidate();
		ModelWeaponBundleOptions options{name->text().trimmed(), weapon->currentData().toString(), hand->currentData().toString(),
										 barrel->currentData().toString(), flash->currentData().toString()};
		options.detailLevels = ModelPlayerDetailLevels(detail->currentIndex());
		options.lodLevels = levels->value();
		options.lodRatio = ratio->value();
		const auto cell = [&](int row, int column) {
			const auto *item = sounds->item(row, column);
			return item ? item->text().trimmed() : QString();
		};
		for (int row = 0; row < sounds->rowCount(); ++row)
			if (!cell(row, 0).isEmpty() || !cell(row, 1).isEmpty())
				options.sounds.append({cell(row, 0), modelPlayerReference(cell(row, 1))});
		ModelWeaponBundle candidate;
		QString error;
		if (!prepareWeaponBundle(options, &candidate, &error))
		{
			status->setText(error);
			return;
		}
		for (const auto &file : candidate.package.files)
		{
			auto *row = new QTreeWidgetItem(files, {file.path, file.role, QLocale().toString(file.bytes.size())});
			row->setData(0, Qt::AccessibleDescriptionRole,
						 Text::tr("%1\nSHA-256: %2").arg(file.path, QString::fromLatin1(file.sha256.toHex())));
			row->setToolTip(0, file.path);
		}
		details->setPlainText(candidate.package.notes.join(QStringLiteral("\n\n")));
		prepared = std::move(candidate);
		publish->setEnabled(true);
		status->setText(Text::tr("Review ready: %Ln file(s). Export writes exactly these bytes.", nullptr, int(prepared->package.files.size())));
	});
	connect(publish, &QPushButton::clicked, &dialog, [&] {
		if (!prepared)
			return;
		const auto path = QFileDialog::getSaveFileName(&dialog, Text::tr("Export Weapon Package"), {}, Text::tr("Quake III package (*.pk3)"));
		if (path.isEmpty())
			return;
		PackageWriteReport written;
		QString error;
		if (!exportWeaponBundle(*prepared, path, true, &written, &error))
			status->setText(error);
		else
			status->setText(packageWriteReportText(written));
	});
	fitDialogToScreen(&dialog, QSize(640, 640));
	dialog.exec();
}
} // namespace vibestudio
