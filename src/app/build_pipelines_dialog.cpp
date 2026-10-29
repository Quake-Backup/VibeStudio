// The project's own build pipelines. Workflow reference: NetRadiant Custom's
// Build Menu dialog (DoBuildMenu and BuildMenuDialog_construct in
// radiant/build.cpp, registered as BuildMenuCustomize at
// radiant/mainframe.cpp:2089, 449778b3, GPL-2.0-or-later), where named builds
// are added, renamed, reordered and given command lines with variables, and
// TrenchBroom's compilation profiles (CompilationProfileManager,
// CompilationProfileEditor and CompilationVariables at 4c756be1,
// GPL-3.0-or-later) with ${...} variables. Original Qt implementation over
// VibeStudio's compiler profiles; see docs/CREDITS.md.
#include "app/build_pipelines_dialog.h"

#include "app/studio_icons.h"
#include "core/compiler_profiles.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace vibestudio {

namespace {

QPushButton* listButton(const QString& icon, const QString& text, const QString& objectName, const QString& accessibleName)
{
	auto* button = new QPushButton(studioIcon(icon), text);
	button->setObjectName(objectName);
	button->setAccessibleName(accessibleName);
	return button;
}

} // namespace

BuildPipelinesDialog::BuildPipelinesDialog(const QVector<BuildPipelineDescriptor>& pipelines, const BuildPipelineDescriptor& starter, QWidget* parent)
	: QDialog(parent)
	, m_pipelines(pipelines)
	, m_starter(starter)
{
	setObjectName(QStringLiteral("buildPipelinesDialog"));
	setWindowTitle(tr("Build Pipelines"));
	setAccessibleName(windowTitle());
	setAccessibleDescription(tr("The project's own build pipelines: each runs its compiler stages in order, with your arguments and variables."));
	setMinimumWidth(720);

	m_list = new QListWidget;
	m_list->setObjectName(QStringLiteral("buildPipelinesList"));
	m_list->setAccessibleName(tr("The project's pipelines"));
	m_list->setAccessibleDescription(tr("Select a pipeline to edit its name, stages and variables."));
	m_list->setMinimumWidth(180);
	auto* add = listButton(QStringLiteral("plus"), tr("Add"), QStringLiteral("buildPipelinesAdd"), tr("Add a pipeline"));
	add->setToolTip(tr("Add a pipeline that starts with the stages of the pipeline chosen on the Build page."));
	m_duplicate = listButton(QStringLiteral("copy"), tr("Duplicate"), QStringLiteral("buildPipelinesDuplicate"), tr("Duplicate the selected pipeline"));
	m_remove = listButton(QStringLiteral("trash"), tr("Remove"), QStringLiteral("buildPipelinesRemove"), tr("Remove the selected pipeline"));

	m_name = new QLineEdit;
	m_name->setObjectName(QStringLiteral("buildPipelineName"));
	m_name->setAccessibleName(tr("Pipeline name"));
	m_id = new QLabel;
	m_id->setObjectName(QStringLiteral("buildPipelineId"));
	m_id->setAccessibleName(tr("Pipeline id"));
	m_id->setAccessibleDescription(tr("The id the build plan and build run commands take. A new pipeline's id follows its name."));
	m_id->setToolTip(m_id->accessibleDescription());
	m_id->setTextFormat(Qt::PlainText);
	m_id->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
	m_id->setLayoutDirection(Qt::LeftToRight);
	m_description = new QLineEdit;
	m_description->setObjectName(QStringLiteral("buildPipelineDescription"));
	m_description->setAccessibleName(tr("Pipeline description"));

	m_stages = new QListWidget;
	m_stages->setObjectName(QStringLiteral("buildPipelineStages"));
	m_stages->setAccessibleName(tr("Stages"));
	m_stages->setAccessibleDescription(tr("The stages in the order they run. Clear a stage's check box to turn it off; Space toggles it."));
	m_stages->setMinimumHeight(110);
	m_stageAdd = listButton(QStringLiteral("plus"), tr("Add Stage"), QStringLiteral("buildPipelineStageAdd"), tr("Add a stage"));
	m_stageRemove = listButton(QStringLiteral("trash"), tr("Remove Stage"), QStringLiteral("buildPipelineStageRemove"), tr("Remove the selected stage"));
	m_stageUp = listButton(QStringLiteral("chevron-up"), tr("Move Up"), QStringLiteral("buildPipelineStageUp"), tr("Move the selected stage up"));
	m_stageDown = listButton(QStringLiteral("chevron-down"), tr("Move Down"), QStringLiteral("buildPipelineStageDown"), tr("Move the selected stage down"));

	m_stageName = new QLineEdit;
	m_stageName->setObjectName(QStringLiteral("buildPipelineStageName"));
	m_stageName->setAccessibleName(tr("Stage name"));
	m_stageProfile = new QComboBox;
	m_stageProfile->setObjectName(QStringLiteral("buildPipelineStageProfile"));
	m_stageProfile->setAccessibleName(tr("Compiler profile"));
	m_stageProfile->setAccessibleDescription(tr("The compiler the stage runs. Its program comes from the Toolchain settings."));
	for (const CompilerProfileDescriptor& profile : compilerProfileDescriptors()) {
		m_stageProfile->addItem(tr("%1 (%2)").arg(profile.displayName, profile.engineFamily), profile.id);
		m_stageProfile->setItemData(m_stageProfile->count() - 1, profile.description, Qt::ToolTipRole);
	}
	m_stageInput = new QComboBox;
	m_stageInput->setObjectName(QStringLiteral("buildPipelineStageInput"));
	m_stageInput->setAccessibleName(tr("Stage input"));
	m_stageInput->addItem(tr("The map"), QStringLiteral("map"));
	m_stageInput->addItem(tr("The previous stage's output"), QStringLiteral("previous"));
	m_stageArguments = new QLineEdit;
	m_stageArguments->setObjectName(QStringLiteral("buildPipelineStageArguments"));
	m_stageArguments->setAccessibleName(tr("Stage arguments"));
	m_stageArguments->setAccessibleDescription(tr("Arguments added to the compiler's own. Quotes keep spaces. ${MAP}, ${MAP_DIR}, ${MAP_NAME}, ${OUTPUT}, ${PROJECT_DIR}, ${GAME_DIR}, ${MOD}, ${CPU_COUNT} and the pipeline's variables are filled in; a switch whose variable is empty is left out."));
	m_stageArguments->setToolTip(m_stageArguments->accessibleDescription());
	m_stageArguments->setPlaceholderText(QStringLiteral("-threads ${CPU_COUNT}"));
	// Arguments are typed left to right in every language.
	m_stageArguments->setLayoutDirection(Qt::LeftToRight);

	m_variables = new QTableWidget(0, 2);
	m_variables->setObjectName(QStringLiteral("buildPipelineVariables"));
	m_variables->setAccessibleName(tr("Pipeline variables"));
	m_variables->setAccessibleDescription(tr("Your own variables: a capitalised name, used as ${NAME} in the arguments, and its value."));
	m_variables->setHorizontalHeaderLabels({tr("Name"), tr("Value")});
	m_variables->horizontalHeader()->setStretchLastSection(true);
	m_variables->verticalHeader()->setVisible(false);
	m_variables->setMinimumHeight(90);
	m_variableAdd = listButton(QStringLiteral("plus"), tr("Add Variable"), QStringLiteral("buildPipelineVariableAdd"), tr("Add a variable"));
	m_variableRemove = listButton(QStringLiteral("trash"), tr("Remove Variable"), QStringLiteral("buildPipelineVariableRemove"), tr("Remove the selected variable"));

	m_preview = new QPlainTextEdit;
	m_preview->setObjectName(QStringLiteral("buildPipelinePreview"));
	m_preview->setAccessibleName(tr("Command lines with the current values"));
	m_preview->setReadOnly(true);
	m_preview->setLineWrapMode(QPlainTextEdit::WidgetWidth);
	m_preview->setMinimumHeight(90);
	m_preview->setLayoutDirection(Qt::LeftToRight);
	m_problem = new QLabel;
	m_problem->setObjectName(QStringLiteral("buildPipelineProblem"));
	m_problem->setAccessibleName(tr("Pipeline problem"));
	m_problem->setTextFormat(Qt::PlainText);
	m_problem->setWordWrap(true);

	auto* pipelineForm = new QFormLayout;
	pipelineForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
	pipelineForm->addRow(tr("&Name"), m_name);
	pipelineForm->addRow(tr("Id"), m_id);
	pipelineForm->addRow(tr("&Description"), m_description);

	auto* stageButtons = new QHBoxLayout;
	for (QPushButton* button : {m_stageAdd, m_stageRemove, m_stageUp, m_stageDown}) {
		stageButtons->addWidget(button);
	}
	stageButtons->addStretch(1);
	auto* stageForm = new QFormLayout;
	stageForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
	stageForm->addRow(tr("Stage na&me"), m_stageName);
	stageForm->addRow(tr("&Compiler"), m_stageProfile);
	stageForm->addRow(tr("&Input"), m_stageInput);
	stageForm->addRow(tr("&Arguments"), m_stageArguments);
	auto* stagesBox = new QGroupBox(tr("Stages"));
	auto* stagesLayout = new QVBoxLayout(stagesBox);
	stagesLayout->addWidget(m_stages, 1);
	stagesLayout->addLayout(stageButtons);
	stagesLayout->addLayout(stageForm);

	auto* variableButtons = new QHBoxLayout;
	variableButtons->addWidget(m_variableAdd);
	variableButtons->addWidget(m_variableRemove);
	variableButtons->addStretch(1);
	auto* variablesBox = new QGroupBox(tr("Variables"));
	auto* variablesLayout = new QVBoxLayout(variablesBox);
	variablesLayout->addWidget(m_variables, 1);
	variablesLayout->addLayout(variableButtons);

	auto* previewBox = new QGroupBox(tr("Preview"));
	auto* previewLayout = new QVBoxLayout(previewBox);
	previewLayout->addWidget(m_preview, 1);

	auto* editor = new QWidget;
	auto* editorLayout = new QVBoxLayout(editor);
	editorLayout->setContentsMargins(0, 0, 0, 0);
	editorLayout->addLayout(pipelineForm);
	editorLayout->addWidget(stagesBox, 2);
	editorLayout->addWidget(variablesBox, 1);
	editorLayout->addWidget(previewBox, 1);
	editorLayout->addWidget(m_problem);
	auto* scroll = new QScrollArea;
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	scroll->setWidget(editor);

	auto* listButtons = new QHBoxLayout;
	listButtons->addWidget(add);
	listButtons->addWidget(m_duplicate);
	listButtons->addWidget(m_remove);
	listButtons->addStretch(1);
	auto* left = new QVBoxLayout;
	left->addWidget(m_list, 1);
	left->addLayout(listButtons);
	auto* body = new QHBoxLayout;
	body->addLayout(left, 2);
	body->addWidget(scroll, 5);

	m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	m_buttons->setObjectName(QStringLiteral("buildPipelinesButtons"));
	auto* layout = new QVBoxLayout(this);
	layout->addLayout(body, 1);
	layout->addWidget(m_buttons);

	m_previewTimer = new QTimer(this);
	m_previewTimer->setSingleShot(true);
	m_previewTimer->setInterval(120);
	connect(m_previewTimer, &QTimer::timeout, this, [this]() { refreshPreview(); });

	connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) {
		if (!m_updating) {
			showPipeline(row);
		}
	});
	connect(add, &QPushButton::clicked, this, [this]() { addPipeline(); });
	connect(m_duplicate, &QPushButton::clicked, this, [this]() { duplicatePipeline(); });
	connect(m_remove, &QPushButton::clicked, this, [this]() { removePipeline(); });
	connect(m_name, &QLineEdit::textEdited, this, [this](const QString& text) {
		BuildPipelineDescriptor* pipeline = current();
		if (!pipeline) {
			return;
		}
		pipeline->displayName = text;
		const qsizetype created = m_createdIds.indexOf(pipeline->id);
		if (created >= 0) {
			QVector<BuildPipelineDescriptor> others = m_pipelines;
			others.removeAt(m_row);
			pipeline->id = projectBuildPipelineId(text, others);
			m_createdIds[created] = pipeline->id;
			m_id->setText(pipeline->id);
		}
		refreshPipelineItems();
		refreshState();
	});
	connect(m_description, &QLineEdit::textEdited, this, [this](const QString& text) {
		if (BuildPipelineDescriptor* pipeline = current()) {
			pipeline->description = text;
		}
	});
	connect(m_stages, &QListWidget::currentRowChanged, this, [this](int row) {
		if (!m_updating) {
			showStage(row);
		}
	});
	connect(m_stages, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
		BuildPipelineDescriptor* pipeline = current();
		const int row = m_stages->row(item);
		if (m_updating || !pipeline || row < 0 || row >= pipeline->stages.size()) {
			return;
		}
		pipeline->stages[row].enabledByDefault = item->checkState() == Qt::Checked;
		refreshState();
	});
	connect(m_stageAdd, &QPushButton::clicked, this, [this]() { addStage(); });
	connect(m_stageRemove, &QPushButton::clicked, this, [this]() { removeStage(); });
	connect(m_stageUp, &QPushButton::clicked, this, [this]() { moveStage(-1); });
	connect(m_stageDown, &QPushButton::clicked, this, [this]() { moveStage(1); });
	connect(m_stageName, &QLineEdit::textEdited, this, [this]() { storeStage(); });
	connect(m_stageArguments, &QLineEdit::textEdited, this, [this]() { storeStage(); });
	connect(m_stageProfile, &QComboBox::currentIndexChanged, this, [this]() {
		BuildPipelineDescriptor* pipeline = current();
		if (m_updating || !pipeline || m_stageRow < 0 || m_stageRow >= pipeline->stages.size()) {
			return;
		}
		// A stage still named after its old compiler takes the new one's name.
		BuildPipelineStage& stage = pipeline->stages[m_stageRow];
		CompilerProfileDescriptor previous;
		if (compilerProfileForId(stage.profileId, &previous) && stage.displayName == previous.displayName) {
			CompilerProfileDescriptor next;
			if (compilerProfileForId(m_stageProfile->currentData().toString(), &next)) {
				const QSignalBlocker blocker(m_stageName);
				m_stageName->setText(next.displayName);
			}
		}
		storeStage();
	});
	connect(m_stageInput, &QComboBox::currentIndexChanged, this, [this]() { storeStage(); });
	connect(m_variables, &QTableWidget::itemChanged, this, [this]() {
		if (!m_updating) {
			storeVariables();
		}
	});
	connect(m_variableAdd, &QPushButton::clicked, this, [this]() { addVariable(); });
	connect(m_variableRemove, &QPushButton::clicked, this, [this]() { removeVariable(); });
	connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

	refreshPipelineItems();
	if (!m_pipelines.isEmpty()) {
		m_list->setCurrentRow(0);
	} else {
		showPipeline(-1);
	}
	refreshState();
}

QVector<BuildPipelineDescriptor> BuildPipelinesDialog::pipelines() const
{
	QVector<BuildPipelineDescriptor> result = m_pipelines;
	for (BuildPipelineDescriptor& pipeline : result) {
		normalizeProjectBuildPipeline(&pipeline);
	}
	return result;
}

QString BuildPipelinesDialog::currentPipelineId() const
{
	return m_row >= 0 && m_row < m_pipelines.size() ? m_pipelines.at(m_row).id : QString();
}

void BuildPipelinesDialog::setPreviewRequest(const BuildPipelineRequest& request)
{
	m_previewRequest = request;
	refreshPreview();
}

void BuildPipelinesDialog::selectPipeline(const QString& id)
{
	for (int row = 0; row < m_pipelines.size(); ++row) {
		if (m_pipelines.at(row).id == id) {
			m_list->setCurrentRow(row);
			return;
		}
	}
}

BuildPipelineDescriptor* BuildPipelinesDialog::current()
{
	return m_row >= 0 && m_row < m_pipelines.size() ? &m_pipelines[m_row] : nullptr;
}

void BuildPipelinesDialog::addPipeline()
{
	// A new pipeline starts from the Build page's choice, so its first
	// version is a working build.
	const QString name = m_starter.stages.isEmpty() ? tr("My pipeline") : tr("%1 (mine)").arg(m_starter.displayName);
	BuildPipelineDescriptor pipeline = projectBuildPipelineFrom(m_starter, name, m_pipelines);
	if (pipeline.stages.isEmpty()) {
		BuildPipelineStage stage;
		stage.profileId = compilerProfileIds().value(0);
		stage.id = projectBuildPipelineStageId(stage.profileId, pipeline);
		pipeline.stages.push_back(stage);
		normalizeProjectBuildPipeline(&pipeline);
	}
	m_createdIds << pipeline.id;
	m_pipelines.push_back(pipeline);
	refreshPipelineItems();
	m_list->setCurrentRow(static_cast<int>(m_pipelines.size()) - 1);
	m_name->setFocus(Qt::OtherFocusReason);
	m_name->selectAll();
}

void BuildPipelinesDialog::duplicatePipeline()
{
	const BuildPipelineDescriptor* pipeline = current();
	if (!pipeline) {
		return;
	}
	const BuildPipelineDescriptor copy = projectBuildPipelineFrom(*pipeline, tr("%1 (copy)").arg(pipeline->displayName), m_pipelines);
	m_createdIds << copy.id;
	m_pipelines.push_back(copy);
	refreshPipelineItems();
	m_list->setCurrentRow(static_cast<int>(m_pipelines.size()) - 1);
}

void BuildPipelinesDialog::removePipeline()
{
	if (m_row < 0 || m_row >= m_pipelines.size()) {
		return;
	}
	const int row = m_row;
	m_pipelines.removeAt(row);
	m_row = -1;
	refreshPipelineItems();
	const int next = m_pipelines.isEmpty() ? -1 : std::min(row, static_cast<int>(m_pipelines.size()) - 1);
	m_list->setCurrentRow(next);
	if (next < 0) {
		showPipeline(-1);
	}
	refreshState();
}

void BuildPipelinesDialog::showPipeline(int row)
{
	m_row = row >= 0 && row < m_pipelines.size() ? row : -1;
	const BuildPipelineDescriptor* pipeline = current();
	{
		const QScopedValueRollback<bool> updating(m_updating, true);
		m_name->setText(pipeline ? pipeline->displayName : QString());
		m_id->setText(pipeline ? pipeline->id : QString());
		m_description->setText(pipeline ? pipeline->description : QString());
		m_variables->setRowCount(0);
		if (pipeline) {
			for (auto it = pipeline->variables.cbegin(); it != pipeline->variables.cend(); ++it) {
				const int index = m_variables->rowCount();
				m_variables->insertRow(index);
				m_variables->setItem(index, 0, new QTableWidgetItem(it.key()));
				m_variables->setItem(index, 1, new QTableWidgetItem(it.value()));
			}
		}
	}
	for (QWidget* field : std::initializer_list<QWidget*> {m_name, m_description, m_stages, m_stageAdd, m_variables, m_variableAdd, m_duplicate, m_remove}) {
		field->setEnabled(pipeline != nullptr);
	}
	m_stageRow = -1;
	refreshStageItems();
	if (pipeline && !pipeline->stages.isEmpty()) {
		m_stages->setCurrentRow(0);
	} else {
		showStage(-1);
	}
	refreshState();
}

void BuildPipelinesDialog::addStage()
{
	BuildPipelineDescriptor* pipeline = current();
	if (!pipeline) {
		return;
	}
	BuildPipelineStage stage;
	// The next stage usually runs another compiler of the same game on the
	// output so far: the last stage's profile is a starting point.
	stage.profileId = pipeline->stages.isEmpty() ? compilerProfileIds().value(0) : pipeline->stages.constLast().profileId;
	stage.id = projectBuildPipelineStageId(stage.profileId, *pipeline);
	stage.inputFromPrevious = !pipeline->stages.isEmpty();
	pipeline->stages.push_back(stage);
	normalizeProjectBuildPipeline(pipeline);
	refreshStageItems();
	m_stages->setCurrentRow(static_cast<int>(pipeline->stages.size()) - 1);
	m_stageProfile->setFocus(Qt::OtherFocusReason);
	refreshState();
}

void BuildPipelinesDialog::removeStage()
{
	BuildPipelineDescriptor* pipeline = current();
	if (!pipeline || m_stageRow < 0 || m_stageRow >= pipeline->stages.size()) {
		return;
	}
	const int row = m_stageRow;
	pipeline->stages.removeAt(row);
	normalizeProjectBuildPipeline(pipeline);
	m_stageRow = -1;
	refreshStageItems();
	const int next = pipeline->stages.isEmpty() ? -1 : std::min(row, static_cast<int>(pipeline->stages.size()) - 1);
	m_stages->setCurrentRow(next);
	if (next < 0) {
		showStage(-1);
	}
	refreshState();
}

void BuildPipelinesDialog::moveStage(int offset)
{
	BuildPipelineDescriptor* pipeline = current();
	const int target = m_stageRow + offset;
	if (!pipeline || m_stageRow < 0 || target < 0 || target >= pipeline->stages.size()) {
		return;
	}
	pipeline->stages.swapItemsAt(m_stageRow, target);
	normalizeProjectBuildPipeline(pipeline);
	refreshStageItems();
	m_stages->setCurrentRow(target);
	refreshState();
}

void BuildPipelinesDialog::showStage(int row)
{
	const BuildPipelineDescriptor* pipeline = current();
	m_stageRow = pipeline && row >= 0 && row < pipeline->stages.size() ? row : -1;
	const BuildPipelineStage* stage = m_stageRow >= 0 ? &pipeline->stages.at(m_stageRow) : nullptr;
	{
		const QScopedValueRollback<bool> updating(m_updating, true);
		m_stageName->setText(stage ? stage->displayName : QString());
		const int profile = stage ? m_stageProfile->findData(stage->profileId) : -1;
		m_stageProfile->setCurrentIndex(profile);
		m_stageInput->setCurrentIndex(stage && stage->inputFromPrevious ? 1 : 0);
		m_stageArguments->setText(stage ? stage->argumentLine : QString());
	}
	for (QWidget* field : std::initializer_list<QWidget*> {m_stageName, m_stageProfile, m_stageArguments, m_stageRemove}) {
		field->setEnabled(stage != nullptr);
	}
	// The first stage always reads the map.
	m_stageInput->setEnabled(stage != nullptr && m_stageRow > 0);
	m_stageUp->setEnabled(stage != nullptr && m_stageRow > 0);
	m_stageDown->setEnabled(stage != nullptr && pipeline && m_stageRow + 1 < pipeline->stages.size());
}

void BuildPipelinesDialog::storeStage()
{
	BuildPipelineDescriptor* pipeline = current();
	if (m_updating || !pipeline || m_stageRow < 0 || m_stageRow >= pipeline->stages.size()) {
		return;
	}
	BuildPipelineStage& stage = pipeline->stages[m_stageRow];
	stage.displayName = m_stageName->text();
	stage.profileId = m_stageProfile->currentData().toString();
	stage.inputFromPrevious = m_stageRow > 0 && m_stageInput->currentData().toString() == QLatin1String("previous");
	stage.argumentLine = m_stageArguments->text();
	normalizeProjectBuildPipeline(pipeline);
	if (QListWidgetItem* item = m_stages->item(m_stageRow)) {
		const QSignalBlocker blocker(m_stages);
		item->setText(stage.displayName.trimmed().isEmpty() ? stage.id : stage.displayName);
	}
	refreshState();
}

void BuildPipelinesDialog::addVariable()
{
	BuildPipelineDescriptor* pipeline = current();
	if (!pipeline) {
		return;
	}
	QString name = QStringLiteral("NAME");
	for (int copy = 2; pipeline->variables.contains(name); ++copy) {
		name = QStringLiteral("NAME_%1").arg(copy);
	}
	{
		const QScopedValueRollback<bool> updating(m_updating, true);
		const int row = m_variables->rowCount();
		m_variables->insertRow(row);
		m_variables->setItem(row, 0, new QTableWidgetItem(name));
		m_variables->setItem(row, 1, new QTableWidgetItem(QString()));
		m_variables->setCurrentCell(row, 0);
	}
	storeVariables();
	m_variables->setFocus(Qt::OtherFocusReason);
	m_variables->editItem(m_variables->item(m_variables->rowCount() - 1, 0));
}

void BuildPipelinesDialog::removeVariable()
{
	const int row = m_variables->currentRow();
	if (!current() || row < 0) {
		return;
	}
	m_variables->removeRow(row);
	storeVariables();
}

void BuildPipelinesDialog::storeVariables()
{
	BuildPipelineDescriptor* pipeline = current();
	if (!pipeline) {
		return;
	}
	pipeline->variables.clear();
	for (int row = 0; row < m_variables->rowCount(); ++row) {
		const QString name = m_variables->item(row, 0) ? m_variables->item(row, 0)->text().trimmed() : QString();
		if (!name.isEmpty()) {
			pipeline->variables.insert(name, m_variables->item(row, 1) ? m_variables->item(row, 1)->text() : QString());
		}
	}
	refreshState();
}

void BuildPipelinesDialog::refreshPipelineItems()
{
	const QScopedValueRollback<bool> updating(m_updating, true);
	const int row = m_row;
	m_list->clear();
	for (const BuildPipelineDescriptor& pipeline : std::as_const(m_pipelines)) {
		m_list->addItem(pipeline.displayName.trimmed().isEmpty() ? tr("(no name)") : pipeline.displayName);
	}
	if (row >= 0 && row < m_list->count()) {
		m_list->setCurrentRow(row);
	}
}

void BuildPipelinesDialog::refreshStageItems()
{
	const QScopedValueRollback<bool> updating(m_updating, true);
	const int row = m_stageRow;
	m_stages->clear();
	if (const BuildPipelineDescriptor* pipeline = current()) {
		for (const BuildPipelineStage& stage : pipeline->stages) {
			auto* item = new QListWidgetItem(stage.displayName.trimmed().isEmpty() ? stage.id : stage.displayName);
			item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
			item->setCheckState(stage.enabledByDefault ? Qt::Checked : Qt::Unchecked);
			m_stages->addItem(item);
		}
	}
	if (row >= 0 && row < m_stages->count()) {
		m_stages->setCurrentRow(row);
	}
}

QStringList BuildPipelinesDialog::problemsFor(int row) const
{
	if (row < 0 || row >= m_pipelines.size()) {
		return {};
	}
	QStringList problems = projectBuildPipelineProblems(m_pipelines.at(row), m_pipelines);
	if (row == m_row) {
		// The table can hold a name twice; the stored variables cannot.
		QStringList names;
		for (int index = 0; index < m_variables->rowCount(); ++index) {
			const QString name = m_variables->item(index, 0) ? m_variables->item(index, 0)->text().trimmed() : QString();
			if (!name.isEmpty() && names.contains(name)) {
				problems << tr("Two variables are called %1.").arg(name);
			}
			names << name;
		}
	}
	return problems;
}

void BuildPipelinesDialog::refreshState()
{
	// The first pipeline with a problem keeps OK disabled and is named.
	QString blocking;
	for (int row = 0; row < m_pipelines.size() && blocking.isEmpty(); ++row) {
		const QStringList problems = problemsFor(row);
		if (!problems.isEmpty()) {
			const QString name = m_pipelines.at(row).displayName.trimmed();
			blocking = name.isEmpty() ? problems.constFirst() : tr("%1: %2").arg(name, problems.constFirst());
		}
	}
	const QStringList currentProblems = problemsFor(m_row);
	const QString shown = !currentProblems.isEmpty() ? currentProblems.join(QLatin1Char('\n')) : blocking;
	m_problem->setText(shown.isEmpty() ? QString() : tr("Problem: %1").arg(shown));
	m_problem->setVisible(!shown.isEmpty());
	if (QPushButton* ok = m_buttons ? m_buttons->button(QDialogButtonBox::Ok) : nullptr) {
		ok->setEnabled(blocking.isEmpty());
		ok->setToolTip(blocking);
	}
	if (m_previewTimer) {
		m_previewTimer->start();
	}
}

void BuildPipelinesDialog::refreshPreview()
{
	if (m_row < 0 || m_row >= m_pipelines.size()) {
		m_preview->setPlainText(m_pipelines.isEmpty() ? tr("No pipelines of the project's own yet. Choose Add to make one.") : QString());
		return;
	}
	BuildPipelineRequest request = m_previewRequest;
	request.pipelineId = m_pipelines.at(m_row).id;
	request.projectPipelines = pipelines();
	request.dryRun = true;
	const BuildPipelineResult plan = planBuildPipeline(request);
	QStringList lines;
	if (request.inputPath.trimmed().isEmpty()) {
		lines << tr("No input map: choose one on the Build page to see the full command lines.");
	}
	for (const BuildPipelineStageResult& stage : plan.stages) {
		const QString name = stage.stage.displayName.trimmed().isEmpty() ? stage.stage.id : stage.stage.displayName;
		lines << (stage.skipped ? tr("%1: skipped. %2").arg(name, stage.skipReason) : tr("%1: %2").arg(name, stage.plan.commandLine));
	}
	for (const QString& warning : plan.warnings) {
		lines << tr("Warning: %1").arg(warning);
	}
	for (const QString& error : plan.errors) {
		lines << tr("Error: %1").arg(error);
	}
	m_preview->setPlainText(lines.join(QLatin1Char('\n')));
}

} // namespace vibestudio
