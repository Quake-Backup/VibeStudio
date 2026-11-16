#include "app/model_skin_source_dialog.h"
#include "app/dialog_screen_fit.h"

#include <QAbstractTableModel>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QVBoxLayout>

#include <limits>

namespace slopstudio
{
namespace
{
class Entries final : public QAbstractTableModel
{
  public:
	Entries(QVector<PackageEntry> entries, bool bindings, QObject *parent)
		: QAbstractTableModel(parent), m_entries(std::move(entries)), m_bindings(bindings)
	{
	}
	int rowCount(const QModelIndex &parent = {}) const override
	{
		return parent.isValid() ? 0 : int(qMin<qsizetype>(m_entries.size(), std::numeric_limits<int>::max()));
	}
	int columnCount(const QModelIndex &parent = {}) const override
	{
		return parent.isValid() ? 0 : 4;
	}
	QVariant data(const QModelIndex &index, int role) const override
	{
		if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size())
		{
			return {};
		}
		const auto &entry = m_entries[index.row()];
		if (role == Qt::UserRole)
		{
			return index.row();
		}
		if (role == Qt::UserRole + 1)
		{
			return entry.virtualPath;
		}
		if (role == Qt::ToolTipRole)
		{
			return entry.virtualPath;
		}
		if (role != Qt::DisplayRole && role != Qt::AccessibleTextRole)
		{
			return {};
		}
		switch (index.column())
		{
		case 0:
			return index.row();
		case 1:
			return entry.virtualPath;
		case 2:
			return entry.typeHint;
		case 3:
			return QLocale().formattedDataSize(qint64(entry.sizeBytes));
		default:
			return {};
		}
	}
	QVariant headerData(int section, Qt::Orientation orientation, int role) const override
	{
		if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
		{
			return {};
		}
		switch (section)
		{
		case 0:
			return QCoreApplication::translate("SlopStudioModelSkinSource", "Entry (0-based)");
		case 1:
			return QCoreApplication::translate("SlopStudioModelSkinSource", "Package path");
		case 2:
			return QCoreApplication::translate("SlopStudioModelSkinSource", "Type");
		case 3:
			return QCoreApplication::translate("SlopStudioModelSkinSource", "Size");
		default:
			return {};
		}
	}
	Qt::ItemFlags flags(const QModelIndex &index) const override
	{
		if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size())
		{
			return Qt::NoItemFlags;
		}
		const auto &entry = m_entries[index.row()];
		return entry.kind == PackageEntryKind::File && entry.readable &&
					   (!m_bindings || entry.virtualPath.endsWith(QStringLiteral(".skin"), Qt::CaseInsensitive))
				   ? Qt::ItemIsEnabled | Qt::ItemIsSelectable
				   : Qt::NoItemFlags;
	}

  private:
	QVector<PackageEntry> m_entries;
	bool m_bindings = false;
};
} // namespace

ModelSkinSourceDialog::ModelSkinSourceDialog(QVector<PackageEntry> entries, int skin, int member, double duration, QWidget *parent,
											 ModelSkinSourcePurpose purpose)
	: QDialog(parent)
{
	const bool preview = purpose == ModelSkinSourcePurpose::PreviewBindings;
	const bool bindings = purpose == ModelSkinSourcePurpose::ShaderBindings || preview;
	const bool image = purpose == ModelSkinSourcePurpose::Image;
	setObjectName(QStringLiteral("meshSkinSourceDialog"));
	setWindowTitle(image ? QCoreApplication::translate("SlopStudioModelSkinSource", "Choose Package Image")
		: preview ? QCoreApplication::translate("SlopStudioModelSkinSource", "Preview Package Skin")
		: bindings ? QCoreApplication::translate("SlopStudioModelSkinSource", "Apply Package Skin Assignments")
							: QCoreApplication::translate("SlopStudioModelSkinSource", "Import Package Texture"));
	setAccessibleName(windowTitle());
	auto *layout = new QVBoxLayout(this);
	auto *form = new QFormLayout;
	form->setRowWrapPolicy(QFormLayout::WrapLongRows);
	layout->addLayout(form);
	auto *filter = new QLineEdit;
	filter->setObjectName(QStringLiteral("meshSkinSourceFilter"));
	filter->setAccessibleName(QCoreApplication::translate("SlopStudioModelSkinSource", "Filter package paths"));
	filter->setAccessibleDescription(
		bindings ? QCoreApplication::translate("SlopStudioModelSkinSource", "Find a staged or existing .skin file by its package path.")
				 : QCoreApplication::translate("SlopStudioModelSkinSource", "Find a staged or existing texture by its package path."));
	form->addRow(filter->accessibleName(), filter);
	if (!bindings && !image)
	{
		m_operation = new QComboBox;
		m_operation->setObjectName(QStringLiteral("meshSkinSourceOperation"));
		m_operation->setAccessibleName(QCoreApplication::translate("SlopStudioModelSkinSource", "Skin operation"));
		m_operation->setAccessibleDescription(QCoreApplication::translate(
			"SlopStudioModelSkinSource", "Add a new skin slot, or replace or append a member in the selected skin."));
		m_operation->addItem(QCoreApplication::translate("SlopStudioModelSkinSource", "Add skin slot"), int(ModelEditKind::AddMdlSkin));
		if (skin >= 0 && member >= 0)
		{
			m_operation->addItem(
				QCoreApplication::translate("SlopStudioModelSkinSource", "Replace skin %1, member %2").arg(skin).arg(member),
				int(ModelEditKind::ReplaceMdlSkinMember));
			m_operation->addItem(
				QCoreApplication::translate("SlopStudioModelSkinSource", "Append to skin %1 (%2 s)").arg(skin).arg(duration),
				int(ModelEditKind::AppendMdlSkinMember));
		}
		form->addRow(m_operation->accessibleName(), m_operation);
	}
	m_entries = new QTableView;
	m_entries->setObjectName(QStringLiteral("meshSkinSourceEntries"));
	m_entries->setAccessibleName(bindings ? QCoreApplication::translate("SlopStudioModelSkinSource", "Package skin assignment entries")
										  : QCoreApplication::translate("SlopStudioModelSkinSource", "Package texture entries"));
	m_entries->setAccessibleDescription(
		image ? QCoreApplication::translate("SlopStudioModelSkinSource", "Choose an exact image entry. Its bytes are read and validated by the requesting editor after selection.")
		: preview ? QCoreApplication::translate("SlopStudioModelSkinSource", "Select an exact .skin entry for this preview. Original model bindings and package contents stay unchanged.") : bindings
			? QCoreApplication::translate(
				  "SlopStudioModelSkinSource",
				  "Select an exact .skin entry. Its shader assignments will replace primary surface materials in one undo step.")
			: QCoreApplication::translate("SlopStudioModelSkinSource", "Exact entry numbers distinguish repeated paths. Import copies "
																	   "indexed pixels into the model; package changes remain staged."));
	m_entries->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_entries->setSelectionMode(QAbstractItemView::SingleSelection);
	m_entries->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_entries->setAlternatingRowColors(true);
	m_entries->setWordWrap(false);
	m_entries->verticalHeader()->hide();
	auto *proxy = new QSortFilterProxyModel(m_entries);
	proxy->setSourceModel(new Entries(std::move(entries), bindings, proxy));
	proxy->setFilterKeyColumn(1);
	proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
	m_entries->setModel(proxy);
	m_entries->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
	m_entries->horizontalHeader()->setResizeContentsPrecision(100);
	m_entries->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
	layout->addWidget(m_entries, 1);
	auto *status = new QLabel(QCoreApplication::translate("SlopStudioModelSkinSource", "No entry selected"));
	status->setObjectName(QStringLiteral("meshSkinSourceSelection"));
	status->setTextFormat(Qt::PlainText);
	status->setWordWrap(true);
	status->setMinimumWidth(0);
	status->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	layout->addWidget(status);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
	auto *import = buttons->addButton(image ? QCoreApplication::translate("SlopStudioModelSkinSource", "Choose Image")
		: preview ? QCoreApplication::translate("SlopStudioModelSkinSource", "Preview Skin")
		: bindings ? QCoreApplication::translate("SlopStudioModelSkinSource", "Apply Assignments")
											   : QCoreApplication::translate("SlopStudioModelSkinSource", "Import Skin"),
									  QDialogButtonBox::AcceptRole);
	import->setObjectName(QStringLiteral("meshSkinSourceImport"));
	import->setAccessibleName(import->text());
	import->setAccessibleDescription(
		image ? QCoreApplication::translate("SlopStudioModelSkinSource", "Return the selected image path and exact package index without modifying any asset.")
		: preview ? QCoreApplication::translate("SlopStudioModelSkinSource", "Validate complete surface coverage and preview the selected shader paths without editing the model.")
		: bindings ? QCoreApplication::translate("SlopStudioModelSkinSource",
											   "Validate complete surface coverage, then apply shader paths without changing the package.")
				 : QCoreApplication::translate("SlopStudioModelSkinSource",
											   "Read and validate the selected indexed image, then apply one undoable skin edit."));
	import->setEnabled(false);
	layout->addWidget(buttons);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(filter, &QLineEdit::textChanged, proxy, &QSortFilterProxyModel::setFilterFixedString);
	connect(m_entries->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this, import, status] {
		const auto selected = reference();
		import->setEnabled(selected.entryIndex >= 0);
		status->setText(
			selected.entryIndex >= 0
				? QCoreApplication::translate("SlopStudioModelSkinSource", "Entry %1: %2").arg(selected.entryIndex).arg(selected.path)
				: QCoreApplication::translate("SlopStudioModelSkinSource", "No entry selected"));
	});
	if (bindings)
	{
		filter->setText(QStringLiteral(".skin"));
	}
	filter->setFocus();
	fitDialogToScreen(this, QSize(820, 530));
}
ModelSkinSourceReference ModelSkinSourceDialog::reference() const
{
	const auto selected = m_entries->selectionModel()->selectedRows();
	if (selected.size() != 1 || !(selected[0].flags() & Qt::ItemIsSelectable))
	{
		return {};
	}
	return {selected[0].data(Qt::UserRole + 1).toString(), selected[0].data(Qt::UserRole).toInt()};
}
ModelEditKind ModelSkinSourceDialog::kind() const
{
	return m_operation ? ModelEditKind(m_operation->currentData().toInt()) : ModelEditKind::ApplySkinBindings;
}
} // namespace slopstudio
