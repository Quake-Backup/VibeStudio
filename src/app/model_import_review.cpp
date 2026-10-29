#include "app/model_import_review.h"

#include "core/model_decode_issues.h"

#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSet>
#include <QVBoxLayout>

#include <algorithm>

namespace vibestudio
{

QString modelEditorOpenPatterns()
{
	QStringList patterns{QStringLiteral("*.mesh.json"), QStringLiteral("*.model.json")};
	for (const ModelFormatCapability &format : modelFormatCapabilities())
	{
		if (format.animationOnly)
		{
			continue;
		}
		for (const QString &suffix : format.suffixes)
		{
			const auto pattern = QStringLiteral("*.") + suffix;
			if (!patterns.contains(pattern))
			{
				patterns << pattern;
			}
		}
	}
	return patterns.join(QLatin1Char(' '));
}

bool reviewModelImportIssues(QWidget *parent, const QString &fileName, const QVector<ModelDecodeIssue> &issues)
{
	QDialog dialog(parent);
	dialog.setObjectName(QStringLiteral("modelImportReview"));
	dialog.setWindowTitle(QCoreApplication::translate("VibeStudioModelEditor", "Review Import"));
	dialog.setAccessibleName(dialog.windowTitle());
	auto *layout = new QVBoxLayout(&dialog);
	auto *summary = new QLabel(QCoreApplication::translate("VibeStudioModelEditor",
														   "%1 decoded, but the decoder left some of its data out. Accept the omissions "
														   "to edit the model without that data; saving it records what was left out. "
														   "The game file itself is not changed.")
								   .arg(QFileInfo(fileName).fileName()));
	summary->setObjectName(QStringLiteral("modelImportReviewSummary"));
	summary->setWordWrap(true);
	summary->setTextFormat(Qt::PlainText);
	layout->addWidget(summary);
	auto *counts = new QLabel(modelDecodeIssueSummary(issues));
	counts->setObjectName(QStringLiteral("modelImportReviewCounts"));
	counts->setWordWrap(true);
	counts->setTextFormat(Qt::PlainText);
	layout->addWidget(counts);
	auto *list = new QListWidget;
	list->setObjectName(QStringLiteral("modelImportReviewIssues"));
	list->setAccessibleName(QCoreApplication::translate("VibeStudioModelEditor", "Decode issues"));
	list->setAccessibleDescription(QCoreApplication::translate(
		"VibeStudioModelEditor", "Every decode issue of the model, grouped by severity: what blocks editing comes first, then advisories."));
	list->setWordWrap(true);
	list->setTextElideMode(Qt::ElideNone);
	// What decides the choice first: damage cannot be accepted, omissions can,
	// advisories need nothing. Each line names its severity in words.
	for (const auto severity : {ModelDecodeIssueSeverity::Damage, ModelDecodeIssueSeverity::Omission, ModelDecodeIssueSeverity::Advisory})
	{
		QVector<ModelDecodeIssue> group;
		for (const ModelDecodeIssue &issue : issues)
		{
			if (issue.severity == severity)
			{
				group.append(issue);
			}
		}
		for (const QString &line : modelDecodeIssueLines(group))
		{
			auto *item = new QListWidgetItem(line, list);
			item->setData(Qt::AccessibleTextRole, line);
			item->setData(Qt::UserRole, modelDecodeIssueSeverityId(severity));
		}
	}
	if (list->count() > 0)
	{
		list->setCurrentRow(0);
	}
	layout->addWidget(list, 1);
	auto *buttons = new QDialogButtonBox;
	auto *accept = buttons->addButton(QCoreApplication::translate("VibeStudioModelEditor", "Accept Omissions and Import"), QDialogButtonBox::AcceptRole);
	accept->setObjectName(QStringLiteral("acceptImportOmissions"));
	accept->setAccessibleDescription(QCoreApplication::translate("VibeStudioModelEditor",
																 "Import the model for editing without the data the decoder left out."));
	auto *cancel = buttons->addButton(QDialogButtonBox::Cancel);
	cancel->setObjectName(QStringLiteral("cancelImportReview"));
	// Leaving data out is a deliberate choice, so Enter cancels.
	accept->setAutoDefault(false);
	cancel->setDefault(true);
	// Damage cannot be accepted; the review then only explains the refusal.
	const bool damaged = std::any_of(issues.cbegin(), issues.cend(),
									 [](const ModelDecodeIssue &issue) { return issue.severity == ModelDecodeIssueSeverity::Damage; });
	accept->setEnabled(!damaged);
	QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	layout->addWidget(buttons);
	list->setFocus();
	dialog.resize(dialog.sizeHint().expandedTo(QSize(560, 360)));
	return dialog.exec() == QDialog::Accepted;
}

} // namespace vibestudio
