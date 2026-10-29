// The user's own launch profiles. Workflow reference: TrenchBroom
// GameEngineDialog and GameEngineProfileEditor at 4c756be1
// (GPL-3.0-or-later): a list of named engine profiles, each with its own
// parameter line. Original Qt implementation; see docs/CREDITS.md.
#include "app/game_launch_profiles_dialog.h"

#include "app/studio_icons.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <algorithm>

namespace vibestudio {

GameLaunchProfilesDialog::GameLaunchProfilesDialog(const QVector<GameLaunchProfile>& profiles, const QString& starterArguments, QWidget* parent)
	: QDialog(parent)
	, m_profiles(profiles)
	, m_starter(starterArguments.trimmed())
{
	setObjectName(QStringLiteral("gameLaunchProfilesDialog"));
	setWindowTitle(tr("Launch Profiles"));
	setAccessibleName(windowTitle());
	setAccessibleDescription(tr("Your own launch profiles: each has a name and the engine arguments a launch passes."));
	setMinimumWidth(560);

	m_list = new QListWidget;
	m_list->setObjectName(QStringLiteral("launchProfilesList"));
	m_list->setAccessibleName(tr("Your launch profiles"));
	m_list->setAccessibleDescription(tr("Select a profile to edit its name and arguments."));
	m_list->setMinimumWidth(180);

	auto* add = new QPushButton(studioIcon(QStringLiteral("plus")), tr("Add"));
	add->setObjectName(QStringLiteral("launchProfilesAdd"));
	add->setAccessibleName(tr("Add a launch profile"));
	add->setToolTip(tr("Add a profile that starts with the installation's usual arguments."));
	m_remove = new QPushButton(studioIcon(QStringLiteral("trash")), tr("Remove"));
	m_remove->setObjectName(QStringLiteral("launchProfilesRemove"));
	m_remove->setAccessibleName(tr("Remove the selected launch profile"));

	m_name = new QLineEdit;
	m_name->setObjectName(QStringLiteral("launchProfileName"));
	m_name->setAccessibleName(tr("Profile name"));
	m_engine = new QComboBox;
	m_engine->setObjectName(QStringLiteral("launchProfileEngine"));
	m_engine->setAccessibleName(tr("Engine"));
	m_engine->setToolTip(tr("The engine family the profile is for; Any game offers it for every installation."));
	m_engine->addItem(tr("Any game"), QString());
	m_engine->addItem(tr("Doom (idTech1)"), QStringLiteral("idTech1"));
	m_engine->addItem(tr("Quake and Quake II (idTech2)"), QStringLiteral("idTech2"));
	m_engine->addItem(tr("Quake III (idTech3)"), QStringLiteral("idTech3"));
	m_arguments = new QLineEdit;
	m_arguments->setObjectName(QStringLiteral("launchProfileArguments"));
	m_arguments->setAccessibleName(tr("Engine arguments"));
	m_arguments->setAccessibleDescription(tr("The arguments the launch passes. {map} is the map, {mod} the game folder, {basedir} the installation folder, {bsp} the built map, {iwad} a Doom IWAD and {warp} a Doom map's -warp numbers. Quotes keep spaces."));
	m_arguments->setToolTip(m_arguments->accessibleDescription());
	m_arguments->setPlaceholderText(QStringLiteral("+set fs_game {mod} +devmap {map}"));
	// Arguments are typed left to right in every language.
	m_arguments->setLayoutDirection(Qt::LeftToRight);
	m_preview = new QLabel;
	m_preview->setObjectName(QStringLiteral("launchProfilePreview"));
	m_preview->setWordWrap(true);
	m_preview->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
	m_preview->setAccessibleName(tr("Arguments with the current values"));
	// Names and arguments the user typed are shown as typed, never as markup.
	m_preview->setTextFormat(Qt::PlainText);
	m_problem = new QLabel;
	m_problem->setObjectName(QStringLiteral("launchProfileProblem"));
	m_problem->setTextFormat(Qt::PlainText);
	m_problem->setWordWrap(true);
	m_problem->setAccessibleName(tr("Profile problem"));

	auto* form = new QFormLayout;
	form->setRowWrapPolicy(QFormLayout::WrapLongRows);
	form->addRow(tr("&Name"), m_name);
	form->addRow(tr("&Engine"), m_engine);
	form->addRow(tr("&Arguments"), m_arguments);
	form->addRow(tr("Preview"), m_preview);
	form->addRow(m_problem);

	auto* listButtons = new QHBoxLayout;
	listButtons->addWidget(add);
	listButtons->addWidget(m_remove);
	listButtons->addStretch(1);
	auto* left = new QVBoxLayout;
	left->addWidget(m_list, 1);
	left->addLayout(listButtons);
	auto* body = new QHBoxLayout;
	body->addLayout(left, 2);
	body->addLayout(form, 3);

	m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	m_buttons->setObjectName(QStringLiteral("launchProfilesButtons"));
	auto* layout = new QVBoxLayout(this);
	layout->addLayout(body, 1);
	layout->addWidget(m_buttons);

	for (const GameLaunchProfile& profile : std::as_const(m_profiles)) {
		m_list->addItem(profile.displayName);
	}
	connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) {
		storeCurrent();
		showProfile(row);
	});
	connect(add, &QPushButton::clicked, this, [this]() { addProfile(); });
	connect(m_remove, &QPushButton::clicked, this, [this]() { removeCurrentProfile(); });
	for (QLineEdit* field : {m_name, m_arguments}) {
		connect(field, &QLineEdit::textEdited, this, [this]() {
			storeCurrent();
			refreshState();
		});
	}
	connect(m_engine, &QComboBox::currentIndexChanged, this, [this]() {
		storeCurrent();
		refreshState();
	});
	connect(m_buttons, &QDialogButtonBox::accepted, this, [this]() {
		storeCurrent();
		accept();
	});
	connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	if (!m_profiles.isEmpty()) {
		m_list->setCurrentRow(0);
	} else {
		showProfile(-1);
	}
	refreshState();
}

void GameLaunchProfilesDialog::setPreviewTokens(const QMap<QString, QString>& tokens)
{
	m_tokens = tokens;
	refreshState();
}

QVector<GameLaunchProfile> GameLaunchProfilesDialog::profiles() const
{
	return m_profiles;
}

QString GameLaunchProfilesDialog::currentProfileId() const
{
	return m_row >= 0 && m_row < m_profiles.size() ? m_profiles.at(m_row).id : QString();
}

void GameLaunchProfilesDialog::addProfile()
{
	storeCurrent();
	GameLaunchProfile profile;
	profile.userDefined = true;
	profile.displayName = tr("My launch profile");
	profile.id = userGameLaunchProfileId(profile.displayName, m_profiles);
	profile.argumentLine = m_starter;
	profile.requiresMap = m_starter.contains(QStringLiteral("{map}")) || m_starter.contains(QStringLiteral("{warp}"));
	m_profiles.push_back(profile);
	m_list->addItem(profile.displayName);
	m_list->setCurrentRow(static_cast<int>(m_profiles.size()) - 1);
	m_name->setFocus(Qt::OtherFocusReason);
	m_name->selectAll();
}

void GameLaunchProfilesDialog::removeCurrentProfile()
{
	const int row = m_row;
	if (row < 0 || row >= m_profiles.size()) {
		return;
	}
	m_row = -1;
	m_profiles.removeAt(row);
	{
		const QSignalBlocker blocker(m_list);
		delete m_list->takeItem(row);
	}
	const int next = std::min(row, static_cast<int>(m_profiles.size()) - 1);
	{
		const QSignalBlocker blocker(m_list);
		m_list->setCurrentRow(next);
	}
	showProfile(next);
	m_list->setFocus(Qt::OtherFocusReason);
}

void GameLaunchProfilesDialog::showProfile(int row)
{
	m_row = row >= 0 && row < m_profiles.size() ? row : -1;
	const bool has = m_row >= 0;
	{
		const QScopedValueRollback<bool> filling(m_filling, true);
		const QSignalBlocker nameBlocker(m_name);
		const QSignalBlocker engineBlocker(m_engine);
		const QSignalBlocker argumentsBlocker(m_arguments);
		const GameLaunchProfile profile = has ? m_profiles.at(m_row) : GameLaunchProfile {};
		m_name->setText(profile.displayName);
		const int engine = m_engine->findData(profile.engineFamily);
		m_engine->setCurrentIndex(engine < 0 ? 0 : engine);
		m_arguments->setText(profile.argumentLine);
	}
	for (QWidget* field : std::initializer_list<QWidget*> {m_name, m_engine, m_arguments, m_remove}) {
		field->setEnabled(has);
	}
	refreshState();
}

void GameLaunchProfilesDialog::storeCurrent()
{
	if (m_filling || m_row < 0 || m_row >= m_profiles.size()) {
		return;
	}
	GameLaunchProfile& profile = m_profiles[m_row];
	profile.displayName = m_name->text().trimmed();
	profile.engineFamily = m_engine->currentData().toString();
	profile.argumentLine = m_arguments->text().trimmed();
	profile.requiresMap = profile.argumentLine.contains(QStringLiteral("{map}")) || profile.argumentLine.contains(QStringLiteral("{warp}"));
	profile.userDefined = true;
	if (QListWidgetItem* item = m_list->item(m_row)) {
		item->setText(profile.displayName.isEmpty() ? tr("(no name)") : profile.displayName);
	}
}

QString GameLaunchProfilesDialog::problemFor(const GameLaunchProfile& profile) const
{
	const QString problem = userGameLaunchProfileProblem(profile);
	if (!problem.isEmpty()) {
		return problem;
	}
	for (const GameLaunchProfile& other : m_profiles) {
		if (&other != &profile && other.displayName.compare(profile.displayName, Qt::CaseInsensitive) == 0) {
			return tr("Another profile is called %1; give each a name of its own.").arg(profile.displayName);
		}
	}
	return {};
}

void GameLaunchProfilesDialog::refreshState()
{
	// The first profile with a problem keeps OK disabled and is named.
	QString blocking;
	for (const GameLaunchProfile& profile : std::as_const(m_profiles)) {
		const QString problem = problemFor(profile);
		if (!problem.isEmpty() && blocking.isEmpty()) {
			blocking = profile.displayName.isEmpty() ? problem : tr("%1: %2").arg(profile.displayName, problem);
		}
	}
	const QString current = m_row >= 0 ? problemFor(m_profiles.at(m_row)) : QString();
	const QString shown = !current.isEmpty() ? current : blocking;
	m_problem->setText(shown.isEmpty() ? QString() : tr("Problem: %1").arg(shown));
	m_problem->setVisible(!shown.isEmpty());
	if (QPushButton* ok = m_buttons ? m_buttons->button(QDialogButtonBox::Ok) : nullptr) {
		ok->setEnabled(blocking.isEmpty());
		ok->setToolTip(blocking);
	}
	if (m_row < 0) {
		m_preview->setText(m_profiles.isEmpty() ? tr("No profiles of your own yet. Choose Add to make one.") : QString());
		return;
	}
	// The arguments as this launch would pass them: the plan's own resolution
	// (so {warp} may be two words), each word with a space quoted.
	QStringList dropped;
	QStringList words;
	for (const QString& word : resolveGameLaunchArgumentLine(m_profiles.at(m_row).argumentLine, m_tokens, &dropped)) {
		words << (word.contains(QLatin1Char(' ')) ? QStringLiteral("\"%1\"").arg(word) : word);
	}
	QString text = words.isEmpty() ? tr("No arguments") : words.join(QLatin1Char(' '));
	if (!dropped.isEmpty()) {
		text += QLatin1Char('\n') + tr("Left out while a value is empty: %1").arg(dropped.join(QStringLiteral(", ")));
	}
	m_preview->setText(text);
}

} // namespace vibestudio
