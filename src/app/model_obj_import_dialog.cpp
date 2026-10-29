#include "app/model_obj_import_dialog.h"

#include "app/dialog_screen_fit.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelObjImportDialog)
};
void describe(QWidget *widget, const QString &label, const QString &description)
{
	widget->setAccessibleName(label);
	widget->setAccessibleDescription(description);
	widget->setToolTip(description);
}
ModelObjImportOptions &remembered()
{
	static ModelObjImportOptions options;
	return options;
}
} // namespace

ModelObjImportDialog::ModelObjImportDialog(QWidget *parent) : QDialog(parent)
{
	setObjectName(QStringLiteral("modelObjImportDialog"));
	setWindowTitle(Text::tr("OBJ Import Settings"));
	describe(this, windowTitle(), Text::tr("How to read this OBJ: axes, scale, material libraries and loose data."));
	auto *layout = new QVBoxLayout(this);
	auto *intro = new QLabel(Text::tr("Blender and most modellers export Y up and -Z forward; idTech models are Z up."));
	intro->setWordWrap(true);
	intro->setTextFormat(Qt::PlainText);
	layout->addWidget(intro);
	auto *form = new QFormLayout;
	form->setRowWrapPolicy(QFormLayout::WrapLongRows);
	form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	layout->addLayout(form);
	const auto combo = [&](const char *id, const QString &label, const QString &description, const QStringList &items)
	{
		auto *field = new QComboBox;
		field->setObjectName(QString::fromLatin1(id));
		field->addItems(items);
		field->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
		field->setMinimumContentsLength(12);
		describe(field, label, description);
		auto *caption = new QLabel(label);
		caption->setBuddy(field);
		form->addRow(caption, field);
		return field;
	};
	m_up = combo("objUpAxis", Text::tr("&Up axis"), Text::tr("Y up converts Blender's default export to Z up; Z up keeps coordinates."),
				 {Text::tr("Z up (as idTech)"), Text::tr("Y up (Blender default)")});
	m_scale = new QDoubleSpinBox;
	m_scale->setObjectName(QStringLiteral("objScale"));
	m_scale->setRange(0.000001, 1000000);
	m_scale->setDecimals(6);
	m_scale->setValue(1);
	m_scale->setLayoutDirection(Qt::LeftToRight);
	describe(m_scale, Text::tr("Scale"), Text::tr("Multiplies positions after the axis conversion."));
	auto *scaleCaption = new QLabel(Text::tr("&Scale"));
	scaleCaption->setBuddy(m_scale);
	form->addRow(scaleCaption, m_scale);
	m_materials = combo("objMaterials", Text::tr("&Material libraries"),
						Text::tr("Read the material library beside the OBJ and bind each material's diffuse map, keep material "
								 "names only, or refuse files that name a library."),
						{Text::tr("Read and bind diffuse maps"), Text::tr("Keep material names"), Text::tr("Refuse")});
	m_loose = combo("objLooseData", Text::tr("&Lines, points and unused vertices"),
					Text::tr("Drop them with a note in the import report, or refuse the file."),
					{Text::tr("Drop with a note"), Text::tr("Refuse")});
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	auto *ok = buttons->button(QDialogButtonBox::Ok);
	ok->setObjectName(QStringLiteral("objImportAccept"));
	ok->setText(Text::tr("Import"));
	describe(ok, ok->text(), Text::tr("Import the OBJ with these settings."));
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	layout->addWidget(buttons);
	setOptions(remembered());
	fitDialogToScreen(this, QSize(std::max(420, fontMetrics().averageCharWidth() * 60), 300));
}

ModelObjImportOptions ModelObjImportDialog::options() const
{
	ModelObjImportOptions result;
	result.up = m_up->currentIndex() == 1 ? ModelObjUpAxis::Y : ModelObjUpAxis::Z;
	result.scale = m_scale->value();
	result.materials = m_materials->currentIndex() == 2   ? ModelObjMaterials::Refuse
					   : m_materials->currentIndex() == 1 ? ModelObjMaterials::Ignore
														  : ModelObjMaterials::Resolve;
	result.refuseLooseData = m_loose->currentIndex() == 1;
	return result;
}

void ModelObjImportDialog::setOptions(const ModelObjImportOptions &options)
{
	m_up->setCurrentIndex(options.up == ModelObjUpAxis::Y ? 1 : 0);
	m_scale->setValue(options.scale);
	m_materials->setCurrentIndex(options.materials == ModelObjMaterials::Refuse ? 2 : options.materials == ModelObjMaterials::Ignore ? 1 : 0);
	m_loose->setCurrentIndex(options.refuseLooseData ? 1 : 0);
}

bool ModelObjImportDialog::ask(QWidget *parent, ModelObjImportOptions *options)
{
	ModelObjImportDialog dialog(parent);
	if (dialog.exec() != QDialog::Accepted)
		return false;
	remembered() = dialog.options();
	if (options)
		*options = remembered();
	return true;
}
} // namespace vibestudio
