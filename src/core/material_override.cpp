#include "core/material_override.h"

#include "core/material_script.h"

#include <QCoreApplication>

namespace vibestudio {
namespace {

struct Text {
	Q_DECLARE_TR_FUNCTIONS(VibeStudioMaterials)
};

// A WAL is at most a few hundred KiB; this only stops a damaged entry.
constexpr qint64 kMaximumOverrideFileBytes = 16LL * 1024 * 1024;

} // namespace

QString materialOverrideScriptPath(MaterialEngine engine, MaterialDialect dialect)
{
	if (engine == MaterialEngine::Doom3) {
		// idDeclManager keeps the first definition, reading files in sorted order.
		return QStringLiteral("materials/00_vibestudio_overrides.mtr");
	}
	if (engine == MaterialEngine::Quake3) {
		// ioquake3 and its kin read the scripts in reverse, so the last one
		// wins; Quake III 1.32's shader text hash finds the first.
		return materialDialectLastScriptWins(dialect) ? QStringLiteral("scripts/zz_vibestudio_overrides.shader")
													  : QStringLiteral("scripts/00_vibestudio_overrides.shader");
	}
	return {};
}

bool materialOverrideForEntry(const MaterialLibrary& library, int entry, MaterialDialect dialect, MaterialOverride* out, QString* error)
{
	const auto failure = [error](const QString& message) {
		if (error) {
			*error = message;
		}
		return false;
	};
	if (entry < 0 || entry >= library.entries.size() || !library.entries.at(entry).base) {
		return failure(Text::tr("Only a base game material can be overridden in the package."));
	}
	const MaterialLibraryEntry& item = library.entries.at(entry);
	MaterialOverride copy;
	copy.name = item.name;
	copy.engine = item.engine;
	if (item.engine == MaterialEngine::Quake2) {
		// A Quake II WAL: the package gets its own copy of the file.
		QString readError;
		if (!library.reader || !library.reader->readEntryBytes(item.sourcePath, &copy.fileBytes, &readError, kMaximumOverrideFileBytes)
			|| copy.fileBytes.isEmpty()) {
			return failure(readError.isEmpty() ? Text::tr("%1 could not be read.").arg(item.sourcePath) : readError);
		}
		copy.virtualPath = item.sourcePath;
		*out = copy;
		return true;
	}
	const MaterialScript* script = item.scriptIndex >= 0 ? library.script(entry) : nullptr;
	copy.virtualPath = materialOverrideScriptPath(item.engine, dialect);
	if (!script || copy.virtualPath.isEmpty()) {
		return failure(Text::tr("Only shaders, materials and Quake II textures can be copied into the package."));
	}
	const MaterialDefinition definition = library.definition(entry);
	if (definition.span.isValid() && definition.span.end <= script->text.size()) {
		copy.definitionText = script->text.mid(definition.span.start, definition.span.length());
	}
	if (copy.definitionText.trimmed().isEmpty()) {
		return failure(Text::tr("The definition's text could not be read."));
	}
	*out = copy;
	return true;
}

bool materialOverrideScriptText(const QString& existing, const MaterialOverride& copy, MaterialDialect dialect, QString* text, QString* error)
{
	if (existing.trimmed().isEmpty()) {
		*text = copy.definitionText + QLatin1Char('\n');
		return true;
	}
	const MaterialScript script = parseMaterialScript(existing, copy.engine, copy.virtualPath, dialect);
	if (script.find(copy.name)) {
		if (error) {
			*error = Text::tr("%1 already defines %2.").arg(copy.virtualPath, copy.name);
		}
		return false;
	}
	MaterialEdit edit;
	edit.kind = MaterialEditKind::AddDefinition;
	edit.text = copy.definitionText;
	const MaterialEditResult result = applyMaterialEdit(script, edit);
	if (!result.ok) {
		if (error) {
			*error = result.error;
		}
		return false;
	}
	*text = result.text;
	return true;
}

} // namespace vibestudio
