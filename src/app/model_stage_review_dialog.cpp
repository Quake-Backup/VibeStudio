#include "app/model_stage_review_dialog.h"

#include "app/dialog_screen_fit.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QLocale>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace vibestudio
{
namespace
{
QString roleText(const QString &role)
{
	if (role == QStringLiteral("detail-level"))
		return QCoreApplication::translate("VibeStudioModelEditor", "Detail level");
	if (role == QStringLiteral("animation"))
		return QCoreApplication::translate("VibeStudioModelEditor", "Animation clip");
	if (role == QStringLiteral("skin"))
		return QCoreApplication::translate("VibeStudioModelEditor", "Skin");
	return QCoreApplication::translate("VibeStudioModelEditor", "Model");
}
QTableWidget *table(const QString &name, const QString &accessibleName, const QStringList &headers)
{
	auto *widget = new QTableWidget(0, int(headers.size()));
	widget->setObjectName(name);
	widget->setAccessibleName(accessibleName);
	widget->setHorizontalHeaderLabels(headers);
	widget->setEditTriggers(QAbstractItemView::NoEditTriggers);
	widget->setSelectionBehavior(QAbstractItemView::SelectRows);
	widget->setTabKeyNavigation(false);
	widget->verticalHeader()->hide();
	widget->horizontalHeader()->setStretchLastSection(true);
	widget->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
	widget->setWordWrap(false);
	return widget;
}
QTableWidgetItem *cell(const QString &text, const QString &tip = {})
{
	auto *item = new QTableWidgetItem(text);
	item->setToolTip(tip.isEmpty() ? text : tip);
	return item;
}
} // namespace

ModelStageReviewDialog::ModelStageReviewDialog(const ModelStageReview &review, bool place, bool replaceExisting, QWidget *parent)
	: QDialog(parent), m_review(review)
{
	setObjectName(QStringLiteral("modelStageReviewDialog"));
	setWindowTitle(place ? QCoreApplication::translate("VibeStudioModelEditor", "Review Stage and Place")
						 : QCoreApplication::translate("VibeStudioModelEditor", "Review Stage in Package"));
	setAccessibleName(windowTitle());
	auto *layout = new QVBoxLayout(this);
	const QLocale locale;
	const int files = int(review.entries.size());
	auto *summary = new QLabel(
		QCoreApplication::translate("VibeStudioModelEditor", "%n file(s), %1 in total, staged as one package step.", nullptr, files)
			.arg(locale.formattedDataSize(review.totalBytes())) +
		(review.packagePath.isEmpty() ? QString()
									  : QLatin1Char(' ') + QCoreApplication::translate("VibeStudioModelEditor", "Package: %1")
															   .arg(QFileInfo(review.packagePath).fileName())));
	summary->setObjectName(QStringLiteral("modelStageReviewSummary"));
	summary->setWordWrap(true);
	summary->setTextFormat(Qt::PlainText);
	summary->setToolTip(review.packagePath);
	layout->addWidget(summary);

	auto *filesTable = table(QStringLiteral("modelStageReviewFiles"), QCoreApplication::translate("VibeStudioModelEditor", "Files to stage"),
							 {QCoreApplication::translate("VibeStudioModelEditor", "Package path"),
							  QCoreApplication::translate("VibeStudioModelEditor", "Role"),
							  QCoreApplication::translate("VibeStudioModelEditor", "Size"),
							  QCoreApplication::translate("VibeStudioModelEditor", "In the package")});
	filesTable->setAccessibleDescription(
		QCoreApplication::translate("VibeStudioModelEditor", "Each file this step stages, its role and size, and whether it is new or replaces a file."));
	filesTable->setRowCount(files);
	for (int row = 0; row < files; ++row)
	{
		const auto &entry = review.entries.at(row);
		filesTable->setItem(row, 0, cell(entry.virtualPath));
		filesTable->setItem(row, 1, cell(roleText(entry.role)));
		filesTable->setItem(row, 2, cell(locale.formattedDataSize(entry.bytes)));
		filesTable->setItem(row, 3, cell(modelStageEntryStateText(entry.state)));
	}
	filesTable->resizeColumnsToContents();
	layout->addWidget(filesTable, 2);

	auto *materials = table(QStringLiteral("modelStageReviewMaterials"), QCoreApplication::translate("VibeStudioModelEditor", "Materials the model uses"),
							{QCoreApplication::translate("VibeStudioModelEditor", "Material"),
							 QCoreApplication::translate("VibeStudioModelEditor", "Status")});
	materials->setAccessibleDescription(QCoreApplication::translate(
		"VibeStudioModelEditor", "Each material or skin the model names, resolved against the package; missing ones draw as missing textures in game."));
	const auto &dependencies = review.dependencies.dependencies;
	materials->setRowCount(int(dependencies.size()));
	for (int row = 0; row < dependencies.size(); ++row)
	{
		const auto &dependency = dependencies.at(row);
		materials->setItem(row, 0, cell(dependency.reference, dependency.resolvedPath.isEmpty() ? dependency.reference : dependency.resolvedPath));
		materials->setItem(row, 1, cell(levelDependencyStatusName(dependency.status), dependency.note));
	}
	materials->resizeColumnsToContents();
	auto *materialsLabel = new QLabel(
		!review.dependenciesReviewed
			? QCoreApplication::translate("VibeStudioModelEditor", "Materials were not reviewed: no package view is open.")
		: review.dependencies.missingCount > 0
			? QCoreApplication::translate("VibeStudioModelEditor", "%n material(s) are missing from the package.", nullptr,
										  review.dependencies.missingCount)
			: QCoreApplication::translate("VibeStudioModelEditor", "Every material the model uses is in the package or the game."));
	materialsLabel->setObjectName(QStringLiteral("modelStageReviewMaterialsSummary"));
	materialsLabel->setWordWrap(true);
	materialsLabel->setBuddy(materials);
	layout->addWidget(materialsLabel);
	layout->addWidget(materials, 1);

	if (!review.notes.isEmpty())
	{
		auto *notes = new QListWidget;
		notes->setObjectName(QStringLiteral("modelStageReviewNotes"));
		notes->setAccessibleName(QCoreApplication::translate("VibeStudioModelEditor", "Export notes"));
		notes->setWordWrap(true);
		notes->addItems(review.notes);
		layout->addWidget(notes, 1);
	}

	m_replace = new QCheckBox(QCoreApplication::translate("VibeStudioModelEditor", "&Replace existing package files"));
	m_replace->setObjectName(QStringLiteral("modelStageReviewReplace"));
	m_replace->setChecked(replaceExisting);
	m_replace->setAccessibleName(QCoreApplication::translate("VibeStudioModelEditor", "Replace existing package files"));
	m_replace->setAccessibleDescription(
		QCoreApplication::translate("VibeStudioModelEditor", "Stage over files the package or the staging plan already holds at these paths."));
	layout->addWidget(m_replace);
	m_blocked = new QLabel;
	m_blocked->setObjectName(QStringLiteral("modelStageReviewBlocked"));
	m_blocked->setWordWrap(true);
	m_blocked->setTextFormat(Qt::PlainText);
	layout->addWidget(m_blocked);

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
	m_stage = buttons->addButton(place ? QCoreApplication::translate("VibeStudioModelEditor", "Stage and &Place")
									   : QCoreApplication::translate("VibeStudioModelEditor", "&Stage"),
								 QDialogButtonBox::AcceptRole);
	m_stage->setObjectName(QStringLiteral("modelStageReviewStage"));
	m_stage->setAccessibleName(place ? QCoreApplication::translate("VibeStudioModelEditor", "Stage and place")
									 : QCoreApplication::translate("VibeStudioModelEditor", "Stage"));
	m_stage->setAccessibleDescription(QCoreApplication::translate("VibeStudioModelEditor", "Stage every listed file as one package step."));
	m_stage->setDefault(true);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(m_replace, &QCheckBox::toggled, this, [this] { refreshState(); });
	layout->addWidget(buttons);
	refreshState();
	fitDialogToScreen(this, QSize(760, 560));
}

bool ModelStageReviewDialog::replaceExisting() const { return m_replace->isChecked(); }

void ModelStageReviewDialog::refreshState()
{
	const int replaced = m_review.conflicts();
	const bool blocked = replaced > 0 && !m_replace->isChecked();
	m_stage->setEnabled(!m_review.entries.isEmpty() && !blocked);
	m_replace->setEnabled(replaced > 0);
	m_blocked->setText(blocked ? QCoreApplication::translate(
									 "VibeStudioModelEditor", "%n file(s) are already in the package. Choose Replace existing package files to stage over them.",
									 nullptr, replaced)
					   : replaced > 0 ? QCoreApplication::translate("VibeStudioModelEditor", "%n existing file(s) will be replaced.", nullptr, replaced)
									  : QString());
	m_blocked->setVisible(!m_blocked->text().isEmpty());
	m_stage->setToolTip(m_blocked->text());
}

} // namespace vibestudio
