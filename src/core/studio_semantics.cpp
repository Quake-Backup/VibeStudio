#include "core/studio_semantics.h"

#include "core/level_entity_create.h"
#include "core/level_filter_tags.h"
#include "core/level_smart_tags.h"

#include <QCoreApplication>
#include <QHash>
#include <QSet>

#include <algorithm>
#include <utility>

namespace slopstudio {

namespace {

// Shell commands are registered with camelCase ids ("shell.commandPalette")
// while this registry documents them in kebab-case ("shell.command-palette").
// Both spellings name the same command, so a lower-to-upper transition becomes
// a hyphen before the comparison.
QString normalizedId(QString value)
{
	value = value.trimmed();
	QString normalized;
	normalized.reserve(value.size() + 8);
	for (int index = 0; index < value.size(); ++index) {
		const QChar ch = value.at(index);
		if (ch.isUpper() && index > 0 && (value.at(index - 1).isLower() || value.at(index - 1).isDigit())) {
			normalized += QLatin1Char('-');
		}
		normalized += ch.toLower();
	}
	normalized.replace('_', '-');
	normalized.replace(' ', '-');
	return normalized;
}

QString normalizedSequence(const QString& sequence)
{
	return sequence.trimmed().toLower().remove(' ');
}

ShortcutDescriptor shortcut(
	const QString& id,
	const QString& commandId,
	const QString& label,
	const QString& context,
	const QString& defaultSequence,
	const QString& description,
	const QStringList& alternateSequences = {},
	bool userRemappable = true)
{
	return {id, commandId, label, context, defaultSequence, alternateSequences, description, userRemappable};
}

// A shortcut that fires only while focus is inside its context's surface.
ShortcutDescriptor surfaceShortcut(
	const QString& id,
	const QString& commandId,
	const QString& label,
	const QString& context,
	const QString& defaultSequence,
	const QString& description,
	const QStringList& alternateSequences = {},
	bool userRemappable = true)
{
	ShortcutDescriptor descriptor = shortcut(id, commandId, label, context, defaultSequence, description, alternateSequences, userRemappable);
	descriptor.surfaceScoped = true;
	return descriptor;
}

// Palette entries never spell their own shortcut: it is looked up from the
// shortcut registry so the two can never drift apart.
const QHash<QString, QString>& defaultSequencesByCommandId()
{
	static const QHash<QString, QString> sequences = [] {
		QHash<QString, QString> map;
		for (const ShortcutDescriptor& descriptor : shortcutDescriptors()) {
			map.insert(normalizedId(descriptor.commandId), descriptor.defaultSequence);
		}
		return map;
	}();
	return sequences;
}

CommandPaletteEntry paletteEntry(
	const QString& id,
	const QString& commandId,
	const QString& label,
	const QString& category,
	const QString& summary,
	bool requiresProject = false,
	bool destructive = false,
	bool stagedOrDryRun = false)
{
	return {
		id,
		commandId,
		label,
		category,
		summary,
		defaultSequencesByCommandId().value(normalizedId(commandId)),
		requiresProject,
		destructive,
		stagedOrDryRun,
	};
}

} // namespace

QVector<StatusChipDescriptor> statusChipDescriptors()
{
	return {
		{
			QStringLiteral("project-ready"),
			QStringLiteral("project"),
			QCoreApplication::translate("SlopStudioSemantics", "Project Ready"),
			QCoreApplication::translate("SlopStudioSemantics", "A project manifest is loaded or can be initialized from the selected folder."),
			OperationState::Completed,
			QStringLiteral("folder"),
			QStringLiteral("completed"),
			QCoreApplication::translate("SlopStudioSemantics", "checkmark plus Ready text"),
			QCoreApplication::translate("SlopStudioSemantics", "Open the workspace dashboard or validate the project."),
		},
		{
			QStringLiteral("project-needs-attention"),
			QStringLiteral("project"),
			QCoreApplication::translate("SlopStudioSemantics", "Project Needs Attention"),
			QCoreApplication::translate("SlopStudioSemantics", "The project can be inspected, but validation found warnings or missing optional context."),
			OperationState::Warning,
			QStringLiteral("folder"),
			QStringLiteral("warning"),
			QCoreApplication::translate("SlopStudioSemantics", "warning triangle plus Needs Attention text"),
			QCoreApplication::translate("SlopStudioSemantics", "Open project validation details."),
		},
		{
			QStringLiteral("package-loaded"),
			QStringLiteral("package"),
			QCoreApplication::translate("SlopStudioSemantics", "Package Loaded"),
			QCoreApplication::translate("SlopStudioSemantics", "The selected folder, PAK, WAD, ZIP, or PK3 is open and entries are indexed."),
			OperationState::Completed,
			QStringLiteral("archive"),
			QStringLiteral("completed"),
			QCoreApplication::translate("SlopStudioSemantics", "archive icon plus Loaded text"),
			QCoreApplication::translate("SlopStudioSemantics", "Browse entries or open the package detail drawer."),
		},
		{
			QStringLiteral("package-staged"),
			QStringLiteral("package"),
			QCoreApplication::translate("SlopStudioSemantics", "Package Staged"),
			QCoreApplication::translate("SlopStudioSemantics", "Package edits are staged for a non-destructive save-as operation."),
			OperationState::Warning,
			QStringLiteral("archive"),
			QStringLiteral("staged"),
			QCoreApplication::translate("SlopStudioSemantics", "staged badge plus Save-As Required text"),
			QCoreApplication::translate("SlopStudioSemantics", "Review staged changes and choose a save-as path."),
		},
		{
			QStringLiteral("package-read-only"),
			QStringLiteral("package"),
			QCoreApplication::translate("SlopStudioSemantics", "Package Read-Only"),
			QCoreApplication::translate("SlopStudioSemantics", "The package is mounted for inspection and extraction, but no in-place mutation will be performed."),
			OperationState::Completed,
			QStringLiteral("lock"),
			QStringLiteral("read-only"),
			QCoreApplication::translate("SlopStudioSemantics", "lock icon plus Read-Only text"),
			QCoreApplication::translate("SlopStudioSemantics", "Use staged save-as if changes are needed."),
		},
		{
			QStringLiteral("compiler-ready"),
			QStringLiteral("compiler"),
			QCoreApplication::translate("SlopStudioSemantics", "Compiler Ready"),
			QCoreApplication::translate("SlopStudioSemantics", "A compiler profile has enough executable and input context to produce a command plan."),
			OperationState::Completed,
			QStringLiteral("terminal"),
			QStringLiteral("completed"),
			QCoreApplication::translate("SlopStudioSemantics", "terminal icon plus Ready text"),
			QCoreApplication::translate("SlopStudioSemantics", "Plan or run the selected compiler profile."),
		},
		{
			QStringLiteral("compiler-running"),
			QStringLiteral("compiler"),
			QCoreApplication::translate("SlopStudioSemantics", "Compiler Running"),
			QCoreApplication::translate("SlopStudioSemantics", "A compiler process is active and task-state/progress logs are available."),
			OperationState::Running,
			QStringLiteral("terminal"),
			QStringLiteral("running"),
			QCoreApplication::translate("SlopStudioSemantics", "moving progress indicator plus Running text"),
			QCoreApplication::translate("SlopStudioSemantics", "Watch task logs or cancel when the task allows it."),
		},
		{
			QStringLiteral("compiler-blocked"),
			QStringLiteral("compiler"),
			QCoreApplication::translate("SlopStudioSemantics", "Compiler Blocked"),
			QCoreApplication::translate("SlopStudioSemantics", "The compiler profile is missing an executable, input, working directory, or required output context."),
			OperationState::Failed,
			QStringLiteral("terminal"),
			QStringLiteral("failed"),
			QCoreApplication::translate("SlopStudioSemantics", "stop symbol plus Blocked text"),
			QCoreApplication::translate("SlopStudioSemantics", "Open compiler registry details and configure the missing path."),
		},
		{
			QStringLiteral("install-confirmable"),
			QStringLiteral("install"),
			QCoreApplication::translate("SlopStudioSemantics", "Install Confirmable"),
			QCoreApplication::translate("SlopStudioSemantics", "A detected game installation candidate is visible but will not be saved until the user confirms it."),
			OperationState::Warning,
			QStringLiteral("drive"),
			QStringLiteral("warning"),
			QCoreApplication::translate("SlopStudioSemantics", "drive icon plus Confirm text"),
			QCoreApplication::translate("SlopStudioSemantics", "Review the detected path and import or skip it."),
		},
		{
			QStringLiteral("ai-free"),
			QStringLiteral("ai"),
			QCoreApplication::translate("SlopStudioSemantics", "AI-Free"),
			QCoreApplication::translate("SlopStudioSemantics", "Cloud and agentic AI workflows are disabled while deterministic local workflows remain available."),
			OperationState::Completed,
			QStringLiteral("shield"),
			QStringLiteral("local"),
			QCoreApplication::translate("SlopStudioSemantics", "shield icon plus AI-Free text"),
			QCoreApplication::translate("SlopStudioSemantics", "Continue with local package, project, compiler, and CLI workflows."),
		},
		{
			QStringLiteral("ai-cloud-optional"),
			QStringLiteral("ai"),
			QCoreApplication::translate("SlopStudioSemantics", "Cloud AI Optional"),
			QCoreApplication::translate("SlopStudioSemantics", "Cloud connector settings exist, but core workflows remain available without provider credentials."),
			OperationState::Idle,
			QStringLiteral("cloud"),
			QStringLiteral("cloud"),
			QCoreApplication::translate("SlopStudioSemantics", "cloud icon plus Optional text"),
			QCoreApplication::translate("SlopStudioSemantics", "Configure a connector only after reviewing consent and redaction settings."),
		},
		{
			QStringLiteral("ai-review-required"),
			QStringLiteral("ai"),
			QCoreApplication::translate("SlopStudioSemantics", "AI Review Required"),
			QCoreApplication::translate("SlopStudioSemantics", "An AI-generated proposal exists but cannot mutate project files until reviewed and approved."),
			OperationState::Warning,
			QStringLiteral("sparkles"),
			QStringLiteral("staged"),
			QCoreApplication::translate("SlopStudioSemantics", "review badge plus Approval Required text"),
			QCoreApplication::translate("SlopStudioSemantics", "Open the proposal review surface."),
		},
		{
			QStringLiteral("validation-failed"),
			QStringLiteral("validation"),
			QCoreApplication::translate("SlopStudioSemantics", "Validation Failed"),
			QCoreApplication::translate("SlopStudioSemantics", "A validation pass found blocking issues that should be resolved before release or destructive operations."),
			OperationState::Failed,
			QStringLiteral("badge"),
			QStringLiteral("failed"),
			QCoreApplication::translate("SlopStudioSemantics", "X badge plus Failed text"),
			QCoreApplication::translate("SlopStudioSemantics", "Open diagnostics and resolve blocking findings."),
		},
		{
			QStringLiteral("task-paused"),
			QStringLiteral("activity"),
			QCoreApplication::translate("SlopStudioSemantics", "Task Paused"),
			QCoreApplication::translate("SlopStudioSemantics", "A task is waiting for user input, a missing path, or an external tool before it can continue."),
			OperationState::Queued,
			QStringLiteral("pause"),
			QStringLiteral("queued"),
			QCoreApplication::translate("SlopStudioSemantics", "pause icon plus Paused text"),
			QCoreApplication::translate("SlopStudioSemantics", "Open the task detail drawer and resolve the blocker."),
		},
		{
			QStringLiteral("task-cancelled"),
			QStringLiteral("activity"),
			QCoreApplication::translate("SlopStudioSemantics", "Task Cancelled"),
			QCoreApplication::translate("SlopStudioSemantics", "A user-cancelled task has stopped and cleanup/results are visible in the activity details."),
			OperationState::Cancelled,
			QStringLiteral("slash"),
			QStringLiteral("cancelled"),
			QCoreApplication::translate("SlopStudioSemantics", "slash icon plus Cancelled text"),
			QCoreApplication::translate("SlopStudioSemantics", "Review cleanup state before starting the task again."),
		},
	};
}

QStringList statusChipDomains()
{
	QStringList domains;
	for (const StatusChipDescriptor& descriptor : statusChipDescriptors()) {
		domains.push_back(descriptor.domain);
	}
	domains.removeDuplicates();
	domains.sort(Qt::CaseInsensitive);
	return domains;
}

bool statusChipForId(const QString& id, StatusChipDescriptor* out)
{
	const QString requested = normalizedId(id);
	for (const StatusChipDescriptor& descriptor : statusChipDescriptors()) {
		if (descriptor.id == requested) {
			if (out) {
				*out = descriptor;
			}
			return true;
		}
	}
	return false;
}

QStringList statusChipAccentColorTokens()
{
	return {
		QStringLiteral("staged"),
		QStringLiteral("read-only"),
		QStringLiteral("local"),
		QStringLiteral("cloud"),
	};
}

QStringList statusChipColorTokens()
{
	QStringList tokens = operationStateIds();
	tokens.append(statusChipAccentColorTokens());
	return tokens;
}

QStringList statusChipIconNames()
{
	return {
		QStringLiteral("archive"),
		QStringLiteral("badge"),
		QStringLiteral("cloud"),
		QStringLiteral("drive"),
		QStringLiteral("folder"),
		QStringLiteral("lock"),
		QStringLiteral("pause"),
		QStringLiteral("shield"),
		QStringLiteral("slash"),
		QStringLiteral("sparkles"),
		QStringLiteral("terminal"),
	};
}

bool statusChipTokensAreValid(QStringList* problems)
{
	const QStringList accents = statusChipAccentColorTokens();
	const QStringList icons = statusChipIconNames();
	QStringList found;
	for (const StatusChipDescriptor& descriptor : statusChipDescriptors()) {
		if (!icons.contains(descriptor.iconName)) {
			found.push_back(QStringLiteral("%1: icon %2").arg(descriptor.id, descriptor.iconName));
		}
		if (accents.contains(descriptor.colorToken)) {
			continue;
		}
		if (descriptor.colorToken != operationStateId(descriptor.state)) {
			found.push_back(QStringLiteral("%1: color %2").arg(descriptor.id, descriptor.colorToken));
		}
	}
	if (problems) {
		*problems = found;
	}
	return found.isEmpty();
}

QVector<ShortcutDescriptor> shortcutDescriptors()
{
	return {
		// Application shell.
		shortcut(
			QStringLiteral("command-palette"),
			QStringLiteral("shell.command-palette"),
			QCoreApplication::translate("SlopStudioSemantics", "Command Palette"),
			QStringLiteral("global"),
			QStringLiteral("Ctrl+Shift+P"),
			QCoreApplication::translate("SlopStudioSemantics", "Open the searchable command palette shell."),
			{QStringLiteral("Ctrl+K")}),
		shortcut(
			QStringLiteral("go-to-file"),
			QStringLiteral("shell.go-to-file"),
			QCoreApplication::translate("SlopStudioSemantics", "Go to File"),
			QStringLiteral("global"),
			QStringLiteral("Ctrl+P"),
			QCoreApplication::translate("SlopStudioSemantics", "Find a project file or an entry of the open package by name and open it where it belongs.")),
		shortcut(
			QStringLiteral("focus-workspace-search"),
			QStringLiteral("shell.focus-search"),
			QCoreApplication::translate("SlopStudioSemantics", "Find"),
			QStringLiteral("global"),
			QStringLiteral("Ctrl+F"),
			QCoreApplication::translate("SlopStudioSemantics", "Move focus to the search or filter field of the current work surface without changing the active mode.")),
		shortcut(
			QStringLiteral("preferences"),
			QStringLiteral("app.preferences"),
			QCoreApplication::translate("SlopStudioSemantics", "Preferences"),
			QStringLiteral("global"),
			QStringLiteral("Ctrl+,"),
			QCoreApplication::translate("SlopStudioSemantics", "Open language, accessibility, theme, density, editor profile, and automation preferences.")),
		shortcut(
			QStringLiteral("read-aloud"),
			QStringLiteral("accessibility.read-aloud"),
			QCoreApplication::translate("SlopStudioSemantics", "Read Aloud"),
			QStringLiteral("global"),
			QStringLiteral("Ctrl+Shift+U"),
			QCoreApplication::translate("SlopStudioSemantics", "Read the selected text, the focused item, or the latest status message aloud with this computer's voice; press again to stop.")),
		shortcut(
			QStringLiteral("about"),
			QStringLiteral("app.about"),
			QCoreApplication::translate("SlopStudioSemantics", "About SlopStudio"),
			QStringLiteral("global"),
			QStringLiteral("Shift+F1"),
			QCoreApplication::translate("SlopStudioSemantics", "Show version, platform, module, and credits information.")),
		shortcut(
			QStringLiteral("quit"),
			QStringLiteral("app.quit"),
			QCoreApplication::translate("SlopStudioSemantics", "Quit"),
			QStringLiteral("global"),
			QStringLiteral("Ctrl+Q"),
			QCoreApplication::translate("SlopStudioSemantics", "Close the studio shell after offering to review unsaved staged work.")),

		// Projects.
		shortcut(
			QStringLiteral("open-project"),
			QStringLiteral("project.open"),
			QCoreApplication::translate("SlopStudioSemantics", "Open Project"),
			QStringLiteral("project"),
			QStringLiteral("Ctrl+O"),
			QCoreApplication::translate("SlopStudioSemantics", "Open or remember a project folder without changing project files.")),
		shortcut(
			QStringLiteral("initialize-manifest"),
			QStringLiteral("project.initialize-manifest"),
			QCoreApplication::translate("SlopStudioSemantics", "Initialize Project Manifest"),
			QStringLiteral("project"),
			QStringLiteral("Ctrl+Shift+I"),
			QCoreApplication::translate("SlopStudioSemantics", "Write a new studio manifest into the selected project folder.")),
		shortcut(
			QStringLiteral("copy-manifest"),
			QStringLiteral("project.copy-manifest"),
			QCoreApplication::translate("SlopStudioSemantics", "Copy Project Manifest"),
			QStringLiteral("project"),
			QStringLiteral("Ctrl+Alt+M"),
			QCoreApplication::translate("SlopStudioSemantics", "Copy the resolved project manifest to the clipboard for review or support.")),

		// Packages.
		shortcut(
			QStringLiteral("package-open"),
			QStringLiteral("package.open"),
			QCoreApplication::translate("SlopStudioSemantics", "Open Package"),
			QStringLiteral("package"),
			QStringLiteral("Ctrl+Alt+O"),
			QCoreApplication::translate("SlopStudioSemantics", "Open a PAK, WAD, ZIP, or PK3 package for browsing.")),
		shortcut(
			QStringLiteral("package-open-folder"),
			QStringLiteral("package.open-folder"),
			QCoreApplication::translate("SlopStudioSemantics", "Open Folder As Package"),
			QStringLiteral("package"),
			QStringLiteral("Ctrl+Shift+O"),
			QCoreApplication::translate("SlopStudioSemantics", "Mount a plain folder with the same read-only package semantics as an archive.")),
		shortcut(
			QStringLiteral("package-close"),
			QStringLiteral("package.close"),
			QCoreApplication::translate("SlopStudioSemantics", "Close Package"),
			QStringLiteral("package"),
			QStringLiteral("Ctrl+W"),
			QCoreApplication::translate("SlopStudioSemantics", "Close the open package and report any staged edits that would be discarded.")),
		surfaceShortcut(
			QStringLiteral("package-save-as"),
			QStringLiteral("package.save-as"),
			QCoreApplication::translate("SlopStudioSemantics", "Save Package As"),
			QStringLiteral("package"),
			QStringLiteral("Ctrl+Shift+S"),
			QCoreApplication::translate("SlopStudioSemantics", "Write staged package changes to a new package path.")),
		surfaceShortcut(
			QStringLiteral("package-extract-selected"),
			QStringLiteral("package.extract-selected"),
			QCoreApplication::translate("SlopStudioSemantics", "Extract Selected Entries"),
			QStringLiteral("package"),
			QStringLiteral("Ctrl+E"),
			QCoreApplication::translate("SlopStudioSemantics", "Extract the selected package entries into a chosen output folder.")),
		shortcut(
			QStringLiteral("package-extract-all"),
			QStringLiteral("package.extract-all"),
			QCoreApplication::translate("SlopStudioSemantics", "Extract All Entries"),
			QStringLiteral("package"),
			QStringLiteral("Ctrl+Shift+E"),
			QCoreApplication::translate("SlopStudioSemantics", "Extract every package entry into a chosen output folder.")),
		surfaceShortcut(
			QStringLiteral("package-stage-add"),
			QStringLiteral("package.stage-add"),
			QCoreApplication::translate("SlopStudioSemantics", "Stage Add"),
			QStringLiteral("package"),
			QStringLiteral("Ctrl+Alt+A"),
			QCoreApplication::translate("SlopStudioSemantics", "Stage new files for the next package save-as without touching the open package.")),
		surfaceShortcut(
			QStringLiteral("package-stage-replace"),
			QStringLiteral("package.stage-replace"),
			QCoreApplication::translate("SlopStudioSemantics", "Stage Replace"),
			QStringLiteral("package"),
			QStringLiteral("Ctrl+Alt+R"),
			QCoreApplication::translate("SlopStudioSemantics", "Stage a replacement for the selected entry without touching the open package.")),
		surfaceShortcut(
			QStringLiteral("package-stage-rename"),
			QStringLiteral("package.stage-rename"),
			QCoreApplication::translate("SlopStudioSemantics", "Stage Rename"),
			QStringLiteral("package"),
			QStringLiteral("F2"),
			QCoreApplication::translate("SlopStudioSemantics", "Stage a new archive path for the selected entry.")),
		surfaceShortcut(
			QStringLiteral("package-stage-delete"),
			QStringLiteral("package.stage-delete"),
			QCoreApplication::translate("SlopStudioSemantics", "Stage Delete"),
			QStringLiteral("package"),
			QStringLiteral("Del"),
			QCoreApplication::translate("SlopStudioSemantics", "Stage the selected entry for removal from the next saved package.")),
		surfaceShortcut(
			QStringLiteral("package-unstage-last"),
			QStringLiteral("package.unstage-last"),
			QCoreApplication::translate("SlopStudioSemantics", "Undo Package Edit"),
			QStringLiteral("package"),
			QStringLiteral("Ctrl+Z"),
			QCoreApplication::translate("SlopStudioSemantics", "Undo the last package edit or import group while the Packages surface has focus.")),
		surfaceShortcut(QStringLiteral("package-redo"), QStringLiteral("package.redo"),
			QCoreApplication::translate("SlopStudioSemantics", "Redo Package Edit"), QStringLiteral("package"), QStringLiteral("Ctrl+Shift+Z"),
			QCoreApplication::translate("SlopStudioSemantics", "Redo the last undone package edit while the Packages surface has focus.")),
		surfaceShortcut(QStringLiteral("package-save-draft"), QStringLiteral("package.save-draft"),
			QCoreApplication::translate("SlopStudioSemantics", "Save Package Draft"), QStringLiteral("package"), QStringLiteral("Ctrl+S"),
			QCoreApplication::translate("SlopStudioSemantics", "Save package content and edit history in a portable draft directory.")),

		// Level documents.
		surfaceShortcut(QStringLiteral("map-new"), QStringLiteral("map.new"),
			QCoreApplication::translate("SlopStudioSemantics", "New Map"), QStringLiteral("map"), QStringLiteral("Ctrl+N"),
			QCoreApplication::translate("SlopStudioSemantics", "Create a map while the Levels surface has focus.")),
		surfaceShortcut(QStringLiteral("map-save"), QStringLiteral("map.save"),
			QCoreApplication::translate("SlopStudioSemantics", "Save Map"), QStringLiteral("map"), QStringLiteral("Ctrl+S"),
			QCoreApplication::translate("SlopStudioSemantics", "Save the map while the Levels surface has focus.")),
		shortcut(
			QStringLiteral("map-open"),
			QStringLiteral("map.open"),
			QCoreApplication::translate("SlopStudioSemantics", "Open Map"),
			QStringLiteral("map"),
			QStringLiteral("Ctrl+M"),
			QCoreApplication::translate("SlopStudioSemantics", "Open a Quake/Quake III .map or a Doom map lump for inspection and editing.")),
		shortcut(
			QStringLiteral("map-save-as"),
			QStringLiteral("map.save-as"),
			QCoreApplication::translate("SlopStudioSemantics", "Save Map As"),
			QStringLiteral("map"),
			QStringLiteral("Ctrl+Alt+S"),
			QCoreApplication::translate("SlopStudioSemantics", "Choose a destination for the edited map, with a backup before replacing an existing file.")),
		surfaceShortcut(
			QStringLiteral("map-undo"),
			QStringLiteral("map.undo"),
			QCoreApplication::translate("SlopStudioSemantics", "Undo Map Edit"),
			QStringLiteral("map"),
			QStringLiteral("Ctrl+Z"),
			QCoreApplication::translate("SlopStudioSemantics", "Undo the last edit to the open level document while the Levels surface has focus.")),
		surfaceShortcut(
			QStringLiteral("map-redo"),
			QStringLiteral("map.redo"),
			QCoreApplication::translate("SlopStudioSemantics", "Redo Map Edit"),
			QStringLiteral("map"),
			QStringLiteral("Ctrl+Y"),
			QCoreApplication::translate("SlopStudioSemantics", "Redo the last undone edit to the open level document while the Levels surface has focus."),
			{QStringLiteral("Ctrl+Shift+Z")}),
		surfaceShortcut(
			QStringLiteral("map-hide-selection"),
			QStringLiteral("map.hide-selection"),
			QCoreApplication::translate("SlopStudioSemantics", "Hide Selection"),
			QStringLiteral("map"),
			QStringLiteral("H"),
			QCoreApplication::translate("SlopStudioSemantics", "Hide the selected map objects from the view, without changing the map, while the Levels surface has focus.")),
		surfaceShortcut(
			QStringLiteral("map-show-all"),
			QStringLiteral("map.show-all"),
			QCoreApplication::translate("SlopStudioSemantics", "Show All Hidden"),
			QStringLiteral("map"),
			QStringLiteral("Shift+H"),
			QCoreApplication::translate("SlopStudioSemantics", "Clear temporary hiding while the Levels surface has focus. Saved scene visibility and view filters remain in effect.")),
		surfaceShortcut(
			QStringLiteral("map-select-all"),
			QStringLiteral("map.select-all"),
			QCoreApplication::translate("SlopStudioSemantics", "Select All"),
			QStringLiteral("map"),
			QStringLiteral("Ctrl+A"),
			QCoreApplication::translate("SlopStudioSemantics", "Select every entity, brush, and patch, or every Doom thing, that is not hidden, while the Levels surface has focus.")),
		surfaceShortcut(
			QStringLiteral("map-select-none"),
			QStringLiteral("map.select-none"),
			QCoreApplication::translate("SlopStudioSemantics", "Select None"),
			QStringLiteral("map"),
			QStringLiteral("Ctrl+Shift+A"),
			QCoreApplication::translate("SlopStudioSemantics", "Clear the Levels selection.")),
		surfaceShortcut(
			QStringLiteral("map-invert-selection"),
			QStringLiteral("map.invert-selection"),
			QCoreApplication::translate("SlopStudioSemantics", "Invert Selection"),
			QStringLiteral("map"),
			QStringLiteral("Ctrl+I"),
			QCoreApplication::translate("SlopStudioSemantics", "Select what is not selected and drop what is, among the objects Select All would pick.")),
		surfaceShortcut(
			QStringLiteral("map-preview-wireframe"),
			QStringLiteral("map.preview-wireframe"),
			QCoreApplication::translate("SlopStudioSemantics", "3D Wireframe"),
			QStringLiteral("map"),
			QStringLiteral("W"),
			QCoreApplication::translate("SlopStudioSemantics", "Switch the Levels 3D preview between its faces, textured or flat, and wireframe while it is showing; the choice is kept.")),
		surfaceShortcut(
			QStringLiteral("map-clip-tool"),
			QStringLiteral("map.clip-tool"),
			QCoreApplication::translate("SlopStudioSemantics", "Clip Tool"),
			QStringLiteral("map"),
			QStringLiteral("X"),
			QCoreApplication::translate("SlopStudioSemantics", "Turn the Levels clip tool on or off: drag a line across brushes, Tab to choose what stays, Enter to cut.")),
		surfaceShortcut(
			QStringLiteral("map-draw-sector"),
			QStringLiteral("map.draw-sector"),
			QCoreApplication::translate("SlopStudioSemantics", "Draw Sector"),
			QStringLiteral("map"),
			QStringLiteral("D"),
			QCoreApplication::translate("SlopStudioSemantics", "Turn Draw Sector on or off on a Doom map: click the corners, then the first corner or Enter to close the shape.")),
		// Doom Builder's raise and lower, on the map view only: in the objects
		// list and the inspector Page Up and Page Down still page.
		surfaceShortcut(
			QStringLiteral("map-raise-floor"),
			QStringLiteral("map.raise-floor"),
			QCoreApplication::translate("SlopStudioSemantics", "Raise Floor"),
			QStringLiteral("map-view"),
			QStringLiteral("PgUp"),
			QCoreApplication::translate("SlopStudioSemantics", "Raise the selected Doom sectors' floors by 8, or by 1 with Shift, while the map view has focus."),
			{QStringLiteral("Shift+PgUp")}),
		surfaceShortcut(
			QStringLiteral("map-lower-floor"),
			QStringLiteral("map.lower-floor"),
			QCoreApplication::translate("SlopStudioSemantics", "Lower Floor"),
			QStringLiteral("map-view"),
			QStringLiteral("PgDown"),
			QCoreApplication::translate("SlopStudioSemantics", "Lower the selected Doom sectors' floors by 8, or by 1 with Shift, while the map view has focus."),
			{QStringLiteral("Shift+PgDown")}),
		surfaceShortcut(
			QStringLiteral("map-raise-ceiling"),
			QStringLiteral("map.raise-ceiling"),
			QCoreApplication::translate("SlopStudioSemantics", "Raise Ceiling"),
			QStringLiteral("map-view"),
			QStringLiteral("Ctrl+PgUp"),
			QCoreApplication::translate("SlopStudioSemantics", "Raise the selected Doom sectors' ceilings by 8, or by 1 with Shift, while the map view has focus."),
			{QStringLiteral("Ctrl+Shift+PgUp")}),
		surfaceShortcut(
			QStringLiteral("map-lower-ceiling"),
			QStringLiteral("map.lower-ceiling"),
			QCoreApplication::translate("SlopStudioSemantics", "Lower Ceiling"),
			QStringLiteral("map-view"),
			QStringLiteral("Ctrl+PgDown"),
			QCoreApplication::translate("SlopStudioSemantics", "Lower the selected Doom sectors' ceilings by 8, or by 1 with Shift, while the map view has focus."),
			{QStringLiteral("Ctrl+Shift+PgDown")}),
		surfaceShortcut(
			QStringLiteral("map-grid-smaller"),
			QStringLiteral("map.grid-smaller"),
			QCoreApplication::translate("SlopStudioSemantics", "Smaller Grid"),
			QStringLiteral("map"),
			QStringLiteral("["),
			QCoreApplication::translate("SlopStudioSemantics", "Halve the Levels grid, down to 0.125 units (1 unit on Doom and Hexen maps), as Radiant and TrenchBroom do, while the Levels surface has focus.")),
		surfaceShortcut(
			QStringLiteral("map-grid-larger"),
			QStringLiteral("map.grid-larger"),
			QCoreApplication::translate("SlopStudioSemantics", "Larger Grid"),
			QStringLiteral("map"),
			QStringLiteral("]"),
			QCoreApplication::translate("SlopStudioSemantics", "Double the Levels grid, up to 1024 units, while the Levels surface has focus.")),
		surfaceShortcut(
			QStringLiteral("map-copy-selection"),
			QStringLiteral("map.copy-selection"),
			QCoreApplication::translate("SlopStudioSemantics", "Copy Selection"),
			QStringLiteral("map"),
			QStringLiteral("Ctrl+C"),
			QCoreApplication::translate("SlopStudioSemantics", "Copy selected objects as .map text. In the TrenchBroom profile, picked faces copy face attributes.")),
		surfaceShortcut(
			QStringLiteral("map-cut-selection"),
			QStringLiteral("map.cut-selection"),
			QCoreApplication::translate("SlopStudioSemantics", "Cut Selection"),
			QStringLiteral("map"),
			QStringLiteral("Ctrl+X"),
			QCoreApplication::translate("SlopStudioSemantics", "Copy selected map objects as .map text, then delete them; undo restores them. Unavailable for TrenchBroom face picks.")),
		surfaceShortcut(
			QStringLiteral("map-paste"),
			QStringLiteral("map.paste"),
			QCoreApplication::translate("SlopStudioSemantics", "Paste"),
			QStringLiteral("map"),
			QStringLiteral("Ctrl+V"),
			QCoreApplication::translate("SlopStudioSemantics", "Paste objects at the cursor in the TrenchBroom profile, or at original coordinates otherwise. Face text pastes attributes onto selected faces or brushes, retaining contents.")),
		surfaceShortcut(QStringLiteral("map-paste-at-original"), QStringLiteral("map.paste-at-original"),
			QCoreApplication::translate("SlopStudioSemantics", "Paste at Original Position"), QStringLiteral("map"), QString(),
			QCoreApplication::translate("SlopStudioSemantics", "Paste clipboard objects at their saved coordinates.")),
		surfaceShortcut(QStringLiteral("map-paste-to-camera"), QStringLiteral("map.paste-to-camera"),
			QCoreApplication::translate("SlopStudioSemantics", "Paste to Camera"), QStringLiteral("map"), QString(),
			QCoreApplication::translate("SlopStudioSemantics", "Centre the pasted assembly on the camera or orbit focus using a snapped translation.")),
		surfaceShortcut(
			QStringLiteral("map-duplicate-selection"),
			QStringLiteral("map.duplicate-selection"),
			QCoreApplication::translate("SlopStudioSemantics", "Duplicate Selection"),
			QStringLiteral("map"),
			QStringLiteral("Ctrl+D"),
			QCoreApplication::translate("SlopStudioSemantics", "Copy selected map objects using the profile's duplicate placement; NRC requires object mode.")),
		surfaceShortcut(QStringLiteral("map-duplicate-unique"), QStringLiteral("map.duplicate-unique"),
			QCoreApplication::translate("SlopStudioSemantics", "Duplicate, Make Unique"), QStringLiteral("map"), QString(),
			QCoreApplication::translate("SlopStudioSemantics", "Copy selected objects into new entity owners and rename copied target names and references together. Requires object mode.")),
		surfaceShortcut(QStringLiteral("map-repeat"), QStringLiteral("map.repeat"),
			QCoreApplication::translate("SlopStudioSemantics", "Repeat Last Commands"), QStringLiteral("map"), QString(),
			QCoreApplication::translate("SlopStudioSemantics", "Replay recorded object transforms and duplications on the current selection.")),
		surfaceShortcut(QStringLiteral("map-clear-repeat"), QStringLiteral("map.clear-repeat"),
			QCoreApplication::translate("SlopStudioSemantics", "Clear Repeatable Commands"), QStringLiteral("map"), QString(),
			QCoreApplication::translate("SlopStudioSemantics", "Forget repeatable commands without changing the map or its undo history.")),
		surfaceShortcut(
			QStringLiteral("map-delete-selection"),
			QStringLiteral("map.delete-selection"),
			QCoreApplication::translate("SlopStudioSemantics", "Delete Selection"),
			QStringLiteral("map"),
			QStringLiteral("Del"),
			QCoreApplication::translate("SlopStudioSemantics", "Delete the selected entities, brushes, and patches from the open map while the Levels surface has focus; undo restores them.")),

		// Code and script editor.
		surfaceShortcut(
			QStringLiteral("code-new"), QStringLiteral("code.new"),
			QCoreApplication::translate("SlopStudioSemantics", "New Text File"),
			QStringLiteral("code"), QStringLiteral("Ctrl+N"),
			QCoreApplication::translate("SlopStudioSemantics", "Start an untitled document while the Code surface has focus.")),
		surfaceShortcut(
			QStringLiteral("code-save-as"), QStringLiteral("code.save-as"),
			QCoreApplication::translate("SlopStudioSemantics", "Save File As"),
			QStringLiteral("code"), QStringLiteral("Ctrl+Shift+S"),
			QCoreApplication::translate("SlopStudioSemantics", "Choose a destination for the document while the Code surface has focus.")),
		surfaceShortcut(
			QStringLiteral("code-save"),
			QStringLiteral("code.save"),
			QCoreApplication::translate("SlopStudioSemantics", "Save File"),
			QStringLiteral("code"),
			QStringLiteral("Ctrl+S"),
			QCoreApplication::translate("SlopStudioSemantics", "Save the file open in the code editor while the Code surface has focus.")),
		shortcut(
			QStringLiteral("code-find-in-files"),
			QStringLiteral("code.find-replace"),
			QCoreApplication::translate("SlopStudioSemantics", "Find In Project Files"),
			QStringLiteral("code"),
			QStringLiteral("Ctrl+Shift+F"),
			QCoreApplication::translate("SlopStudioSemantics", "Search, and optionally replace, across every text file in the project.")),
		surfaceShortcut(
			QStringLiteral("code-go-to-line"),
			QStringLiteral("code.go-to-line"),
			QCoreApplication::translate("SlopStudioSemantics", "Go To Line"),
			QStringLiteral("code"),
			QStringLiteral("Ctrl+L"),
			QCoreApplication::translate("SlopStudioSemantics", "Move the editor caret to a line number.")),
		surfaceShortcut(
			QStringLiteral("code-go-to-symbol"),
			QStringLiteral("code.go-to-symbol"),
			QCoreApplication::translate("SlopStudioSemantics", "Go To Symbol"),
			QStringLiteral("code"),
			QStringLiteral("Ctrl+T"),
			QCoreApplication::translate("SlopStudioSemantics", "Find a function, shader, or entity class in the open file by name and move the caret to it.")),
		surfaceShortcut(
			QStringLiteral("code-go-to-definition"),
			QStringLiteral("code.go-to-definition"),
			QCoreApplication::translate("SlopStudioSemantics", "Go to Definition"),
			QStringLiteral("code"),
			QStringLiteral("F12"),
			QCoreApplication::translate("SlopStudioSemantics", "Open where the name at the caret is defined, in the open file or across the project; Ctrl+click does the same.")),
		surfaceShortcut(
			QStringLiteral("code-complete"),
			QStringLiteral("code.complete"),
			QCoreApplication::translate("SlopStudioSemantics", "Complete Name"),
			QStringLiteral("code"),
			QStringLiteral("Ctrl+Space"),
			QCoreApplication::translate("SlopStudioSemantics", "Offer the names that start with what is typed: the file's own, the project's symbols, and the language's keywords.")),
		surfaceShortcut(
			QStringLiteral("code-find-references"),
			QStringLiteral("code.find-references"),
			QCoreApplication::translate("SlopStudioSemantics", "Find All References"),
			QStringLiteral("code"),
			QStringLiteral("Shift+F12"),
			QCoreApplication::translate("SlopStudioSemantics", "List every use of the name at the caret across the project, as whole words, under Search Results.")),
		surfaceShortcut(
			QStringLiteral("code-format-document"), QStringLiteral("code.format-document"),
			QCoreApplication::translate("SlopStudioSemantics", "Format Document"), QStringLiteral("code"), QStringLiteral("Alt+Shift+F"),
			QCoreApplication::translate("SlopStudioSemantics", "Format the open document with the connected language server as one unsaved Undo step.")),
		surfaceShortcut(
			QStringLiteral("code-rename-symbol"), QStringLiteral("code.rename-symbol"),
			QCoreApplication::translate("SlopStudioSemantics", "Rename Symbol"), QStringLiteral("code"), QStringLiteral("F2"),
			QCoreApplication::translate("SlopStudioSemantics", "Preview a language server rename across project files and unsaved documents.")),
		surfaceShortcut(
			QStringLiteral("code-code-actions"), QStringLiteral("code.code-actions"),
			QCoreApplication::translate("SlopStudioSemantics", "Code Actions"), QStringLiteral("code"), QStringLiteral("Ctrl+."),
			QCoreApplication::translate("SlopStudioSemantics", "Choose a provider quick fix or refactoring and review its project edits.")),
		surfaceShortcut(
			QStringLiteral("code-format-selection"), QStringLiteral("code.format-selection"),
			QCoreApplication::translate("SlopStudioSemantics", "Format Selection"), QStringLiteral("code"), QStringLiteral("Ctrl+Alt+F"),
			QCoreApplication::translate("SlopStudioSemantics", "Format selected source text with the connected language server.")),
		surfaceShortcut(
			QStringLiteral("code-quick-info"),
			QStringLiteral("code.quick-info"),
			QCoreApplication::translate("SlopStudioSemantics", "Quick Info"),
			QStringLiteral("code"),
			QStringLiteral("Ctrl+I"),
			QCoreApplication::translate("SlopStudioSemantics", "Show the symbol's type and documentation from the connected language server.")),
		surfaceShortcut(
			QStringLiteral("code-parameter-hints"), QStringLiteral("code.parameter-hints"),
			QCoreApplication::translate("SlopStudioSemantics", "Parameter Hints"), QStringLiteral("code"), QStringLiteral("Ctrl+Shift+Space"),
			QCoreApplication::translate("SlopStudioSemantics", "Show call overloads and the active parameter from the connected language server.")),
		shortcut(
			QStringLiteral("shell-navigate-back"),
			QStringLiteral("shell.navigate-back"),
			QCoreApplication::translate("SlopStudioSemantics", "Go Back"),
			QStringLiteral("global"),
			QStringLiteral("Alt+Left"),
			QCoreApplication::translate("SlopStudioSemantics", "Return to the page and place you were at before the last page change or jump; the mouse's back button does the same.")),
		shortcut(
			QStringLiteral("shell-navigate-forward"),
			QStringLiteral("shell.navigate-forward"),
			QCoreApplication::translate("SlopStudioSemantics", "Go Forward"),
			QStringLiteral("global"),
			QStringLiteral("Alt+Right"),
			QCoreApplication::translate("SlopStudioSemantics", "Return to the place Go Back left; the mouse's forward button does the same.")),
		shortcut(
			QStringLiteral("build-next-problem"),
			QStringLiteral("build.next-problem"),
			QCoreApplication::translate("SlopStudioSemantics", "Next Build Problem"),
			QStringLiteral("global"),
			QStringLiteral("F4"),
			QCoreApplication::translate("SlopStudioSemantics", "Show the last build's next problem, wrapping at the end, from any page: the map object, file line, or leak it names.")),
		shortcut(
			QStringLiteral("build-previous-problem"),
			QStringLiteral("build.previous-problem"),
			QCoreApplication::translate("SlopStudioSemantics", "Previous Build Problem"),
			QStringLiteral("global"),
			QStringLiteral("Shift+F4"),
			QCoreApplication::translate("SlopStudioSemantics", "Show the last build's previous problem, wrapping at the start.")),
		surfaceShortcut(
			QStringLiteral("code-next-problem"),
			QStringLiteral("code.next-problem"),
			QCoreApplication::translate("SlopStudioSemantics", "Next Problem"),
			QStringLiteral("code"),
			QStringLiteral("F8"),
			QCoreApplication::translate("SlopStudioSemantics", "Move the caret to the open file's next diagnostic, wrapping at the end, and say what it reports.")),
		surfaceShortcut(
			QStringLiteral("code-previous-problem"),
			QStringLiteral("code.previous-problem"),
			QCoreApplication::translate("SlopStudioSemantics", "Previous Problem"),
			QStringLiteral("code"),
			QStringLiteral("Shift+F8"),
			QCoreApplication::translate("SlopStudioSemantics", "Move the caret to the open file's previous diagnostic, wrapping at the start.")),
		surfaceShortcut(
			QStringLiteral("code-fold"),
			QStringLiteral("code.fold"),
			QCoreApplication::translate("SlopStudioSemantics", "Fold"),
			QStringLiteral("code"),
			QStringLiteral("Ctrl+Shift+["),
			QCoreApplication::translate("SlopStudioSemantics", "Fold the innermost { } block holding the caret; the gutter's chevrons fold with a click.")),
		surfaceShortcut(
			QStringLiteral("code-unfold"),
			QStringLiteral("code.unfold"),
			QCoreApplication::translate("SlopStudioSemantics", "Unfold"),
			QStringLiteral("code"),
			QStringLiteral("Ctrl+Shift+]"),
			QCoreApplication::translate("SlopStudioSemantics", "Unfold the block folded on the caret's line.")),
		surfaceShortcut(
			QStringLiteral("code-find-next"),
			QStringLiteral("code.find-next"),
			QCoreApplication::translate("SlopStudioSemantics", "Find Next"),
			QStringLiteral("code"),
			QStringLiteral("F3"),
			QCoreApplication::translate("SlopStudioSemantics", "Select the next match of the editor find text in the open file.")),
		surfaceShortcut(
			QStringLiteral("code-replace"),
			QStringLiteral("code.replace"),
			QCoreApplication::translate("SlopStudioSemantics", "Replace"),
			QStringLiteral("code"),
			QStringLiteral("Ctrl+H"),
			QCoreApplication::translate("SlopStudioSemantics", "Open the editor find bar with its replace row: Enter replaces the selected match, Ctrl+Enter every match.")),
		surfaceShortcut(
			QStringLiteral("code-find-previous"),
			QStringLiteral("code.find-previous"),
			QCoreApplication::translate("SlopStudioSemantics", "Find Previous"),
			QStringLiteral("code"),
			QStringLiteral("Shift+F3"),
			QCoreApplication::translate("SlopStudioSemantics", "Select the previous match of the editor find text in the open file.")),
		surfaceShortcut(
			QStringLiteral("code-toggle-comment"),
			QStringLiteral("code.toggle-comment"),
			QCoreApplication::translate("SlopStudioSemantics", "Toggle Line Comment"),
			QStringLiteral("code"),
			QStringLiteral("Ctrl+/"),
			QCoreApplication::translate("SlopStudioSemantics", "Comment or uncomment the selected lines with the file type's line comment.")),
		surfaceShortcut(
			QStringLiteral("code-duplicate-lines"),
			QStringLiteral("code.duplicate-lines"),
			QCoreApplication::translate("SlopStudioSemantics", "Duplicate Lines"),
			QStringLiteral("code"),
			QStringLiteral("Ctrl+D"),
			QCoreApplication::translate("SlopStudioSemantics", "Copy the selected lines below themselves.")),
		surfaceShortcut(
			QStringLiteral("code-move-lines-up"),
			QStringLiteral("code.move-lines-up"),
			QCoreApplication::translate("SlopStudioSemantics", "Move Lines Up"),
			QStringLiteral("code"),
			QStringLiteral("Alt+Up"),
			QCoreApplication::translate("SlopStudioSemantics", "Swap the selected lines with the line above.")),
		surfaceShortcut(
			QStringLiteral("code-move-lines-down"),
			QStringLiteral("code.move-lines-down"),
			QCoreApplication::translate("SlopStudioSemantics", "Move Lines Down"),
			QStringLiteral("code"),
			QStringLiteral("Alt+Down"),
			QCoreApplication::translate("SlopStudioSemantics", "Swap the selected lines with the line below.")),
		surfaceShortcut(
			QStringLiteral("code-close-file"),
			QStringLiteral("code.close-file"),
			QCoreApplication::translate("SlopStudioSemantics", "Close File"),
			QStringLiteral("code"),
			QStringLiteral("Ctrl+F4"),
			QCoreApplication::translate("SlopStudioSemantics", "Close the code editor's current tab, asking first if it has unsaved changes.")),
		surfaceShortcut(
			QStringLiteral("code-next-file"),
			QStringLiteral("code.next-file"),
			QCoreApplication::translate("SlopStudioSemantics", "Next Open File"),
			QStringLiteral("code"),
			QStringLiteral("Ctrl+PgDown"),
			// Not Ctrl+Tab: in the editor, where Tab types a tab, Ctrl+Tab is
			// the keyboard's way out.
			QCoreApplication::translate("SlopStudioSemantics", "Move to the code editor's next tab.")),
		surfaceShortcut(
			QStringLiteral("code-previous-file"),
			QStringLiteral("code.previous-file"),
			QCoreApplication::translate("SlopStudioSemantics", "Previous Open File"),
			QStringLiteral("code"),
			QStringLiteral("Ctrl+PgUp"),
			QCoreApplication::translate("SlopStudioSemantics", "Move to the code editor's previous tab.")),
		surfaceShortcut(
			QStringLiteral("code-zoom-in"),
			QStringLiteral("code.zoom-in"),
			QCoreApplication::translate("SlopStudioSemantics", "Zoom In Editor"),
			QStringLiteral("code"),
			QStringLiteral("Ctrl+="),
			QCoreApplication::translate("SlopStudioSemantics", "Make the code editor's text larger; Ctrl with the mouse wheel does the same."),
			{QStringLiteral("Ctrl++")}),
		surfaceShortcut(
			QStringLiteral("code-zoom-out"),
			QStringLiteral("code.zoom-out"),
			QCoreApplication::translate("SlopStudioSemantics", "Zoom Out Editor"),
			QStringLiteral("code"),
			QStringLiteral("Ctrl+-"),
			QCoreApplication::translate("SlopStudioSemantics", "Make the code editor's text smaller.")),

		// Compilers and the build pipeline.
		shortcut(
			QStringLiteral("compiler-run"),
			QStringLiteral("compiler.run"),
			QCoreApplication::translate("SlopStudioSemantics", "Run Compiler Profile"),
			QStringLiteral("compiler"),
			QStringLiteral("Ctrl+R"),
			QCoreApplication::translate("SlopStudioSemantics", "Run the selected compiler profile through the shared task runner.")),
		shortcut(
			QStringLiteral("compiler-copy-cli"),
			QStringLiteral("compiler.copy-cli"),
			QCoreApplication::translate("SlopStudioSemantics", "Copy CLI Equivalent"),
			QStringLiteral("compiler"),
			QStringLiteral("Ctrl+Shift+C"),
			QCoreApplication::translate("SlopStudioSemantics", "Copy a reproducible command line for the selected GUI action.")),
		shortcut(
			QStringLiteral("build-run-pipeline"),
			QStringLiteral("build.run-pipeline"),
			QCoreApplication::translate("SlopStudioSemantics", "Run Build Pipeline"),
			QStringLiteral("build"),
			QStringLiteral("F7"),
			QCoreApplication::translate("SlopStudioSemantics", "Run the ordered compile, validate, and package pipeline with per-step task state.")),
		shortcut(
			QStringLiteral("build-run-and-launch"),
			QStringLiteral("build.run-and-launch"),
			QCoreApplication::translate("SlopStudioSemantics", "Build and Launch"),
			QStringLiteral("build"),
			QStringLiteral("F5"),
			QCoreApplication::translate("SlopStudioSemantics", "Run the build pipeline, then launch the game with the built map once it succeeds.")),

		// Game installations.
		shortcut(
			QStringLiteral("detect-installations"),
			QStringLiteral("game.detect-installations"),
			QCoreApplication::translate("SlopStudioSemantics", "Detect Game Installations"),
			QStringLiteral("game"),
			QStringLiteral("Ctrl+Shift+G"),
			QCoreApplication::translate("SlopStudioSemantics", "Scan for candidate installations and present them for confirmation without writing game files.")),
		shortcut(
			QStringLiteral("launch-game"),
			QStringLiteral("game.launch"),
			QCoreApplication::translate("SlopStudioSemantics", "Launch Game"),
			QStringLiteral("game"),
			QStringLiteral("Ctrl+G"),
			QCoreApplication::translate("SlopStudioSemantics", "Launch the selected installation with the current mod/map arguments.")),

		// Audio browser.
		surfaceShortcut(
			QStringLiteral("audio-play-pause"),
			QStringLiteral("audio.play-pause"),
			QCoreApplication::translate("SlopStudioSemantics", "Play or Pause Sound"),
			QStringLiteral("audio"),
			QStringLiteral("Space"),
			QCoreApplication::translate("SlopStudioSemantics", "Play the selected sound from the playhead, or pause it, while the sound list or the waveform has focus.")),

		// Activity and support.
		surfaceShortcut(
			QStringLiteral("cancel-task"),
			QStringLiteral("activity.cancel"),
			QCoreApplication::translate("SlopStudioSemantics", "Cancel Selected Task"),
			QStringLiteral("activity"),
			QStringLiteral("Esc"),
			QCoreApplication::translate("SlopStudioSemantics", "Cancel the selected cancellable task, or the running build, while the Activity panel or Build surface has focus."),
			{},
			false),
		shortcut(
			QStringLiteral("diagnostics-bundle"),
			QStringLiteral("diagnostics.bundle"),
			QCoreApplication::translate("SlopStudioSemantics", "Copy Diagnostic Bundle"),
			QStringLiteral("diagnostics"),
			QStringLiteral("Ctrl+Alt+D"),
			QCoreApplication::translate("SlopStudioSemantics", "Copy a redacted diagnostic bundle for support and reproducibility.")),

		// Mode rail. Ctrl+1..Ctrl+9 select the nine work surfaces in rail order;
		// Ctrl+0 opens settings, which is a destination rather than a mode.
		shortcut(
			QStringLiteral("mode-workspace"),
			QStringLiteral("shell.mode.workspace"),
			QCoreApplication::translate("SlopStudioSemantics", "Workspace Mode"),
			QStringLiteral("shell"),
			QStringLiteral("Ctrl+1"),
			QCoreApplication::translate("SlopStudioSemantics", "Show the workspace dashboard.")),
		shortcut(
			QStringLiteral("mode-levels"),
			QStringLiteral("shell.mode.levels"),
			QCoreApplication::translate("SlopStudioSemantics", "Levels Mode"),
			QStringLiteral("shell"),
			QStringLiteral("Ctrl+2"),
			QCoreApplication::translate("SlopStudioSemantics", "Show the level editor surface.")),
		shortcut(
			QStringLiteral("mode-models"),
			QStringLiteral("shell.mode.models"),
			QCoreApplication::translate("SlopStudioSemantics", "Models Mode"),
			QStringLiteral("shell"),
			QStringLiteral("Ctrl+3"),
			QCoreApplication::translate("SlopStudioSemantics", "Show the model browser and inspector.")),
		shortcut(
			QStringLiteral("mode-textures"),
			QStringLiteral("shell.mode.textures"),
			QCoreApplication::translate("SlopStudioSemantics", "Textures Mode"),
			QStringLiteral("shell"),
			QStringLiteral("Ctrl+4"),
			QCoreApplication::translate("SlopStudioSemantics", "Show the texture and palette surface.")),
		shortcut(
			QStringLiteral("mode-audio"),
			QStringLiteral("shell.mode.audio"),
			QCoreApplication::translate("SlopStudioSemantics", "Audio Mode"),
			QStringLiteral("shell"),
			QStringLiteral("Ctrl+5"),
			QCoreApplication::translate("SlopStudioSemantics", "Show the audio browser and waveform preview.")),
		shortcut(
			QStringLiteral("mode-packages"),
			QStringLiteral("shell.mode.packages"),
			QCoreApplication::translate("SlopStudioSemantics", "Packages Mode"),
			QStringLiteral("shell"),
			QStringLiteral("Ctrl+6"),
			QCoreApplication::translate("SlopStudioSemantics", "Show the package manager surface.")),
		shortcut(
			QStringLiteral("mode-code"),
			QStringLiteral("shell.mode.code"),
			QCoreApplication::translate("SlopStudioSemantics", "Code Mode"),
			QStringLiteral("shell"),
			QStringLiteral("Ctrl+7"),
			QCoreApplication::translate("SlopStudioSemantics", "Show the code and script editor surface.")),
		shortcut(
			QStringLiteral("mode-shaders"),
			QStringLiteral("shell.mode.shaders"),
			QCoreApplication::translate("SlopStudioSemantics", "Materials Mode"),
			QStringLiteral("shell"),
			QStringLiteral("Ctrl+8"),
			QCoreApplication::translate("SlopStudioSemantics", "Show the textures, shaders and materials surface.")),
		shortcut(
			QStringLiteral("mode-build"),
			QStringLiteral("shell.mode.build"),
			QCoreApplication::translate("SlopStudioSemantics", "Build Mode"),
			QStringLiteral("shell"),
			QStringLiteral("Ctrl+9"),
			QCoreApplication::translate("SlopStudioSemantics", "Show the compiler, pipeline, and activity surface.")),
		shortcut(
			QStringLiteral("mode-settings"),
			QStringLiteral("shell.mode.settings"),
			QCoreApplication::translate("SlopStudioSemantics", "Settings Mode"),
			QStringLiteral("shell"),
			QStringLiteral("Ctrl+0"),
			QCoreApplication::translate("SlopStudioSemantics", "Show settings, setup progress, and diagnostics.")),
	};
}

bool shortcutForCommandId(const QString& commandId, ShortcutDescriptor* out)
{
	const QString requested = normalizedId(commandId);
	for (const ShortcutDescriptor& descriptor : shortcutDescriptors()) {
		if (normalizedId(descriptor.commandId) == requested) {
			if (out) {
				*out = descriptor;
			}
			return true;
		}
	}
	return false;
}

bool shortcutRegistryHasConflicts(QStringList* conflicts)
{
	// A window-level key may be claimed once across the whole registry,
	// alternates included. Surface-scoped keys fire only on their own surface,
	// so two of them on different surfaces may share a sequence (Ctrl+Z undoes a
	// map edit on Levels and a staged change on Packages).
	QHash<QString, QVector<const ShortcutDescriptor*>> owners;
	QStringList found;
	const QVector<ShortcutDescriptor> descriptors = shortcutDescriptors();
	for (const ShortcutDescriptor& descriptor : descriptors) {
		QStringList sequences;
		sequences << descriptor.defaultSequence;
		sequences << descriptor.alternateSequences;
		for (const QString& sequence : std::as_const(sequences)) {
			const QString key = normalizedSequence(sequence);
			if (key.isEmpty()) {
				continue;
			}
			bool clash = false;
			for (const ShortcutDescriptor* owner : owners.value(key)) {
				const bool separateSurfaces = owner->surfaceScoped && descriptor.surfaceScoped && owner->context != descriptor.context;
				if (!separateSurfaces) {
					found.push_back(QStringLiteral("%1 claimed by %2 and %3").arg(sequence.trimmed(), owner->commandId, descriptor.commandId));
					clash = true;
					break;
				}
			}
			if (!clash) {
				owners[key].push_back(&descriptor);
			}
		}
	}
	if (conflicts) {
		*conflicts = found;
	}
	return !found.isEmpty();
}

QVector<CommandPaletteEntry> commandPaletteEntries()
{
	QVector<CommandPaletteEntry> entries = {
		paletteEntry(QStringLiteral("texture-editor"), QStringLiteral("texture.editor"),
			QCoreApplication::translate("SlopStudioSemantics", "Texture Editor"),
			QCoreApplication::translate("SlopStudioSemantics", "Textures"),
			QCoreApplication::translate("SlopStudioSemantics", "Create or open a texture document, then export or stage it for the project.")),
		paletteEntry(QStringLiteral("texture-edit"), QStringLiteral("texture.edit"),
			QCoreApplication::translate("SlopStudioSemantics", "Edit Selected Texture"),
			QCoreApplication::translate("SlopStudioSemantics", "Textures"),
			QCoreApplication::translate("SlopStudioSemantics", "Edit the displayed mip level or sprite frame with its native export metadata.")),
		paletteEntry(QStringLiteral("texture-export"), QStringLiteral("texture.export"),
			QCoreApplication::translate("SlopStudioSemantics", "Export Texture as PNG"),
			QCoreApplication::translate("SlopStudioSemantics", "Textures"),
			QCoreApplication::translate("SlopStudioSemantics", "Export the displayed texture as a separate PNG image.")),
		paletteEntry(QStringLiteral("model-editor"), QStringLiteral("model.editor"),
			QCoreApplication::translate("SlopStudioSemantics", "Mesh Editor"),
			QCoreApplication::translate("SlopStudioSemantics", "Models"),
			QCoreApplication::translate("SlopStudioSemantics", "Edit the selected model or resume the open mesh document. With no model selected, start a new mesh.")),
		paletteEntry(QStringLiteral("model-design"), QStringLiteral("model.design"),
			QCoreApplication::translate("SlopStudioSemantics", "Prop Designer"),
			QCoreApplication::translate("SlopStudioSemantics", "Models"),
			QCoreApplication::translate("SlopStudioSemantics", "Build a prop from primitives, then export, stage or place it in a level.")),
		paletteEntry(QStringLiteral("model-export"), QStringLiteral("model.export"),
			QCoreApplication::translate("SlopStudioSemantics", "Export Model Frame"),
			QCoreApplication::translate("SlopStudioSemantics", "Models"),
			QCoreApplication::translate("SlopStudioSemantics", "Write the selected model's current frame to a Wavefront OBJ file.")),
		paletteEntry(QStringLiteral("model-assembly"), QStringLiteral("model.assembly"),
			QCoreApplication::translate("SlopStudioSemantics", "Model Assembly"),
			QCoreApplication::translate("SlopStudioSemantics", "Models"),
			QCoreApplication::translate("SlopStudioSemantics", "Link models at named tags, save the assembly or bake a pose for editing.")),
		paletteEntry(QStringLiteral("audio-open"), QStringLiteral("audio.open"),
			QCoreApplication::translate("SlopStudioSemantics", "Open Audio"),
			QCoreApplication::translate("SlopStudioSemantics", "Audio"),
			QCoreApplication::translate("SlopStudioSemantics", "Open an audio file, clip project or multitrack session for editing.")),
		paletteEntry(QStringLiteral("audio-edit"), QStringLiteral("audio.edit"),
			QCoreApplication::translate("SlopStudioSemantics", "Edit Selected Sound"),
			QCoreApplication::translate("SlopStudioSemantics", "Audio"),
			QCoreApplication::translate("SlopStudioSemantics", "Edit the selected package sound, then export or stage the result.")),
		paletteEntry(QStringLiteral("audio-session"), QStringLiteral("audio.session"),
			QCoreApplication::translate("SlopStudioSemantics", "Multitrack Session"),
			QCoreApplication::translate("SlopStudioSemantics", "Audio"),
			QCoreApplication::translate("SlopStudioSemantics", "Create or resume a multitrack audio session.")),
		paletteEntry(QStringLiteral("audio-export"), QStringLiteral("audio.export"),
			QCoreApplication::translate("SlopStudioSemantics", "Export Sound as WAV"),
			QCoreApplication::translate("SlopStudioSemantics", "Audio"),
			QCoreApplication::translate("SlopStudioSemantics", "Export the selected sound as a separate PCM WAV file.")),
		paletteEntry(QStringLiteral("texture-reveal"), QStringLiteral("texture.reveal"),
			QCoreApplication::translate("SlopStudioSemantics", "Show Selected Asset in Package"),
			QCoreApplication::translate("SlopStudioSemantics", "Textures"),
			QCoreApplication::translate("SlopStudioSemantics", "Return to the exact package entry to review its source and staged changes.")),
		paletteEntry(QStringLiteral("model-reveal"), QStringLiteral("model.reveal"),
			QCoreApplication::translate("SlopStudioSemantics", "Show Selected Asset in Package"),
			QCoreApplication::translate("SlopStudioSemantics", "Models"),
			QCoreApplication::translate("SlopStudioSemantics", "Return to the exact package entry to review its source and staged changes.")),
		paletteEntry(QStringLiteral("audio-reveal"), QStringLiteral("audio.reveal"),
			QCoreApplication::translate("SlopStudioSemantics", "Show Selected Asset in Package"),
			QCoreApplication::translate("SlopStudioSemantics", "Audio"),
			QCoreApplication::translate("SlopStudioSemantics", "Return to the exact package entry to review its source and staged changes.")),
		paletteEntry(QStringLiteral("open-workspace"), QStringLiteral("workspace.open"),
			QCoreApplication::translate("SlopStudioSemantics", "Open Workspace"),
			QCoreApplication::translate("SlopStudioSemantics", "Workspace"),
			QCoreApplication::translate("SlopStudioSemantics", "Restore saved project and module references.")),
		paletteEntry(QStringLiteral("save-workspace"), QStringLiteral("workspace.save"),
			QCoreApplication::translate("SlopStudioSemantics", "Save Workspace As"),
			QCoreApplication::translate("SlopStudioSemantics", "Workspace"),
			QCoreApplication::translate("SlopStudioSemantics", "Save portable references to the current work.")),
		paletteEntry(
			QStringLiteral("open-project"),
			QStringLiteral("project.open"),
			QCoreApplication::translate("SlopStudioSemantics", "Open Project"),
			QCoreApplication::translate("SlopStudioSemantics", "Workspace"),
			QCoreApplication::translate("SlopStudioSemantics", "Open or remember a project folder.")),
		paletteEntry(
			QStringLiteral("initialize-manifest"),
			QStringLiteral("project.initialize-manifest"),
			QCoreApplication::translate("SlopStudioSemantics", "Initialize Project Manifest"),
			QCoreApplication::translate("SlopStudioSemantics", "Workspace"),
			QCoreApplication::translate("SlopStudioSemantics", "Write a studio manifest into the selected project folder."),
			true),
		paletteEntry(
			QStringLiteral("copy-manifest"),
			QStringLiteral("project.copy-manifest"),
			QCoreApplication::translate("SlopStudioSemantics", "Copy Project Manifest"),
			QCoreApplication::translate("SlopStudioSemantics", "Workspace"),
			QCoreApplication::translate("SlopStudioSemantics", "Copy the resolved manifest for review, support, or automation."),
			true),
		paletteEntry(
			QStringLiteral("validate-project"),
			QStringLiteral("project.validate"),
			QCoreApplication::translate("SlopStudioSemantics", "Validate Project"),
			QCoreApplication::translate("SlopStudioSemantics", "Workspace"),
			QCoreApplication::translate("SlopStudioSemantics", "Run project health checks and show blocking issues."),
			true),
		paletteEntry(
			QStringLiteral("focus-workspace-search"),
			QStringLiteral("shell.focus-search"),
			QCoreApplication::translate("SlopStudioSemantics", "Find"),
			QCoreApplication::translate("SlopStudioSemantics", "Shell"),
			QCoreApplication::translate("SlopStudioSemantics", "Move focus to the current work surface's search or filter field.")),
		paletteEntry(
			QStringLiteral("command-palette"),
			QStringLiteral("shell.command-palette"),
			QCoreApplication::translate("SlopStudioSemantics", "Show Command Palette"),
			QCoreApplication::translate("SlopStudioSemantics", "Shell"),
			QCoreApplication::translate("SlopStudioSemantics", "Open the searchable command palette.")),
		paletteEntry(
			QStringLiteral("preferences"),
			QStringLiteral("app.preferences"),
			QCoreApplication::translate("SlopStudioSemantics", "Preferences"),
			QCoreApplication::translate("SlopStudioSemantics", "Shell"),
			QCoreApplication::translate("SlopStudioSemantics", "Open language, accessibility, theme, editor profile, and automation preferences.")),
		paletteEntry(
			QStringLiteral("about"),
			QStringLiteral("app.about"),
			QCoreApplication::translate("SlopStudioSemantics", "About SlopStudio"),
			QCoreApplication::translate("SlopStudioSemantics", "Support"),
			QCoreApplication::translate("SlopStudioSemantics", "Show version, platform, module, and credits information.")),
		paletteEntry(
			QStringLiteral("quit"),
			QStringLiteral("app.quit"),
			QCoreApplication::translate("SlopStudioSemantics", "Quit SlopStudio"),
			QCoreApplication::translate("SlopStudioSemantics", "Shell"),
			QCoreApplication::translate("SlopStudioSemantics", "Close the studio shell after reviewing staged work.")),
		paletteEntry(
			QStringLiteral("open-package"),
			QStringLiteral("package.open"),
			QCoreApplication::translate("SlopStudioSemantics", "Open Package"),
			QCoreApplication::translate("SlopStudioSemantics", "Packages"),
			QCoreApplication::translate("SlopStudioSemantics", "Open a PAK, WAD, ZIP, or PK3 package.")),
		paletteEntry(
			QStringLiteral("open-folder-package"),
			QStringLiteral("package.open-folder"),
			QCoreApplication::translate("SlopStudioSemantics", "Open Folder As Package"),
			QCoreApplication::translate("SlopStudioSemantics", "Packages"),
			QCoreApplication::translate("SlopStudioSemantics", "Mount a plain folder with read-only package semantics.")),
		paletteEntry(
			QStringLiteral("close-package"),
			QStringLiteral("package.close"),
			QCoreApplication::translate("SlopStudioSemantics", "Close Package"),
			QCoreApplication::translate("SlopStudioSemantics", "Packages"),
			QCoreApplication::translate("SlopStudioSemantics", "Close the open package and report staged edits that would be discarded.")),
		paletteEntry(
			QStringLiteral("save-package-as"),
			QStringLiteral("package.save-as"),
			QCoreApplication::translate("SlopStudioSemantics", "Save Staged Package As"),
			QCoreApplication::translate("SlopStudioSemantics", "Packages"),
			QCoreApplication::translate("SlopStudioSemantics", "Write staged package edits to a new package path."),
			false,
			true,
			true),
		paletteEntry(
			QStringLiteral("extract-selected"),
			QStringLiteral("package.extract-selected"),
			QCoreApplication::translate("SlopStudioSemantics", "Extract Selected Entries"),
			QCoreApplication::translate("SlopStudioSemantics", "Packages"),
			QCoreApplication::translate("SlopStudioSemantics", "Extract the selected entries into a chosen output folder.")),
		paletteEntry(
			QStringLiteral("extract-all"),
			QStringLiteral("package.extract-all"),
			QCoreApplication::translate("SlopStudioSemantics", "Extract All Entries"),
			QCoreApplication::translate("SlopStudioSemantics", "Packages"),
			QCoreApplication::translate("SlopStudioSemantics", "Extract every entry into a chosen output folder.")),
		paletteEntry(
			QStringLiteral("stage-add"),
			QStringLiteral("package.stage-add"),
			QCoreApplication::translate("SlopStudioSemantics", "Stage Add"),
			QCoreApplication::translate("SlopStudioSemantics", "Packages"),
			QCoreApplication::translate("SlopStudioSemantics", "Stage new files for the next package save-as."),
			false,
			false,
			true),
		paletteEntry(
			QStringLiteral("stage-replace"),
			QStringLiteral("package.stage-replace"),
			QCoreApplication::translate("SlopStudioSemantics", "Stage Replace"),
			QCoreApplication::translate("SlopStudioSemantics", "Packages"),
			QCoreApplication::translate("SlopStudioSemantics", "Stage a replacement for the selected entry."),
			false,
			false,
			true),
		paletteEntry(
			QStringLiteral("stage-rename"),
			QStringLiteral("package.stage-rename"),
			QCoreApplication::translate("SlopStudioSemantics", "Stage Rename"),
			QCoreApplication::translate("SlopStudioSemantics", "Packages"),
			QCoreApplication::translate("SlopStudioSemantics", "Stage a new archive path for the selected entry."),
			false,
			false,
			true),
		paletteEntry(
			QStringLiteral("stage-delete"),
			QStringLiteral("package.stage-delete"),
			QCoreApplication::translate("SlopStudioSemantics", "Stage Delete"),
			QCoreApplication::translate("SlopStudioSemantics", "Packages"),
			QCoreApplication::translate("SlopStudioSemantics", "Stage the selected entry for removal from the next saved package."),
			false,
			true,
			true),
		paletteEntry(
			QStringLiteral("open-map"),
			QStringLiteral("map.open"),
			QCoreApplication::translate("SlopStudioSemantics", "Open Map"),
			QCoreApplication::translate("SlopStudioSemantics", "Maps"),
			QCoreApplication::translate("SlopStudioSemantics", "Open a .map file or a Doom map lump.")),
		paletteEntry(
			QStringLiteral("save-map-as"),
			QStringLiteral("map.save-as"),
			QCoreApplication::translate("SlopStudioSemantics", "Save Map As"),
			QCoreApplication::translate("SlopStudioSemantics", "Maps"),
			QCoreApplication::translate("SlopStudioSemantics", "Write the edited level document to a new path."),
			false,
			true,
			true),
		paletteEntry(
			QStringLiteral("run-compiler"),
			QStringLiteral("compiler.run"),
			QCoreApplication::translate("SlopStudioSemantics", "Run Selected Compiler Profile"),
			QCoreApplication::translate("SlopStudioSemantics", "Compilers"),
			QCoreApplication::translate("SlopStudioSemantics", "Run the selected compiler profile with task-state feedback."),
			true),
		paletteEntry(
			QStringLiteral("copy-cli-equivalent"),
			QStringLiteral("compiler.copy-cli"),
			QCoreApplication::translate("SlopStudioSemantics", "Copy CLI Equivalent"),
			QCoreApplication::translate("SlopStudioSemantics", "Compilers"),
			QCoreApplication::translate("SlopStudioSemantics", "Copy a reproducible command line for the selected GUI action."),
			true),
		paletteEntry(
			QStringLiteral("run-build-pipeline"),
			QStringLiteral("build.run-pipeline"),
			QCoreApplication::translate("SlopStudioSemantics", "Run Build Pipeline"),
			QCoreApplication::translate("SlopStudioSemantics", "Build"),
			QCoreApplication::translate("SlopStudioSemantics", "Run the ordered compile, validate, and package pipeline."),
			true),
		paletteEntry(
			QStringLiteral("detect-installations"),
			QStringLiteral("game.detect-installations"),
			QCoreApplication::translate("SlopStudioSemantics", "Detect Game Installations"),
			QCoreApplication::translate("SlopStudioSemantics", "Game"),
			QCoreApplication::translate("SlopStudioSemantics", "Scan for candidate installations and confirm them before saving.")),
		paletteEntry(
			QStringLiteral("launch-game"),
			QStringLiteral("game.launch"),
			QCoreApplication::translate("SlopStudioSemantics", "Launch Game"),
			QCoreApplication::translate("SlopStudioSemantics", "Game"),
			QCoreApplication::translate("SlopStudioSemantics", "Launch the selected installation with the current mod and map arguments.")),
		// NetRadiant Custom's Run Recent Build (F5) and TrenchBroom's Re-run
		// Compilation (app/build_rerun_actions.cpp).
		paletteEntry(
			QStringLiteral("build-run-last"),
			QStringLiteral("build.run-last"),
			QCoreApplication::translate("SlopStudioSemantics", "Run Last Build"),
			QCoreApplication::translate("SlopStudioSemantics", "Build"),
			QCoreApplication::translate("SlopStudioSemantics", "Run again the build you ran last for this project on the open map: the build pipeline, with its launch, or the Levels compiler profile.")),
		paletteEntry(
			QStringLiteral("compiler-rerun-last"),
			QStringLiteral("compiler.rerun-last"),
			QCoreApplication::translate("SlopStudioSemantics", "Re-run Last Compile"),
			QCoreApplication::translate("SlopStudioSemantics", "Compilers"),
			QCoreApplication::translate("SlopStudioSemantics", "Replay the open map's newest saved compiler run exactly as it ran, with the same program, arguments and folder.")),
		paletteEntry(
			QStringLiteral("game-edit-launch-profiles"),
			QStringLiteral("game.edit-launch-profiles"),
			QCoreApplication::translate("SlopStudioSemantics", "Launch Profiles"),
			QCoreApplication::translate("SlopStudioSemantics", "Game"),
			QCoreApplication::translate("SlopStudioSemantics", "Make your own launch profiles: a name and the engine arguments, with {map}, {mod}, {basedir} and {bsp} filled in at launch.")),
		// NetRadiant Custom's Build > Customize… (app/build_pipeline_actions.cpp).
		paletteEntry(
			QStringLiteral("build-edit-pipelines"),
			QStringLiteral("build.edit-pipelines"),
			QCoreApplication::translate("SlopStudioSemantics", "Build Pipelines"),
			QCoreApplication::translate("SlopStudioSemantics", "Build"),
			QCoreApplication::translate("SlopStudioSemantics", "Make the project's own build pipelines: compiler stages with your arguments, on/off switches and variables, kept in the project.")),
		paletteEntry(
			QStringLiteral("cancel-task"),
			QStringLiteral("activity.cancel"),
			QCoreApplication::translate("SlopStudioSemantics", "Cancel Selected Task"),
			QCoreApplication::translate("SlopStudioSemantics", "Activity"),
			QCoreApplication::translate("SlopStudioSemantics", "Cancel a cancellable task and report its cleanup state.")),
		paletteEntry(
			QStringLiteral("localization-report"),
			QStringLiteral("localization.report"),
			QCoreApplication::translate("SlopStudioSemantics", "Localization Report"),
			QCoreApplication::translate("SlopStudioSemantics", "QA"),
			QCoreApplication::translate("SlopStudioSemantics", "Inspect targets, RTL coverage, formatting, expansion, and catalog status.")),
		paletteEntry(
			QStringLiteral("diagnostics-bundle"),
			QStringLiteral("diagnostics.bundle"),
			QCoreApplication::translate("SlopStudioSemantics", "Copy Diagnostic Bundle"),
			QCoreApplication::translate("SlopStudioSemantics", "Support"),
			QCoreApplication::translate("SlopStudioSemantics", "Copy redacted version, platform, command, module, and localization diagnostics.")),
		paletteEntry(
			QStringLiteral("ai-review"),
			QStringLiteral("ai.review"),
			QCoreApplication::translate("SlopStudioSemantics", "Review AI Proposal"),
			QCoreApplication::translate("SlopStudioSemantics", "AI"),
			QCoreApplication::translate("SlopStudioSemantics", "Open a generated proposal and inspect staged actions before applying anything."),
			true,
			false,
			true),
		paletteEntry(
			QStringLiteral("ai-assistant"),
			QStringLiteral("ai.assistant"),
			QCoreApplication::translate("SlopStudioSemantics", "Assistant"),
			QCoreApplication::translate("SlopStudioSemantics", "AI"),
			QCoreApplication::translate("SlopStudioSemantics", "Ask a model about the open map, code, build, or project, with the context you choose; nothing is sent until you press Send.")),
		paletteEntry(
			QStringLiteral("ai-explain-build"),
			QStringLiteral("ai.explain-build"),
			QCoreApplication::translate("SlopStudioSemantics", "Explain Build Problems"),
			QCoreApplication::translate("SlopStudioSemantics", "AI"),
			QCoreApplication::translate("SlopStudioSemantics", "Open the Assistant with the last build's problems ticked and a question proposed.")),
		paletteEntry(
			QStringLiteral("ai-ask-about-code"),
			QStringLiteral("ai.ask-about-code"),
			QCoreApplication::translate("SlopStudioSemantics", "Ask About This Code"),
			QCoreApplication::translate("SlopStudioSemantics", "AI"),
			QCoreApplication::translate("SlopStudioSemantics", "Open the Assistant with the selected code, or the open file, ticked and a question proposed.")),
	};

	// Level editor commands the editor profiles give keys to (see
	// core/level_editor_controls.h). The SlopStudio profile leaves them
	// without one, so they are listed here rather than among the shortcuts,
	// and the shell keeps whatever keys they get to the Levels page.
	static const struct {
		const char* id;
		const char* commandId;
		const char* label;
		const char* summary;
	} kLevelCommands[] = {
		{"map-cycle-view", "map.cycle-view", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Cycle Map View"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Step one view through 3D, Top, Front, and Side; beside the camera, step the 2D view through its planes.")},
		{"map-next-projection", "map.next-projection", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Next 2D View"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Step the 2D view from Top to Front to Side.")},
		{"map-save-view", "map.save-view", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Save Current Level View"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Name and save the level camera, plan views and layout.")},
		{"map-paint-material", "map.paint-material", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Paint Map Materials"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Paint individual map surfaces in the camera with one undo step per stroke.")},
		{"map-draw-brush", "map.draw-brush", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Draw Brush in Camera"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Drag a brush footprint on a construction plane; adjust depth with the wheel.")},
		{"map-place-at-camera", "map.place-at-camera", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Place at Camera Surface"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Place the selected Create class at the surface under the camera centre.")},
		{"map-sample-material", "map.sample-material", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Sample Map Material"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Choose the material of a camera surface without changing the map.")},
		{"map-manage-views", "map.manage-views", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Saved Level Views"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Navigate, update, rename, remove, import or export saved views.")},
		{"map-next-saved-view", "map.next-saved-view", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Next Saved Level View"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Restore the next saved view for this map.")},
		{"map-previous-saved-view", "map.previous-saved-view", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Previous Saved Level View"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Restore the previous saved view for this map.")},
		{"map-view-top", "map.view-top", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Top View"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show the map from above in the 2D view.")},
		{"map-view-front", "map.view-front", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Front View"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show the map from the front in the 2D view.")},
		{"map-view-side", "map.view-side", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Side View"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show the map from the side in the 2D view.")},
		// NetRadiant Custom's Zoom100 and TrenchBroom's Reset 2D Cameras
		// (app/level_plan_framing_actions.cpp).
		{"map-zoom-actual-size", "map.zoom-actual-size", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Zoom 100%"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show every 2D view at one screen pixel per map unit, each about its own centre.")},
		{"map-reset-plan-views", "map.reset-plan-views", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Reset 2D Views"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Give the other 2D views the focused one's zoom and its position on the axes they share, once, without linking them.")},
		{"map-show-top-view", "map.show-top-view", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show Top View"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show or hide the Top plan pane in the three-plan layouts; one plan pane always stays visible.")},
		{"map-show-front-view", "map.show-front-view", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show Front View"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show or hide the Front plan pane in the three-plan layouts; one plan pane always stays visible.")},
		{"map-show-side-view", "map.show-side-view", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show Side View"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show or hide the Side plan pane in the three-plan layouts; one plan pane always stays visible.")},
		{"map-frame-selection", "map.frame-selection", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Frame Selection"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Frame the selection in the 2D view and turn the 3D camera to it; with Radiant framing (the NetRadiant Custom and NetRadiant profiles) only the 2D views move.")},
		{"map-level-camera", "map.level-camera", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Level Camera"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Level the 3D camera where it stands: no pitch, and the heading turned to the nearest 22.5 degrees.")},
		{"map-focus-camera-on-selection", "map.focus-camera-on-selection", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Focus Camera on Selection"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Move only the 3D camera along its view until the selection fits, at least 64 units away; with nothing selected, the last selection.")},
		{"map-reset-camera-zoom", "map.reset-camera-zoom", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Reset Camera Zoom"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Return the 3D camera's field of view to the editor profile's, undoing field-of-view zoom.")},
		// NetRadiant Custom's UpFloor and DownFloor (core/level_camera_focus.h).
		{"map-camera-floor-up", "map.camera-floor-up", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Camera Floor Up"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Lift the 3D camera to the next floor above it: 32 units over the next top under the camera or selected, or eye height over a Doom sector floor.")},
		{"map-camera-floor-down", "map.camera-floor-down", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Camera Floor Down"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Drop the 3D camera to the next floor below it: 32 units over the next lower top under the camera or selected, or eye height over a Doom sector floor.")},
		// NetRadiant Custom's CameraSpeedInc and CameraSpeedDec (core/level_camera_keys.h).
		{"map-camera-speed-up", "map.camera-speed-up", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Increase Camera Speed"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Make the 3D camera's held keys fly and drive one step faster, up to 9999 units per second; the speed is kept with the editor profile's gestures.")},
		{"map-camera-speed-down", "map.camera-speed-down", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Decrease Camera Speed"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Make the 3D camera's held keys fly and drive one step slower, down to 50 units per second; the speed is kept with the editor profile's gestures.")},
		// View > Camera Display (core/level_camera_render.h): TrenchBroom's
		// face modes and edges, NetRadiant Custom's camera mode cycle.
		{"map-preview-textures", "map.preview-textures", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Textures in Camera"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Draw the camera's faces with package images, or untick for flat-shaded colours; the choice is kept.")},
		{"map-preview-edges", "map.preview-edges", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Edges in Camera"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Outline every brush face and patch quad over the camera's faces, without triangle diagonals; the choice is kept.")},
		{"map-show-camera-textures", "map.show-camera-textures", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show Textures in Camera"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Draw the camera's faces textured, turning 3D Wireframe off.")},
		{"map-hide-camera-textures", "map.hide-camera-textures", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Hide Textures in Camera"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Draw the camera's faces in flat-shaded colours, turning 3D Wireframe off.")},
		{"map-hide-camera-faces", "map.hide-camera-faces", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Hide Faces in Camera"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Draw no faces in the camera, only the brush and patch edges.")},
		{"map-next-camera-mode", "map.next-camera-mode", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Next Camera Mode"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Step the camera through Wireframe, Flat Shaded, Textured and Textured with Edges, wrapping round.")},
		{"map-previous-camera-mode", "map.previous-camera-mode", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Previous Camera Mode"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Step the camera back through Textured with Edges, Textured, Flat Shaded and Wireframe, wrapping round.")},
		// Animated materials on the camera's faces (app/level_material_animator.h).
		{"map-animate-materials", "map.animate-materials", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Animate Materials in Camera"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Play scrolling, pulsing and animated materials on the camera's faces, drawn as the Materials page draws them; reduce motion keeps them still, and the choice is kept.")},
		// TrenchBroom's Shade Faces and Use Fog, and its grid on brush faces.
		{"map-preview-shading", "map.preview-shading", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Shade Faces in Camera"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Darken the camera's faces by the way they face, or untick for full brightness; the choice is kept.")},
		{"map-preview-fog", "map.preview-fog", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Fog in Camera"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Fade the camera's faces towards the background with distance, between the View tab's fog start and end; the choice is kept.")},
		{"map-preview-face-grid", "map.preview-face-grid", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Grid on Camera Faces"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Draw the grid at the current grid size on brush faces in the camera while Show Grid is on; the choice is kept.")},
		// NetRadiant Custom's cubic clip and its CubicClipZoomIn and CubicClipZoomOut.
		{"map-preview-far-clip", "map.preview-far-clip", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Far Clip in Camera"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Draw nothing beyond the camera's draw distance (cubic clip), to speed up and declutter dense maps; the choice is kept.")},
		{"map-shorter-draw-distance", "map.shorter-draw-distance", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Shorter Draw Distance"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Bring the camera's far clip (cubic clip) one step nearer; the distance is kept.")},
		{"map-longer-draw-distance", "map.longer-draw-distance", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Longer Draw Distance"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Push the camera's far clip (cubic clip) one step farther; the distance is kept.")},
		{"map-toggle-grid", "map.toggle-grid", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show Grid"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show or hide the grid lines in the 2D views, and the camera's ground grid and face grid.")},
		{"map-toggle-snap", "map.toggle-snap", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Snap to Grid"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Turn grid snapping for drags, nudges, and drawn brushes on or off.")},
		{"map-toggle-grid-coordinates", "map.toggle-grid-coordinates", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show Grid Coordinates"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Label the grid lines with their coordinates along the bottom and leading edges of the 2D views; the choice is kept.")},
		{"map-toggle-block-grid", "map.toggle-block-grid", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show Block Grid"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Draw dashed lines in the 2D views where the compiler splits the world into blocks, from worldspawn _blocksize; the choice is kept.")},
		{"map-grid-0.125", "map.grid0.125", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Grid 0.125"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Set the Levels grid to 0.125 units, the finest. Doom and Hexen maps stop at 1 unit.")},
		{"map-grid-0.25", "map.grid0.25", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Grid 0.25"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Set the Levels grid to 0.25 units. Doom and Hexen maps stop at 1 unit.")},
		{"map-grid-0.5", "map.grid0.5", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Grid 0.5"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Set the Levels grid to 0.5 units. Doom and Hexen maps stop at 1 unit.")},
		{"map-grid-1", "map.grid1", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Grid 1"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Set the Levels grid to 1 unit.")},
		{"map-grid-2", "map.grid2", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Grid 2"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Set the Levels grid to 2 units.")},
		{"map-grid-4", "map.grid4", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Grid 4"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Set the Levels grid to 4 units.")},
		{"map-grid-8", "map.grid8", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Grid 8"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Set the Levels grid to 8 units.")},
		{"map-grid-16", "map.grid16", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Grid 16"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Set the Levels grid to 16 units.")},
		{"map-grid-32", "map.grid32", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Grid 32"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Set the Levels grid to 32 units.")},
		{"map-grid-64", "map.grid64", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Grid 64"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Set the Levels grid to 64 units.")},
		{"map-grid-128", "map.grid128", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Grid 128"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Set the Levels grid to 128 units.")},
		{"map-grid-256", "map.grid256", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Grid 256"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Set the Levels grid to 256 units.")},
		{"map-grid-512", "map.grid512", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Grid 512"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Set the Levels grid to 512 units.")},
		{"map-grid-1024", "map.grid1024", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Grid 1024"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Set the Levels grid to 1024 units, the largest.")},
		{"map-isolate-selection", "map.isolate-selection", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Isolate Selection"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Hide everything but the selection; Show All Hidden brings it back.")},
		{"map-carve", "map.carve", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Carve"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Cut the selected brushes out of the brushes they overlap (CSG subtract).")},
		{"map-merge-brushes", "map.merge-brushes", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Merge Brushes"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Preview a convex union and resolve conflicting brush materials, UVs and flags.")},
		{"map-wrap-merge", "map.wrap-merge", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Wrap/Merge"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Fill a convex hull from selected brushes or add a hull around picked brush and patch components.")},
		{"map-sweep-faces", "map.sweep-faces", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Sweep Faces"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Preview swept face profiles with arc, straight or S-bend paths, taper and repeated sections.")},
		{"map-sweep-tool", "map.sweep-tool", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Sweep Tool"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Move, rotate and scale a destination cap in the level views before creating the sweep.")},
		{"map-select-inside", "map.select-inside", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Select Inside"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Select inside profile-specific volumes. TB and NRC remove the source selection in one undo step.")},
		{"map-select-connected-entities", "map.select-connected-entities", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Select Connected Entities"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Add direct incoming and outgoing target connections to the current selection, respecting visibility and groups.")},
		{"map-select-all-of-type", "map.select-all-of-type", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Select All of Type"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Select all selected entity classes, or use the current material for brushes, patches or picked-brush faces.")},
		{"map-select-textured", "map.select-textured", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Select Textured"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Add independent faces and whole patches using the current material, preserving object and component picks.")},
		{"map-select-siblings", "map.select-siblings", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Select Siblings"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Select objects sharing a parent, respecting groups, layers, visibility and locks.")},
		{"map-select-source-lines", "map.select-source-lines", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Select by Source Line Number"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Select objects by line numbers in the map's source text at last open.")},
		{"map-expand-selection-to-layers", "map.expand-selection-to-layers", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Expand Selection to Layers"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Add the visible, unlocked objects of the selected objects' layers and their nested groups to the selection.")},
		{"map-move-to-layer", "map.move-to-layer", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Move to Layer"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Choose a layer at the pointer and move the selection into it as one undo step; closed groups move whole.")},
		{"map-select-touching", "map.select-touching", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Select Touching"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Select touching profile-specific volumes. TB removes source brushes; NRC adds matches and retains the sources.")},
		{"map-select-complete-tall", "map.select-complete-tall", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Select Complete Tall"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Select objects inside projected volumes in the active 2D view. TB and NRC remove the source selection.")},
		{"map-select-partial-tall", "map.select-partial-tall", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Select Partial Tall"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Select objects overlapping projected volumes. TB removes source brushes; NRC retains the sources.")},
		{"map-sweep-apply", "map.sweep-apply", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Create Sweep"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Create the checked sweep as one undo step and retain the source faces.")},
		{"map-sweep-decrease-scale", "map.sweep-decrease-scale", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Decrease Sweep Scale"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Move the sweep scale handle inward by one grid step.")},
		{"map-sweep-increase-scale", "map.sweep-increase-scale", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Increase Sweep Scale"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Move the sweep scale handle outward by one grid step.")},
		{"map-add-protected-property", "map.add-protected-property", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Add Protected Property"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Set an entity key locally while preserving the corresponding values in linked groups.")},
		{"map-clear-protected-properties", "map.clear-protected-properties", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Clear Protected Properties"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Clear selected linked entities' protection and restore available unprotected values.")},
		{"map-extract-linked-groups", "map.extract-linked-groups", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Extract Linked Groups"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Move selected contents of the open linked group and their counterparts into a new set of linked groups.")},
		{"map-tie-to-entity", "map.tie-to-entity", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Make Brush Entity"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Make the selected brushes and patches into a new brush entity of a class you choose.")},
		{"map-create-entity", "map.create-entity", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Create Entity"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Type a class of the loaded definitions: a point class goes in at the pointer, a brush class takes the selected brushes. Each class also has its own Create command for keys.")},
		{"map-move-to-world", "map.move-to-world", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Move to World"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Give every brush of the selected brush entities back to worldspawn; the entities and their keys go.")},
		{"map-move-primitives-to-entity", "map.move-primitives-to-entity", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Move Primitives to Entity"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Move the selected brushes and patches into the entity of the item selected last, keeping its keys.")},
		{"map-move-primitives-to-first-entity", "map.move-primitives-to-first-entity", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Move Primitives to First Entity"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Move the selected brushes and patches into the entity of the item selected first, keeping its keys.")},
		{"map-move-selected-to-world", "map.move-selected-to-world", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Move Selected to World"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Move only the selected brushes and patches to worldspawn; their entities keep the rest and their keys.")},
		{"map-make-structural", "map.make-structural", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Make Structural"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Clear detail on the selected brushes. In the TrenchBroom profile they also leave brush entities for the world or current group.")},
		// NetRadiant Custom's Brush > Prism, Cone and Sphere and its Make 3- to
		// 9-Sided keys (app/level_prism_actions.cpp, core/level_shapes.h).
		{"map-make-prism", "map.make-prism", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Make Prism…"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Rebuild each selected brush in place as a prism with the sides you choose, along the active view's depth axis, round on its larger extent, in the current material.")},
		{"map-make-cone", "map.make-cone", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Make Cone…"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Rebuild each selected brush in place as a cone with the sides you choose, pointing up the active view's depth axis, in the current material.")},
		{"map-make-sphere", "map.make-sphere", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Make Sphere…"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Rebuild each selected brush in place as a faceted sphere on its largest extent, poles on Z, in the current material.")},
		{"map-make-3-sided-prism", "map.make3-sided-prism", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Make 3-Sided Prism"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Rebuild each selected brush in place as a three-sided prism along the active view's depth axis, in the current material.")},
		{"map-make-4-sided-prism", "map.make4-sided-prism", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Make 4-Sided Prism"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Rebuild each selected brush in place as a square prism on its larger extent along the active view's depth axis, in the current material.")},
		{"map-make-5-sided-prism", "map.make5-sided-prism", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Make 5-Sided Prism"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Rebuild each selected brush in place as a five-sided prism along the active view's depth axis, in the current material.")},
		{"map-make-6-sided-prism", "map.make6-sided-prism", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Make 6-Sided Prism"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Rebuild each selected brush in place as a six-sided prism along the active view's depth axis, in the current material.")},
		{"map-make-7-sided-prism", "map.make7-sided-prism", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Make 7-Sided Prism"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Rebuild each selected brush in place as a seven-sided prism along the active view's depth axis, in the current material.")},
		{"map-make-8-sided-prism", "map.make8-sided-prism", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Make 8-Sided Prism"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Rebuild each selected brush in place as an eight-sided prism along the active view's depth axis, in the current material.")},
		{"map-make-9-sided-prism", "map.make9-sided-prism", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Make 9-Sided Prism"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Rebuild each selected brush in place as a nine-sided prism along the active view's depth axis, in the current material.")},
		// TrenchBroom's Create Brush and Print Vertices
		// (app/level_brush_points_actions.cpp, core/level_brush_points.h).
		{"map-create-brush-from-points", "map.create-brush-from-points", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Create Brush from Points…"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Type four or more points and add the convex brush that encloses them, in the current material, as one undo step.")},
		// TrenchBroom's Brush Tool and its Create Brush (Return)
		// (app/level_brush_assemble_actions.cpp, core/level_brush_assemble.h).
		{"map-assemble-brush-tool", "map.assemble-brush-tool", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Assemble Brush Tool"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Click surfaces or the grid to collect points, drag faces, Shift-drag a flat outline to extrude it, and press Enter for their convex brush.")},
		{"map-create-assembled-brush", "map.create-assembled-brush", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Create Assembled Brush"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Add the convex brush enclosing the Assemble Brush tool's points, in the current material, as one undo step.")},
		{"map-copy-vertex-coordinates", "map.copy-vertex-coordinates", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Copy Vertex Coordinates"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Copy the corners of the picked faces, edges and vertices, or else of the selected brushes, at full precision.")},
		{"map-explode-entity", "map.explode-entity", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Explode Entity"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Give every brush and patch of the selected brush entities its own entity with identical keys.")},
		{"map-explode-entity-unique", "map.explode-entity-unique", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Explode Entity, Unique Names"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Explode the selected brush entities, giving each new piece its own targetname.")},
		{"map-set-entity-colour", "map.set-entity-colour", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Set Entity Colour"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Pick a colour for the _color key of the selected entities and the entities holding the selected brushes.")},
		{"map-normalise-entity-colour", "map.normalise-entity-colour", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Normalise Entity Colour"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Scale the last-selected entity's _color so its brightest channel is 1 and set it on the selected entities.")},
		{"map-repeat", "map.repeat", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Repeat Last Commands"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Replay the recorded object commands on the current selection with their original world pivots.")},
		{"map-paste-at-original", "map.paste-at-original", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Paste at Original Position"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Paste clipboard objects at their saved coordinates.")},
		{"map-paste-at-cursor", "map.paste-at-cursor", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Paste at Cursor"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Place clipboard objects against the active view's cursor plane or brush surface.")},
		{"map-paste-to-camera", "map.paste-to-camera", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Paste to Camera"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Centre the pasted assembly on the camera or orbit focus using a snapped translation.")},
		{"map-clear-repeat", "map.clear-repeat", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Clear Repeatable Commands"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Forget the recorded sequence without changing map contents or undo history.")},
		{"map-copy-repeat-recipe", "map.copy-repeat-recipe", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Copy Repeat Recipe"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Copy the recorded world transforms as JSON for the map repeat CLI command.")},
		{"map-wrap-merge-points", "map.wrap-merge-points", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Wrap/Merge with Points"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Preview a convex hull with up to three extra XYZ points and compare it with its source objects.")},
		{"map-align-surfaces", "map.align-surfaces", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Surface Alignment"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Shift, scale, rotate, fit and align selected brush faces using package material dimensions.")},
		{"map-uv-editor", "map.uv-editor", QT_TRANSLATE_NOOP("SlopStudioSemantics", "UV Editor"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Drag the picked face's or selected patch's points over its material image: move the texture, turn it about an anchor, zoom, pan and nudge, with a live preview, as one undo step.")},
		{"map-copy-surface", "map.copySurface", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Copy Surface"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Copy one brush face's material, mapping and flags into the surface clipboard.")},
		{"map-paste-surface", "map.pasteSurface", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Paste Surface"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Paste a copied surface onto the explicit Surfaces target as one undoable operation.")},
		{"map-surface-shift-left", "map.surfaceShiftLeft", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Shift Texture U −"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Shift the surface target by the negative U texel step.")},
		{"map-surface-shift-right", "map.surfaceShiftRight", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Shift Texture U +"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Shift the surface target by the positive U texel step.")},
		{"map-surface-shift-down", "map.surfaceShiftDown", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Shift Texture V −"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Shift the surface target by the negative V texel step.")},
		{"map-surface-shift-up", "map.surfaceShiftUp", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Shift Texture V +"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Shift the surface target by the positive V texel step.")},
		{"map-surface-rotate-left", "map.surfaceRotateLeft", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Rotate Texture −"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Rotate surface mapping by the negative angle step, about each face centre or the texture origin as Rotate about chooses.")},
		{"map-surface-rotate-right", "map.surfaceRotateRight", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Rotate Texture +"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Rotate surface mapping by the positive angle step, about each face centre or the texture origin as Rotate about chooses.")},
		{"map-surface-scale-step-left", "map.surfaceScaleStepLeft", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Texture Scale U −"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Subtract the Radiant scale step from each targeted face's U texture scale about the texture origin; a scale reaching zero flips sign.")},
		{"map-surface-scale-step-right", "map.surfaceScaleStepRight", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Texture Scale U +"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Add the Radiant scale step to each targeted face's U texture scale about the texture origin; a scale reaching zero flips sign.")},
		{"map-surface-scale-step-down", "map.surfaceScaleStepDown", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Texture Scale V −"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Subtract the Radiant scale step from each targeted face's V texture scale about the texture origin; a scale reaching zero flips sign.")},
		{"map-surface-scale-step-up", "map.surfaceScaleStepUp", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Texture Scale V +"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Add the Radiant scale step to each targeted face's V texture scale about the texture origin; a scale reaching zero flips sign.")},
		{"map-surface-shrink-u", "map.surfaceShrinkU", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Shrink Texture U"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Divide texture width by the surface size factor, keeping each face centre fixed.")},
		{"map-surface-grow-u", "map.surfaceGrowU", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Grow Texture U"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Multiply texture width by the surface size factor, keeping each face centre fixed.")},
		{"map-surface-shrink-v", "map.surfaceShrinkV", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Shrink Texture V"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Divide texture height by the surface size factor, keeping each face centre fixed.")},
		{"map-surface-grow-v", "map.surfaceGrowV", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Grow Texture V"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Multiply texture height by the surface size factor, keeping each face centre fixed.")},
		{"map-surface-fit", "map.surfaceFit", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Fit Texture 1 × 1"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Fit one texture repeat across each targeted brush face.")},
		{"map-surface-center", "map.surfaceCenter", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Centre Texture"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Centre texture mapping on each targeted brush face.")},
		{"map-edit-patch", "map.edit-patch", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Edit Patch Control Points"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Reshape, subdivide and texture the selected patch.")},
		{"map-cap-patch", "map.cap-patch", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Cap Patch"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Close the boundaries of the selected patches with radial caps or NetRadiant Custom's bevel, endcap and cylinder seams, and review their material mapping.")},
		{"map-convert-to-patches", "map.convert-to-patches", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Convert Selection to Patches"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Turn the selected brushes, or the picked faces, of a Quake III map into flat patches with their materials and texture mapping, then edit their control points, as TrenchBroom does.")},
		{"map-hollow", "map.hollow-selection", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Hollow"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Turn the selected brushes into rooms with walls one grid step thick.")},
		{"map-select-similar", "map.select-similar", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Select All of This Class"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Select every entity of the selected entity's class, or every thing of the selected thing's type.")},
		{"map-load-leak-trail", "map.load-leak-trail", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Load Leak Trail"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Draw a compiler leak point file (.pts or .lin) over the open map.")},
		{"map-zoom-in", "map.zoom-in", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Zoom In"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Zoom the 2D view in about its centre.")},
		{"map-zoom-out", "map.zoom-out", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Zoom Out"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Zoom the 2D view out about its centre.")},
		{"map-toggle-3d", "map.toggle3D", QT_TRANSLATE_NOOP("SlopStudioSemantics", "3D View"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show or hide the 3D view of the open map.")},
		{"map-maximize-view", "map.maximizeView", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Maximize or Restore Active View"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Temporarily enlarge the focused level pane, preserving the workspace for restoration.")},
		{"editor-gestures", "editor.gestures", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Customize Editor Gestures"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Customize pointer gestures, camera movement keys and mouse-look keys for the current editor profile.")},
		{"map-equalize-views", "map.equalizeViews", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Equalize View Sizes"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Restore the workspace and equalize its visible pane sizes.")},
		{"map-snap-selection", "map.snap-to-grid", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Snap Selection to Grid"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Move the selected objects onto the grid.")},
		{"map-flip-horizontal", "map.flip-horizontal", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Flip Horizontal"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Mirror the selected map objects horizontally in the active view, or along the camera's right axis.")},
		{"map-flip-vertical", "map.flip-vertical", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Flip Vertical"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Mirror the selected map objects vertically in the active view, or along world Z from the camera.")},
		{"map-rotate-left", "map.rotate-left", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Rotate 90 Degrees Left"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Turn the selection a quarter turn counter-clockwise in the active view, or about world Z from the camera (Yaw).")},
		{"map-rotate-right", "map.rotate-right", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Rotate 90 Degrees Right"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Turn the selection a quarter turn clockwise in the active view, or about world Z from the camera (Yaw).")},
		{"map-texture-lock", "map.texture-lock", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Texture Lock"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Keep map surface textures fixed during movement and rotation.")},
		{"map-connect-entities", "map.connect-entities", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Connect Entities"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Connect to the entity picked last. NRC requires two objects and prefers the source's existing target value.")},
		{"map-kill-connect-entities", "map.kill-connect-entities", QT_TRANSLATE_NOOP("SlopStudioSemantics", "KillConnect Entities"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Set killtarget from the first picked entity to the second using NRC naming rules. Requires Quake, Nexuiz or Xonotic.")},
	};
	for (const auto& command : kLevelCommands) {
		entries.push_back(paletteEntry(QString::fromLatin1(command.id), QString::fromLatin1(command.commandId), QCoreApplication::translate("SlopStudioSemantics", command.label),
			QCoreApplication::translate("SlopStudioSemantics", "Levels"), QCoreApplication::translate("SlopStudioSemantics", command.summary)));
	}

	// Every other command the Levels page registers, so the CLI's
	// `ui semantics`, parity reviews, editor profiles and Keyboard settings
	// know each of them by the same id. The kebab-case id is the shell's
	// camelCase action id with hyphens (map.vertexTool is map.vertex-tool);
	// the palette id swaps the dots for hyphens as well. Labels drop the menu
	// ellipsis and mnemonic. level_palette_coverage_ui_smoke_test checks that
	// no registered map.* action is missing here.
	static const struct {
		const char* commandId;
		const char* label;
		const char* summary;
	} kLevelActions[] = {
		// Map files and definitions.
		{"map.close", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Close Map"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Close the open map, asking about unsaved edits first.")},
		{"map.import-map", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Import Map"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Add another .map's brushes, patches and entities at their own coordinates as one undo step, renaming clashing entity names; a .bsp is converted with SlopMap3 first.")},
		{"map.export-selection-as-map", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Export Selection as Map"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Write the selection to a .map of its own, inside a worldspawn with or without the map's keys.")},
		{"map.export-map", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Export Map"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Write a copy of the map to another file, leaving out layers marked Omit from export; the open map keeps its own file.")},
		{"map.export-map-as-obj", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Export Map as OBJ"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Write the map's brushes and patches as a Wavefront OBJ with a material library, for Blender and other modellers.")},
		{"map.export-selection-as-obj", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Export Selection as OBJ"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Write the selected brushes and patches as a Wavefront OBJ, grouped by object or material, leaving out tool materials.")},
		{"map.recover", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Recover Maps"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Review local recovery checkpoints and the automatic recovery settings.")},
		{"map.export-image", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Export Map Image"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Write a deterministic SVG picture of the current map.")},
		{"entity.definitions", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Load Entity Definitions"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Choose a .def, .fgd or .ent file to validate map entities against.")},
		// Creating objects, prefabs and generated levels.
		{"map.add-entity", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Add Entity"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Add a point entity to the open Quake-family map at the centre of the view.")},
		{"map.add-brush", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Add Brush"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Preview and add a box, wedge, cylinder, cone or sphere brush.")},
		{"map.add-patch", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Add Patch"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Create a Quake III curved surface with editable control points and UV coordinates.")},
		// NetRadiant Custom's Patch menu prefabs, each replacing the selection.
		{"map.patch-cylinder", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Patch Cylinder"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Replace the selection with Radiant's nine-by-three patch cylinder, filling its bounds along the view's axis, mapped Natural.")},
		{"map.patch-square-cylinder", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Square Patch Cylinder"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Replace the selection with a four-sided nine-by-three patch tube, filling its bounds along the view's axis, mapped Natural.")},
		{"map.patch-end-cap", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Patch End Cap"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Replace the selection with a five-by-three half-round arch patch, filling its bounds along the view's axis, mapped Natural.")},
		{"map.patch-bevel", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Patch Bevel"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Replace the selection with a three-by-three quarter-round patch, corner to corner of its bounds along the view's axis, mapped Natural.")},
		{"map.patch-classic-cone", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Classic Patch Cone"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Replace the selection with Radiant's nine-by-three bulging patch cone, filling its bounds along the view's axis, mapped Natural.")},
		{"map.patch-sphere", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Patch Sphere"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Replace the selection with Radiant's nine-by-five patch sphere, filling its bounds along the view's axis, mapped Natural.")},
		// NetRadiant Custom's Exact shapes, which ask for the density first.
		{"map.patch-exact-cylinder", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Exact Patch Cylinder…"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Choose columns, rows and Square, then replace the selection with a true-circle patch cylinder filling its bounds along the view's axis, mapped Natural.")},
		{"map.patch-exact-cone", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Exact Patch Cone…"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Choose columns, rows and Square, then replace the selection with a straight true-circle patch cone filling its bounds along the view's axis, mapped Natural.")},
		{"map.patch-exact-sphere", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Exact Patch Sphere…"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Choose columns, rows and Square, then replace the selection with a true-circle patch sphere filling its bounds along the view's axis, mapped Natural.")},
		{"map.add-thing", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Add Thing"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Add a thing to the open Doom or Hexen map at the centre of the view.")},
		{"map.add-sector", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Add Sector"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Add a Doom sector from corners typed as x,y pairs.")},
		{"map.export-prefab", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Export Selection as Prefab"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Capture the selection's geometry, entities and asset references with an anchor in a prefab file.")},
		{"map.stage-prefab", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Stage Selection as Prefab"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Add the selection as a reusable prefab to the open package's pending changes.")},
		{"map.insert-prefab", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Insert Prefab"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Preview, position and rotate a prefab, giving its internal target links unique names.")},
		{"map.replace-with-prefab", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Replace Selection with Prefab"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Put a prefab where the selection stands and remove the selection, as one undo step.")},
		{"map.generate", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Generate Level"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Plan and build a sealed, playable level from a description, by the rules or with your text model.")},
		{"map.ai-edit", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Edit Map with AI"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Describe a change, review the edits your text model proposes, then apply the ones you keep, each undoable.")},
		// Entities and keys.
		{"map.edit-property", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Edit Selected Property"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Change a key on the selected map object.")},
		{"map.replace-key-values", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Replace Key Values"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Replace one key's value across the map's entities, or only the selected ones, as one undo step.")},
		{"map.select-targets", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Select Targets"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Select the entities the selected entities target.")},
		{"map.select-sources", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Select Sources"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Select the entities that target the selected entities.")},
		// Make Brush Entity, Move to World and Make Structural are listed with
		// the entity ownership commands above.
		{"map.drop-to-floor", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Drop to Floor"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Move the selected point entities straight down onto the surface beneath them.")},
		{"map.make-detail", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Make Detail"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Make the selected brushes detail: the detail content flag in Quake II and III, func_detail in Quake.")},
		// Selection.
		{"map.select-by-texture", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Select by Texture"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Select every brush and patch, or Doom linedef and sector, that uses a texture.")},
		{"map.select-matching", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Select Matching Objects"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Select every object the Objects filter keeps, by name or by properties such as class=light.")},
		{"map.select-whole-entities", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Expand Selection to Whole Entities"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Select the whole brush entity of every selected brush or patch.")},
		{"map.select-connected-geometry", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Select Connected Geometry"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Expand selected Doom vertices, linedefs or sectors to all the geometry their vertices join.")},
		{"map.find-object", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Find Brush by Number"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Select and frame the object a compiler names by entity and brush number.")},
		// Moving, turning, sizing and lining up the selection.
		{"map.move-selection", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Move Selection"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Move the selected map objects by a typed offset.")},
		// NetRadiant Custom's Translate tool and the pivot the manipulators share.
		{"map.translate-tool", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Translate Tool"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Move the selection with X, Y and Z arrows and a view-plane square about a pivot you can drag; choose again for Select.")},
		// NetRadiant Custom's Rotate tool and TrenchBroom's Rotate Tool.
		{"map.rotate-tool", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Rotate Tool"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Turn the selection with X, Y and Z rings, a view ring and, where the profile offers it, a free turn about a pivot you can drag; choose again for Select.")},
		// NetRadiant Custom's Scale tool, TrenchBroom's Scale Tool and NetRadiant
		// Custom's MouseRotateOrScale.
		{"map.scale-tool", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Scale Tool"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Scale the selection with X, Y and Z handles and a view-plane square about a pivot you can drag, or by its box's faces, edges and corners; choose again for Select.")},
		// NetRadiant Custom's Transform tool (MouseTransform) and its Q toggle
		// (MouseDragOrTransform).
		{"map.transform-tool", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Transform Tool"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Skew the selection by its box's sides, turn it by the corners, move it from inside and scale it from just outside, about a pivot you can drag; choose again for Select.")},
		{"map.select-or-transform-tool", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Select or Transform Tool"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Switch to the Transform tool from Select, and back to Select from any other tool; in component modes, between Move and Shear Components.")},
		// NetRadiant Custom's Build tool (MouseBuild).
		{"map.build-tool", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Build Tool"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Drag a selected brush's face out into a new brush or in to split a slab off, or Shift-drag the selection to lay copies end to end for Repeat; choose again for Select.")},
		{"map.rotate-or-scale-tool", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Rotate or Scale Tool"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Switch to the Scale tool from the Rotate tool, and to the Rotate tool otherwise; in component modes, between Rotate and Scale Components.")},
		{"map.set-object-pivot",QT_TRANSLATE_NOOP("SlopStudioSemantics", "Set Object Pivot"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Type the point the object manipulators move and turn the selection about.")},
		{"map.reset-object-pivot", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Reset Object Pivot"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Let the manipulator pivot follow the centre of the selection again.")},
		{"map.rotate-precisely", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Rotate Selection"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Turn the selection by X, Y and Z angles about a chosen pivot, with texture lock; Apply keeps the window open.")},
		{"map.flip-x", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Flip X"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Mirror the selection along the world X axis in any view, as one undo step.")},
		{"map.flip-y", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Flip Y"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Mirror the selection along the world Y axis in any view, as one undo step.")},
		{"map.flip-z", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Flip Z"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Mirror the selection along the world Z axis in any view, as one undo step.")},
		{"map.rotate-x", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Rotate X"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Turn the selection -90 degrees about the world X axis in any view, as one undo step.")},
		{"map.rotate-y", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Rotate Y"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Turn the selection 90 degrees about the world Y axis in any view, as one undo step.")},
		{"map.rotate-z", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Rotate Z"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Turn the selection -90 degrees about the world Z axis in any view, as one undo step.")},
		// View-relative turns (core/level_quick_transform.h); Yaw is Rotate 90
		// Degrees Left and Right above.
		{"map.roll-clockwise", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Roll Clockwise"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Turn the selection 90 degrees clockwise about the view's forward axis, or turn the sweep destination.")},
		{"map.roll-counterclockwise", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Roll Counter-clockwise"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Turn the selection 90 degrees counter-clockwise about the view's forward axis, or turn the sweep destination.")},
		{"map.pitch-clockwise", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Pitch Clockwise"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Turn the selection 90 degrees clockwise about the view's right axis, or turn the sweep destination.")},
		{"map.pitch-counterclockwise", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Pitch Counter-clockwise"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Turn the selection 90 degrees counter-clockwise about the view's right axis, or turn the sweep destination.")},
		// Directional nudges (core/level_nudge.h): one grid step for the
		// selection, the picked components, the sweep destination or the pivot.
		{"map.nudge-left", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Nudge Left"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Move the selection one grid step left in the plan view, or along the camera's nearest horizontal axis.")},
		{"map.nudge-right", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Nudge Right"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Move the selection one grid step right in the plan view, or along the camera's nearest horizontal axis.")},
		{"map.nudge-forward", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Nudge Forward"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Move the selection one grid step up the plan view, or forward along the camera's nearest horizontal axis.")},
		{"map.nudge-back", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Nudge Back"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Move the selection one grid step down the plan view, or back along the camera's nearest horizontal axis.")},
		{"map.nudge-depth-up", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Nudge Depth Up"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Move the selection one grid step towards you along the plan view's depth axis, or up world Z from the camera.")},
		{"map.nudge-depth-down", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Nudge Depth Down"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Move the selection one grid step away from you along the plan view's depth axis, or down world Z from the camera.")},
		{"map.nudge-world-up", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Nudge Up Along Z"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Move the selection one grid step up world Z, whichever view is active.")},
		{"map.nudge-world-down", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Nudge Down Along Z"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Move the selection one grid step down world Z, whichever view is active.")},
		// Duplicate and Move (core/level_nudge.h): one copy, one grid step, one undo step.
		{"map.duplicate-move-left", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Duplicate and Move Left"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Copy the selection and move the copies one grid step left in the plan view, or along the camera's nearest horizontal axis.")},
		{"map.duplicate-move-right", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Duplicate and Move Right"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Copy the selection and move the copies one grid step right in the plan view, or along the camera's nearest horizontal axis.")},
		{"map.duplicate-move-forward", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Duplicate and Move Forward"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Copy the selection and move the copies one grid step up the plan view, or forward along the camera's nearest horizontal axis.")},
		{"map.duplicate-move-back", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Duplicate and Move Back"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Copy the selection and move the copies one grid step down the plan view, or back along the camera's nearest horizontal axis.")},
		{"map.duplicate-move-depth-up", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Duplicate and Move Depth Up"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Copy the selection and move the copies one grid step towards you along the plan view's depth axis, or up world Z from the camera.")},
		{"map.duplicate-move-depth-down", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Duplicate and Move Depth Down"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Copy the selection and move the copies one grid step away from you along the plan view's depth axis, or down world Z from the camera.")},
		{"map.move-to-camera", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Move to Camera"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Centre the selection, or the picked components, on the camera or orbit focus using a grid-snapped translation.")},
		{"map.repeat-transforms", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Repeat Transforms"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Copy the selection and apply the last translation, rotation, scale and shear to the copies about their centre, or to the picked components.")},
		{"map.reset-repeat-transforms", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Reset Repeat Transforms"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Forget the recorded translation, rotation, scale and shear without changing the map or its undo history.")},
		{"map.resize-selection", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Resize Selection"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Fit the selection to a new size; Texture Scale Lock decides whether brush textures stretch with it.")},
		{"map.scale-selection", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Scale Selection"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Scale the selection by X, Y and Z factors about a pivot; negative factors mirror it. Apply keeps the window open.")},
		{"map.shear-selection", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Shear"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Slant the selection: points slide along one axis by how far they lie along another.")},
		{"map.shear-tool", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Shear Tool"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Drag a side handle in a 2D view, or a face of the box in the camera, to slant the selection about the opposite side.")},
		{"map.duplicate-with-offset", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Duplicate with Offset"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Preview copies at an explicit offset, keeping texture alignment and entity ownership.")},
		{"map.paste-with-offset", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Paste with Offset"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Preview clipboard objects at an explicit offset and paste them as one undo step.")},
		{"map.align-left", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Align Left"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Line the selected objects up on the selection's left edge in the active 2D view.")},
		{"map.align-right", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Align Right"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Line the selected objects up on the selection's right edge in the active 2D view.")},
		{"map.align-top", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Align Top"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Line the selected objects up on the selection's top edge in the active 2D view.")},
		{"map.align-bottom", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Align Bottom"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Line the selected objects up on the selection's bottom edge in the active 2D view.")},
		{"map.align-centre-horizontal", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Centre Horizontally"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Line the selected objects' centres up across the active 2D view.")},
		{"map.align-centre-vertical", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Centre Vertically"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Line the selected objects' centres up down the active 2D view.")},
		{"map.texture-scale-lock", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Texture Scale Lock"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Stretch brush textures with the selection during numeric resizing and handle drags.")},
		{"map.allow-valve220", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Allow Valve 220 Conversion"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Let exact texture lock convert the map's classic brush faces to Valve 220 in the same undo step.")},
		// Clipping and brush tools.
		{"map.clip-selection", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Clip Selection"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Preview or cut with an exact axis plane or three XYZ points, keeping one side or both.")},
		{"map.quick-clip", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Quick Clip"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Start a one-shot clip; a successful cut leaves the clipper.")},
		{"map.clipper-options", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Clipper Options"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Choose the cut material, point and side resets, and the double-click action.")},
		{"map.intersect", QT_TRANSLATE_NOOP("SlopStudioSemantics", "CSG Intersect"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Replace the selected brushes with the one brush where they all overlap.")},
		{"map.hollow-touching", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Hollow Touching"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Hollow the selected brushes into walls that meet without overlapping.")},
		{"map.make-room", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Make Room"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Build walls one grid step thick around each selected brush and remove the brush, so it becomes the room; hidden wall faces are caulked and picked faces left open.")},
		{"map.make-room-options", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Make Room Options"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Choose whether Make Room caulks the wall faces the room does not see, with which material, and leaves picked faces open.")},
		{"map.csg-tool", QT_TRANSLATE_NOOP("SlopStudioSemantics", "CSG Tool"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show the CSG Tool: thickness, the faces to leave alone, caulk and keep inner brush, with Shrink, Expand and four hollows.")},
		{"map.csg-shrink", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Shrink Brushes"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Move the selected brushes' faces in by the CSG Tool's thickness, leaving alone the faces its face policy excludes.")},
		{"map.csg-expand", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Expand Brushes"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Move the selected brushes' faces out by the CSG Tool's thickness, leaving alone the faces its face policy excludes.")},
		{"map.csg-hollow-diagonal", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Hollow with Diagonal Joints"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Build CSG Tool walls outside the selected brushes, meeting on mitred joints with no overlap or gap.")},
		{"map.csg-hollow-wrap", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Hollow by Wrapping"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Build CSG Tool walls inside the selected brushes, each stopping at the walls before it.")},
		{"map.csg-hollow-extrude", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Hollow by Extruding Faces"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Build a CSG Tool wall straight out from each face of the selected brushes, its sides square to the face.")},
		{"map.csg-hollow-pull", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Hollow by Pulling Faces"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Build CSG Tool walls outside the selected brushes with their corners filled, as Make Room does.")},
		{"map.snap-vertices", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Snap Vertices to Grid"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Round every vertex of the selected brushes onto the grid and rebuild them.")},
		{"map.round-to-grid", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Round Selection to Grid"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Round each brush vertex, patch control point and point-entity origin onto the grid, or only the picked components.")},
		{"map.edit-brush-components", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Edit Brush Components"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Reshape the selected brush's vertices, edges and faces in a dialog.")},
		// Vertex, edge and face editing.
		{"map.vertex-tool", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Edit Vertices"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Pick and drag brush vertices and patch control points in the plan views and camera.")},
		{"map.edge-tool", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Edit Edges"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show the selected brushes' edges as handles in the plan views and camera and move them.")},
		{"map.face-tool", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Edit Faces"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show the selected brushes' faces as handles and push or pull them to grow or shrink the brush; faces picked by Select Textured move with them.")},
		{"map.next-component", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Next Overlapping Component"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Cycle to the next depth at the last component pick in the active view.")},
		{"map.previous-component", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Previous Overlapping Component"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Cycle to the previous depth at the last component pick in the active view.")},
		{"map.component-touching-edges", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Select Touching Edges"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Convert picked components to the edges that touch any pick.")},
		{"map.component-touching-faces", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Select Touching Faces"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Convert picked components to the faces that touch any pick.")},
		{"map.grow-components", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Grow Component Selection"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Add one neighbour step within each picked brush or patch.")},
		{"map.shrink-components", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Shrink Component Selection"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Remove the component selection's boundary by one neighbour step.")},
		{"map.connected-components", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Select Connected Components"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Select every component connected to the picks within each brush or patch.")},
		{"map.component-move-tool", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Move Components"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Drag picked components, or the camera's XYZ handles.")},
		{"map.component-rotate-tool", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Rotate Components"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Drag an XYZ rotation ring around the shared component pivot.")},
		{"map.component-scale-tool", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Scale Components"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Drag an axis handle, or All for uniform scale, around the shared component pivot.")},
		{"map.component-shear-tool", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Shear Components"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Drag a labelled axis pair to slant the picked components.")},
		{"map.component-pivot-tool", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Move Component Pivot"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Drag the shared component pivot without editing geometry.")},
		{"map.component-reset-pivot", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Reset Component Pivot"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Put the pivot back at the centre of the picked corners' combined bounds.")},
		{"map.component-gizmo-options", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Component Manipulator Options"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Set rotation, scale and shear snapping, or type a shared pivot.")},
		{"map.snap-picked-vertex", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Snap Picked Vertex"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Move one picked vertex exactly onto another brush vertex, merging corners when valid.")},
		{"map.transform-components", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Transform Components"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Move, rotate, scale or shear picked components, including faces picked by Select Textured in face mode, about a shared or typed pivot.")},
		{"map.component-uv-lock", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Lock Component UVs"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Keep each surviving corner's texture coordinates during component moves and transforms.")},
		{"map.component-uv-fit", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Fit UVs When Lock Fails"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Keep three corners' texture coordinates where component UV lock cannot keep them all, instead of refusing the edit.")},
		{"map.component-xray", QT_TRANSLATE_NOOP("SlopStudioSemantics", "X-ray Component Handles"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show and pick camera component handles through surfaces.")},
		{"map.split-components", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Split Edge/Face"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Drag edge midpoints or face centres to make new corners.")},
		{"map.insert-vertex-tool", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Insert Vertex"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Drag from a grid point on an edge or face to add a convex corner.")},
		{"map.extrude-faces", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Extrude Faces"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Move picked brush faces along their normals; every original face must survive.")},
		{"map.extrude-split", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Split Extrusion"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Make a separate brush when pulling out, or divide the brush when pushing in.")},
		{"map.extrude-precisely", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Extrude Faces Precisely"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Extrude picked faces by an exact signed distance along the first face's normal.")},
		{"map.remove-components", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Remove Selected Components"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Remove the picked brush vertices; every affected brush must stay three-dimensional.")},
		// Groups and linked groups.
		{"map.open-group", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Open Group"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Edit the selected group's contents; objects outside it stay visible and protected.")},
		{"map.close-group", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Close Group"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Return to the parent group and select the group just closed.")},
		{"map.close-all-groups", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Close All Groups"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Return to the map and restore the previous creation destination.")},
		{"map.create-linked-duplicate", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Create Linked Duplicate"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Duplicate the selected group beside itself and link the two, so an edit inside either changes both.")},
		{"map.select-linked-groups", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Select Linked Groups"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Select every group linked with the selected linked groups.")},
		{"map.update-linked-groups", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Update Linked Groups"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Make the other groups linked with the selected one match it now.")},
		{"map.separate-linked-groups", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Separate Linked Groups"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Take the selected linked groups out of their link set; they keep their content.")},
		// Patches.
		{"map.patch-insert-columns", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Insert Patch Columns"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Split the first span of each row in two, adding two control columns without changing the surface.")},
		{"map.patch-add-columns", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Add Patch Columns"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Split the last span of each row in two, adding two control columns without changing the surface.")},
		{"map.patch-insert-rows", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Insert Patch Rows"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Split the first span of each column in two, adding two control rows without changing the surface.")},
		{"map.patch-add-rows", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Add Patch Rows"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Split the last span of each column in two, adding two control rows without changing the surface.")},
		{"map.patch-delete-first-columns", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Delete First Patch Columns"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Join the first two spans of each row into one, two control columns fewer.")},
		{"map.patch-delete-last-columns", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Delete Last Patch Columns"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Join the last two spans of each row into one, two control columns fewer.")},
		{"map.patch-delete-first-rows", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Delete First Patch Rows"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Join the first two spans of each column into one, two control rows fewer.")},
		{"map.patch-delete-last-rows", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Delete Last Patch Rows"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Join the last two spans of each column into one, two control rows fewer.")},
		{"map.patch-transpose", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Transpose Patch"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Swap the selected patches' rows and columns, so they face the other way.")},
		{"map.patch-invert", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Invert Patch Facing"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Turn the selected patches to face the other way.")},
		{"map.patch-redisperse-columns", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Re-disperse Patch Columns"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Put each span's middle control point halfway between its ends along the rows.")},
		{"map.patch-redisperse-rows", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Re-disperse Patch Rows"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Put each span's middle control point halfway between its ends down the columns.")},
		{"map.patch-smooth-columns", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Smooth Patch Columns"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Move each joint between spans along the rows halfway between its neighbours, removing creases.")},
		{"map.patch-smooth-rows", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Smooth Patch Rows"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Move each joint between spans down the columns halfway between its neighbours, removing creases.")},
		{"map.stitch-patches", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Stitch Patches"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Join two patch boundaries with exact grid refinement and optional tangent or UV matching.")},
		{"map.thicken-patch", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Thicken Patch"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Give the selected patches thickness: a copy behind each and walls along their open edges.")},
		{"map.deform-patch", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Deform Patch"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Move every control point of the selected patches along one axis by a random amount up to a maximum, from a seed that repeats the result, as NetRadiant Custom's Deform does.")},
		// Textures and surface mapping.
		{"map.apply-texture", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Apply Texture"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Put a texture on every face of the selected brushes and on the selected patches.")},
		{"map.replace-texture", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Find and Replace Textures"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Find or replace a texture across the map, the selected objects or the picked faces, in a window that stays open.")},
		{"map.reveal-material", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Reveal Material"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show the selection's single material on the Textures tab and make it the current material.")},
		{"map.surface-flip-u", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Flip Texture U"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Mirror the texture mapping of the targeted faces and patches along U.")},
		{"map.surface-flip-v", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Flip Texture V"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Mirror the texture mapping of the targeted faces and patches along V.")},
		{"map.surface-fit-u", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Fit Texture Width Only"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Fit one texture repeat across the width of each targeted face or patch, keeping its height.")},
		{"map.surface-fit-v", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Fit Texture Height Only"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Fit one texture repeat across the height of each targeted face or patch, keeping its width.")},
		// NetRadiant Custom's Fit Width and Fit Height, and its Project Axial,
		// Ortho and Camera (core/level_surface.h).
		{"map.surface-fit-width", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Fit Texture Width"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Fit one texture repeat across the width of each targeted face or patch and scale its height to match.")},
		{"map.surface-fit-height", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Fit Texture Height"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Fit one texture repeat down the height of each targeted face or patch and scale its width to match.")},
		{"map.surface-project-axial", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Project Texture Axially"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Project the default texture along the world axis each picked or targeted face looks down most, and patches along the axis nearest their facing.")},
		{"map.surface-project-view", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Project Texture from 2D View"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Project the default texture onto the picked or targeted faces and patches straight from the active 2D view.")},
		{"map.surface-project-camera", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Project Texture from Camera"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Project the default texture onto the picked or targeted faces and patches along the 3D camera's view.")},
		// NetRadiant Custom's Surface Inspector Unset (core/level_face_flags.h).
		{"map.surface-clear-flags", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Clear Face Flags"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Clear every content and surface flag and the value of the picked or targeted brush faces, keeping the detail flag.")},
		{"map.surface-justify-left", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Justify Texture Left"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Line the texture up with the left edge of each targeted face or patch.")},
		{"map.surface-justify-right", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Justify Texture Right"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Line the texture up with the right edge of each targeted face or patch.")},
		{"map.surface-justify-top", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Justify Texture Top"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Line the texture up with the top edge of each targeted face or patch.")},
		{"map.surface-justify-bottom", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Justify Texture Bottom"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Line the texture up with the bottom edge of each targeted face or patch.")},
		{"map.surface-natural", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Natural Texture Mapping"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Reset the targeted faces to the default world-axis projection, and patches to their natural spread.")},
		{"map.surface-face-aligned", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Align Texture to Face"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Project the texture in each targeted face's own plane.")},
		// TrenchBroom's and Radiant's texture resets and bobToolz's Reset
		// Textures (core/level_surface.h).
		{"map.surface-reset", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Reset Texture Alignment"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Reset the picked faces, or the Surfaces target, to no shift or rotation at the default scale, each keeping its projection kind, as TrenchBroom does.")},
		{"map.surface-radiant-reset", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Radiant Texture Reset"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Reset as Radiant does: classic faces natural, Valve 220 and brush-primitive faces in their plane's own basis, and patches capped.")},
		{"map.reset-textures", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Reset Textures"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Set absolute texture scale, shift and rotation, or a new texture name, on the selection or the whole map, optionally only where one texture is used.")},
		// Camera-relative texture moves and flips (core/level_camera_texture.h):
		// the picked faces, or the Surfaces target, as the 3D camera sees them.
		{"map.move-textures-up", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Move Textures Up"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Move the textures of the picked faces, or of the Surfaces target, one grid size up the 3D camera's view.")},
		{"map.move-textures-down", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Move Textures Down"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Move the textures of the picked faces, or of the Surfaces target, one grid size down the 3D camera's view.")},
		{"map.move-textures-left", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Move Textures Left"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Move the textures of the picked faces, or of the Surfaces target, one grid size left in the 3D camera's view.")},
		{"map.move-textures-right", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Move Textures Right"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Move the textures of the picked faces, or of the Surfaces target, one grid size right in the 3D camera's view.")},
		{"map.move-textures-up-coarse", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Move Textures Up (Coarse)"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Move the textures twice the grid size up the 3D camera's view.")},
		{"map.move-textures-down-coarse", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Move Textures Down (Coarse)"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Move the textures twice the grid size down the 3D camera's view.")},
		{"map.move-textures-left-coarse", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Move Textures Left (Coarse)"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Move the textures twice the grid size left in the 3D camera's view.")},
		{"map.move-textures-right-coarse", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Move Textures Right (Coarse)"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Move the textures twice the grid size right in the 3D camera's view.")},
		{"map.move-textures-up-fine", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Move Textures Up (Fine)"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Move the textures one texel up the 3D camera's view, whatever the grid.")},
		{"map.move-textures-down-fine", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Move Textures Down (Fine)"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Move the textures one texel down the 3D camera's view, whatever the grid.")},
		{"map.move-textures-left-fine", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Move Textures Left (Fine)"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Move the textures one texel left in the 3D camera's view, whatever the grid.")},
		{"map.move-textures-right-fine", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Move Textures Right (Fine)"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Move the textures one texel right in the 3D camera's view, whatever the grid.")},
		{"map.flip-textures-horizontally", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Flip Textures Horizontally"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Mirror the texture axis that runs across the 3D camera's view on the picked faces, or on the Surfaces target.")},
		{"map.flip-textures-vertically", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Flip Textures Vertically"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Mirror the texture axis that runs up the 3D camera's view on the picked faces, or on the Surfaces target.")},
		// TrenchBroom's fixed-step texture turns, as seen from each face's front.
		{"map.rotate-textures-clockwise", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Rotate Textures Clockwise"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Turn the textures of the picked faces, or of the Surfaces target, 15 degrees clockwise as seen from each face's front, about the texture origin.")},
		{"map.rotate-textures-clockwise-coarse", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Rotate Textures Clockwise (Coarse)"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Turn the textures a quarter turn, 90 degrees, clockwise as seen from each face's front.")},
		{"map.rotate-textures-clockwise-fine", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Rotate Textures Clockwise (Fine)"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Turn the textures 1 degree clockwise as seen from each face's front.")},
		{"map.rotate-textures-counterclockwise", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Rotate Textures Counter-clockwise"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Turn the textures of the picked faces, or of the Surfaces target, 15 degrees counter-clockwise (anticlockwise) as seen from each face's front, about the texture origin.")},
		{"map.rotate-textures-counterclockwise-coarse", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Rotate Textures Counter-clockwise (Coarse)"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Turn the textures a quarter turn, 90 degrees, counter-clockwise (anticlockwise) as seen from each face's front.")},
		{"map.rotate-textures-counterclockwise-fine", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Rotate Textures Counter-clockwise (Fine)"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Turn the textures 1 degree counter-clockwise (anticlockwise) as seen from each face's front.")},
		// Doom camera editing.
		{"map.raise-camera-surface", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Raise Surface at Crosshair"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Raise the Doom floor or ceiling under the camera's crosshair by 8 units.")},
		{"map.lower-camera-surface", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Lower Surface at Crosshair"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Lower the Doom floor or ceiling under the camera's crosshair by 8 units.")},
		{"map.brighten-camera-surface", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Brighten Sector at Crosshair"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Raise the light of the Doom sector under the camera's crosshair by 16.")},
		{"map.darken-camera-surface", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Darken Sector at Crosshair"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Lower the light of the Doom sector under the camera's crosshair by 16.")},
		{"map.nudge-camera-texture-left", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Nudge Texture Left at Crosshair"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Slide the texture of the Doom wall under the camera's crosshair one unit left.")},
		{"map.nudge-camera-texture-right", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Nudge Texture Right at Crosshair"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Slide the texture of the Doom wall under the camera's crosshair one unit right.")},
		{"map.nudge-camera-texture-up", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Nudge Texture Up at Crosshair"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Slide the texture of the Doom wall under the camera's crosshair one unit up.")},
		{"map.nudge-camera-texture-down", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Nudge Texture Down at Crosshair"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Slide the texture of the Doom wall under the camera's crosshair one unit down.")},
		{"map.drag-camera-textures", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Drag Textures in Camera"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Drag a Doom wall's texture in the camera to slide its offsets; each drag is one undo step.")},
		{"map.align-wall-textures-crosshair", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Align Textures at Crosshair"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Line up the textures of the walls joined to the one under the camera's crosshair.")},
		// Doom geometry, sectors and lighting.
		{"map.merge-vertices", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Merge Vertices"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Join the selected Doom vertices into the one picked last.")},
		{"map.join-sectors", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Join Sectors"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Make the selected Doom sectors one sector, keeping the lines between them.")},
		{"map.merge-sectors", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Merge Sectors"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Make the selected Doom sectors one sector and remove the lines between them.")},
		{"map.flip-linedefs", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Flip Linedefs"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Turn each selected Doom linedef around, with its sides.")},
		{"map.split-linedefs", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Split Linedefs"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Split each selected Doom linedef at its middle.")},
		{"map.curve-linedefs", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Curve Linedefs"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Bend the selected Doom linedefs into arcs of several linedefs.")},
		{"map.make-door", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Make Door"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Turn the selected Doom sectors into doors that open when used.")},
		{"map.make-sector-mode", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Make Sector Mode"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Click inside a closed shape of lines in the Top view to make a sector of it.")},
		{"map.align-wall-textures", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Align Wall Textures"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Line up the textures of the walls joined to the first selected linedef.")},
		{"map.brighten", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Brighten Sectors"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Raise the selected Doom sectors' light by 16, or by 1 with Shift.")},
		{"map.darken", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Darken Sectors"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Lower the selected Doom sectors' light by 16, or by 1 with Shift.")},
		{"map.gradient-floors", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Gradient Floors"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Spread floor heights evenly over the selected sectors, from the first picked to the last.")},
		{"map.gradient-ceilings", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Gradient Ceilings"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Spread ceiling heights evenly over the selected sectors, from the first picked to the last.")},
		{"map.gradient-light", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Gradient Brightness"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Spread light levels evenly over the selected sectors, from the first picked to the last.")},
		{"map.udmf-properties", QT_TRANSLATE_NOOP("SlopStudioSemantics", "UDMF Properties"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Edit text-map properties, keeping comments and extension fields.")},
		// Regions.
		{"map.region-draw", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Draw Region"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Drag a box in a 2D view to make it the region, through the whole map's depth.")},
		{"map.region-set-brush", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Set Region to Brush"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Make the selected brushes' bounds the region and remove the brushes; Undo brings them back.")},
		{"map.region-bounds", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Region Bounds"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Type the region's lowest and highest corners.")},
		{"map.region-select", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Select Everything in Region"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Select every object the region keeps that is not hidden.")},
		{"map.region-set-selection", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Set Region to Selection"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Keep the views to the selection's bounds.")},
		{"map.region-set-view", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Set Region to View"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Keep the views to what the active 2D view shows.")},
		{"map.region-clear", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Clear Region"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show the whole map again.")},
		{"map.region-save", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Save Region As"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Write the region as a sealed map of its own, to compile and test on its own.")},
		{"map.region-compile", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Compile Region"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Write the region beside the map as a sealed map and compile it with the chosen profile.")},
		// Navigation and plan views.
		{"map.zoom-to-fit", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Zoom to Fit"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Frame the whole map in every 2D view.")},
		{"map.center-on-selection", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Centre Views on Selection"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Move every 2D view to the selection's centre without changing its zoom.")},
		{"map.go-to-coordinates", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Go to Coordinates"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Centre the 2D views on a point you type, and take the camera there.")},
		{"map.center-plans-on-camera", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Centre Plan Views on Camera"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Bring the camera position or orbit target into every plan view without changing zoom.")},
		{"map.follow-camera", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Plan Views Follow Camera"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Keep the plan views centred on the camera as it moves.")},
		{"map.link-plan-centers", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Link Plan Centres"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Keep the top, front and side views centred on the same world point.")},
		{"map.link-plan-zoom", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Link Plan Zoom"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Use the same pixels per map unit in every plan view.")},
		{"map.layout.profile", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Layout: Follow Editor Profile"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Use the level view layout the editor profile prefers.")},
		{"map.layout.single-2d", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Layout: One View, 2D First"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Keep one level view that starts in 2D, without changing the editor profile.")},
		{"map.layout.single-3d", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Layout: One View, 3D First"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Keep one level view that starts in 3D, without changing the editor profile.")},
		{"map.layout.camera-and-plan", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Layout: 3D Camera Beside a 2D View"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show the camera beside one 2D view, without changing the editor profile.")},
		{"map.layout.four-views", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Layout: Four Views"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show the camera with the top, front and side views, without changing the editor profile.")},
		{"map.layout.camera-above-plans", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Layout: Camera Above Plans"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show the camera above the three plan views, without changing the editor profile.")},
		{"map.layout.camera-beside-plans", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Layout: Camera Beside Plans"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show the camera beside the three plan views, without changing the editor profile.")},
		// View filters (core/level_view_filters.h): what the Levels views draw, never the map.
		{"map.filter-world", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Filter: World Brushes"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show or hide worldspawn, func_group and func_detail with their brushes and patches in the Levels views.")},
		{"map.filter-entities", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Filter: Entities"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show or hide every entity but worldspawn, func_group and func_detail in the Levels views.")},
		{"map.filter-brushes", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Filter: Brushes"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show or hide every brush in the Levels views, whatever entity owns it; patches stay.")},
		{"map.filter-brush-entities", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Filter: Brush Entities"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show or hide doors, triggers and the other brush entities, other than func_group and func_detail.")},
		{"map.filter-func-groups", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Filter: Func Groups"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show or hide func_group entities with their brushes and patches in the Levels views.")},
		{"map.filter-patches", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Filter: Patches"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show or hide Quake III patches in the Levels views.")},
		{"map.filter-detail", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Filter: Detail"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show or hide detail brushes and func_detail entities in the Levels views.")},
		{"map.filter-structural", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Filter: Structural"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show or hide the structural brushes, every brush that is not detail, in the Levels views.")},
		{"map.filter-clip", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Filter: Clip"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show or hide player, monster and weapon clip brushes and patches in the Levels views.")},
		{"map.filter-player-clip", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Filter: Player Clip"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show or hide brushes and patches made wholly of player clip, by content flag, surfaceparm playerclip or texture name.")},
		{"map.filter-monster-clip", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Filter: Monster Clip"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show or hide brushes and patches made wholly of monster clip, by content flag, surfaceparm monsterclip or texture name.")},
		{"map.filter-weapon-clip", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Filter: Weapon Clip"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show or hide weapclip, fullclip and other weapon clip brushes and patches in the Levels views.")},
		{"map.filter-botclip", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Filter: Bot Clip"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show or hide Quake III botclip and donotenter brushes in the Levels views.")},
		{"map.filter-hint", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Filter: Hint"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show or hide brushes with a hint face in the Levels views.")},
		{"map.filter-skip", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Filter: Skip"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show or hide brushes skipped on every face in the Levels views, and skip faces on other brushes, such as a hint brush's sides, in the camera.")},
		{"map.filter-caulk", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Filter: Caulk"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show or hide brushes caulked on every face in the Levels views, and caulk faces on other brushes in the camera.")},
		{"map.filter-nodraw", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Filter: Nodraw"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show or hide brushes that draw nothing in game: every face nodraw by surface flag, surfaceparm nodraw or texture name.")},
		{"map.filter-areaportals", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Filter: Area Portals"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show or hide areaportal brushes and Quake II func_areaportal entities in the Levels views.")},
		{"map.filter-clusterportals", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Filter: Cluster Portals"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show or hide Quake III clusterportal brushes in the Levels views.")},
		{"map.filter-lightgrid", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Filter: Light Grid"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show or hide Quake III lightgrid brushes in the Levels views.")},
		{"map.filter-sky", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Filter: Sky"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show or hide brushes with a sky face in the Levels views; Quake III shaders count when they declare skyParms.")},
		{"map.filter-liquids", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Filter: Liquids"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show or hide water, slime and lava brushes in the Levels views, by Quake III surfaceparm, content flag or texture name.")},
		{"map.filter-fog", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Filter: Fog"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show or hide Quake III fog volumes in the Levels views, by shader, the fog content flag or texture name.")},
		{"map.filter-translucent", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Filter: Translucent"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show or hide glass, grates and other see-through brushes and patches: Quake II translucency flags, fence textures, or qer_trans, alphaFunc and fog shaders.")},
		{"map.filter-decals", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Filter: Decals"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show or hide Quake III brushes with a decals/ face in the Levels views.")},
		{"map.filter-point-entities", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Filter: Point Entities"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show or hide point entities such as player starts and items in the Levels views, lights and models apart.")},
		{"map.filter-lights", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Filter: Lights"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show or hide light entities in the Levels views.")},
		{"map.filter-triggers", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Filter: Triggers"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show or hide trigger entities in the Levels views.")},
		{"map.filter-monsters", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Filter: Monsters"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show or hide monster entities in the Levels views.")},
		{"map.filter-items", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Filter: Items and Weapons"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show or hide item, weapon and ammunition entities in the Levels views.")},
		{"map.filter-starts", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Filter: Player Starts"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show or hide player start entities in the Levels views.")},
		{"map.filter-paths", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Filter: Paths"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show or hide path corners and other path entities in the Levels views.")},
		{"map.filter-models", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Filter: Models"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show or hide misc_model and misc_gamemodel entities in the Levels views.")},
		{"map.invert-filters", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Invert Filters"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Hide every kind the view filters show and show every kind they hide, as Radiant's Invert filters does.")},
		{"map.reset-filters", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show Every Kind"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show every kind of object in the Levels views again, as Radiant's Reset filters does.")},
		// Compiler overlays.
		{"map.clear-leak-trail", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Clear Leak Trail"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Remove the leak trail from the map views.")},
		{"map.load-portals", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Load Portal File"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Outline a compiler's vis portals (.prt) over the open map.")},
		{"map.clear-portals", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Clear Portals"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Remove the portal outlines from the map views.")},
		// Levels sidebars.
		{"map.sidebar.toggle-leading", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Fold or Open the Browser Sidebar"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Fold the sidebar of browsers down to its tabs, or open it again.")},
		{"map.sidebar.toggle-trailing", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Fold or Open the Properties Sidebar"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Fold the sidebar of properties down to its tabs, or open it again.")},
		{"map.sidebar.captions", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Sidebar Tab Captions"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Name each Levels sidebar tab under its glyph.")},
		{"map.sidebar.reset", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Reset Sidebars"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Put every Levels sidebar tab back where the editor profile places it.")},
		{"map.sidebar.outliner", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show Outliner"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show the Levels Outliner: every object, filtered by name or key=value, with layers and groups.")},
		{"map.sidebar.entities", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show Entities"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show the entity classes or Doom things to place.")},
		{"map.sidebar.shapes", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show Shapes"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show the brush shapes to draw or add, or to turn the selected brushes into.")},
		{"map.sidebar.textures", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show Textures"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show the map's and the package's textures, to apply or paint.")},
		{"map.sidebar.models", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show Models"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show the package models to place or give the selected entity.")},
		{"map.sidebar.sounds", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show Sounds"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show the package sounds to listen to, place as speakers or give the selected entity.")},
		{"map.sidebar.prefabs", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show Prefabs"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show the saved prefabs to insert, and save the selection as one.")},
		{"map.sidebar.tools", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show Tools"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show every Levels editing tool in one place, grouped by task.")},
		{"map.sidebar.inspector", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show Inspector"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show the selection's keys, fields, position and definition, to edit in place.")},
		{"map.sidebar.surfaces", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show Surfaces"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show the Surfaces tab, or hide it again: texture alignment, projection and face flags for the targeted faces.")},
		{"map.show-face-inspector", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show Face Inspector"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show the Surfaces tab at its Face section: the picked face over its texture with move, turn and stretch handles, and the targeted faces' offset, scale and rotation.")},
		// NetRadiant Custom's UV tool: the texture dragged on the surface in the camera.
		{"map.uv-tool", QT_TRANSLATE_NOOP("SlopStudioSemantics", "UV Tool"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Move, turn, stretch and skew the picked face's or selected patch's texture in the camera about a pivot, and move a patch's control points in texture space, each drag one undo step.")},
		{"map.sidebar.map", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show Map"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show the Map tab, TrenchBroom's Map inspector: worldspawn keys, the pre-build checklist, layers, soft bounds, mods, statistics and the entity definitions in use.")},
		{"map.sidebar.view", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show View"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show what the views draw, filters, grid and snapping, the camera and the view layout.")},
		{"map.sidebar.health", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show Health"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show validation results, missing assets and leaks, each leading to the object concerned.")},
		{"map.sidebar.history", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show History"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show every edit to the map, oldest first; choose one to undo or redo to it.")},
		{"map.statistics", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show Map Statistics"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Open the Map tab's Statistics: entity, brush, face, patch and texture counts, bounds and issues.")},
		{"map.map-info", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Map Info"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Open the Map tab's Statistics at its class table: brush, patch and entity totals, group and in-game entities, and every entity class with its count, as NetRadiant Custom's Map Info does.")},
		{"map.worldspawn", QT_TRANSLATE_NOOP("SlopStudioSemantics", "Show Worldspawn Keys"),
			QT_TRANSLATE_NOOP("SlopStudioSemantics", "Open the Map tab's Worldspawn list to add or change worldspawn keys, each change one undo step.")},
	};
	for (const auto& command : kLevelActions) {
		const QString commandId = QString::fromLatin1(command.commandId);
		entries.push_back(paletteEntry(QString(commandId).replace(QLatin1Char('.'), QLatin1Char('-')), commandId,
			QCoreApplication::translate("SlopStudioSemantics", command.label), QCoreApplication::translate("SlopStudioSemantics", "Levels"),
			QCoreApplication::translate("SlopStudioSemantics", command.summary)));
	}

	for (const ShortcutDescriptor& descriptor : shortcutDescriptors()) {
		if (!descriptor.commandId.startsWith(QLatin1String("shell.mode."))) {
			continue;
		}
		entries.push_back(paletteEntry(
			descriptor.id,
			descriptor.commandId,
			descriptor.label,
			QCoreApplication::translate("SlopStudioSemantics", "Modes"),
			descriptor.description));
	}

	std::sort(entries.begin(), entries.end(), [](const CommandPaletteEntry& left, const CommandPaletteEntry& right) {
		return QString::localeAwareCompare(left.label, right.label) < 0;
	});
	return entries;
}

bool commandPaletteEntryForCommandId(const QString& commandId, CommandPaletteEntry* out)
{
	const QString requested = normalizedId(commandId);
	for (const CommandPaletteEntry& entry : commandPaletteEntries()) {
		if (normalizedId(entry.commandId) == requested) {
			if (out) {
				*out = entry;
			}
			return true;
		}
	}
	// The Create <class> commands are generated from the loaded entity
	// definitions (core/level_entity_create.h), so their entries are too.
	QString className;
	if (levelEntityCreateClassForCommand(commandId, &className)) {
		if (out) {
			*out = paletteEntry(QString(requested).replace(QLatin1Char('.'), QLatin1Char('-')), commandId.trimmed(),
				QCoreApplication::translate("SlopStudioSemantics", "Create %1").arg(className), QCoreApplication::translate("SlopStudioSemantics", "Levels"),
				QCoreApplication::translate("SlopStudioSemantics",
					"Create a %1 from the loaded entity definitions: a point class at the pointer or the middle of the view, a brush class from the selected brushes.")
					.arg(className));
		}
		return true;
	}
	// So are the Toggle <class> Visible commands, and the Toggle <tag> Visible
	// commands of the tags game or project data define (core/level_filter_tags.h).
	if (levelFilterClassForCommand(commandId, &className)) {
		if (out) {
			*out = paletteEntry(QString(requested).replace(QLatin1Char('.'), QLatin1Char('-')), commandId.trimmed(),
				QCoreApplication::translate("SlopStudioSemantics", "Toggle %1 Visible").arg(className), QCoreApplication::translate("SlopStudioSemantics", "Levels"),
				QCoreApplication::translate("SlopStudioSemantics",
					"Show or hide every %1 entity in the Levels views, those added later included. The filter lasts for the map session and never changes the map.")
					.arg(className));
		}
		return true;
	}
	QString tagId;
	bool tagEnabled = false;
	if (levelSmartTagForCommand(commandId, &tagId, &tagEnabled)) {
		if (out) {
			const QString tag = tagId.mid(4);
			*out = paletteEntry(QString(requested).replace(QLatin1Char('.'), QLatin1Char('-')), commandId.trimmed(),
				tagEnabled ? QCoreApplication::translate("SlopStudioSemantics", "Make %1").arg(tag)
					: QCoreApplication::translate("SlopStudioSemantics", "Make non-%1").arg(tag),
				QCoreApplication::translate("SlopStudioSemantics", "Levels"),
				QCoreApplication::translate("SlopStudioSemantics", "Apply or remove a configured tag on the selection in one undo step."));
		}
		return true;
	}
	if (levelFilterTagForCommand(commandId, &tagId)) {
		if (out) {
			const QString tag = tagId.mid(4);
			*out = paletteEntry(QString(requested).replace(QLatin1Char('.'), QLatin1Char('-')), commandId.trimmed(),
				QCoreApplication::translate("SlopStudioSemantics", "Toggle %1 Visible").arg(tag), QCoreApplication::translate("SlopStudioSemantics", "Levels"),
				QCoreApplication::translate("SlopStudioSemantics",
					"Show or hide what the %1 tag of the game or project data names in the Levels views. The map is unchanged.")
					.arg(tag));
		}
		return true;
	}
	return false;
}

QStringList shellCommandIds()
{
	QStringList ids;
	for (const ShortcutDescriptor& descriptor : shortcutDescriptors()) {
		ids.push_back(descriptor.commandId);
	}
	for (const CommandPaletteEntry& entry : commandPaletteEntries()) {
		ids.push_back(entry.commandId);
	}
	ids.removeDuplicates();
	ids.sort();
	return ids;
}

bool shellCommandIdExists(const QString& commandId)
{
	const QString requested = normalizedId(commandId);
	if (requested.isEmpty()) {
		return false;
	}
	// Profile construction checks every binding. These built-in identifiers
	// are invariant across locale changes; rebuilding translated shortcut and
	// palette descriptions for each lookup makes shell startup quadratic.
	// Cache identifiers only, so visible labels still use the current locale.
	static const QSet<QString> registered = [] {
		QSet<QString> ids;
		for (const QString& id : shellCommandIds()) {
			ids.insert(normalizedId(id));
		}
		return ids;
	}();
	// Create <class> commands exist for whatever classes the definitions name,
	// and filter commands for whatever classes and tags data names.
	return registered.contains(requested) || levelEntityCreateClassForCommand(commandId) || levelFilterClassForCommand(commandId)
		|| levelFilterTagForCommand(commandId) || levelSmartTagForCommand(commandId);
}

QString statusChipSummaryText()
{
	QStringList lines;
	lines << QCoreApplication::translate("SlopStudioSemantics", "Status chips");
	for (const StatusChipDescriptor& descriptor : statusChipDescriptors()) {
		lines << QCoreApplication::translate("SlopStudioSemantics", "- %1 [%2/%3]: %4").arg(descriptor.label, descriptor.domain, operationStateId(descriptor.state), descriptor.nonColorCue);
		lines << QCoreApplication::translate("SlopStudioSemantics", "  Next action: %1").arg(descriptor.nextAction);
	}
	return lines.join('\n');
}

QString shortcutRegistrySummaryText()
{
	QStringList lines;
	lines << QCoreApplication::translate("SlopStudioSemantics", "Shortcut registry");
	QStringList conflicts;
	const bool hasConflicts = shortcutRegistryHasConflicts(&conflicts);
	lines << QCoreApplication::translate("SlopStudioSemantics", "Conflicts: %1").arg(hasConflicts ? conflicts.join(QStringLiteral(", ")) : QCoreApplication::translate("SlopStudioSemantics", "none"));
	for (const ShortcutDescriptor& descriptor : shortcutDescriptors()) {
		QString line = QCoreApplication::translate("SlopStudioSemantics", "- %1: %2 (%3)").arg(descriptor.defaultSequence, descriptor.label, descriptor.context);
		if (!descriptor.alternateSequences.isEmpty()) {
			line += QCoreApplication::translate("SlopStudioSemantics", " - also %1").arg(descriptor.alternateSequences.join(QStringLiteral(", ")));
		}
		lines << line;
	}
	return lines.join('\n');
}

QString commandPaletteSummaryText()
{
	QStringList lines;
	lines << QCoreApplication::translate("SlopStudioSemantics", "Command palette");
	for (const CommandPaletteEntry& entry : commandPaletteEntries()) {
		QStringList flags;
		if (entry.requiresProject) {
			flags.push_back(QCoreApplication::translate("SlopStudioSemantics", "requires project"));
		}
		if (entry.destructive) {
			flags.push_back(entry.stagedOrDryRun ? QCoreApplication::translate("SlopStudioSemantics", "staged") : QCoreApplication::translate("SlopStudioSemantics", "destructive"));
		}
		lines << QCoreApplication::translate("SlopStudioSemantics", "- %1 [%2]%3").arg(entry.label, entry.category, flags.isEmpty() ? QString() : QCoreApplication::translate("SlopStudioSemantics", " - %1").arg(flags.join(QStringLiteral(", "))));
		lines << QCoreApplication::translate("SlopStudioSemantics", "  %1").arg(entry.summary);
	}
	return lines.join('\n');
}

} // namespace slopstudio
