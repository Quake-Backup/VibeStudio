#include "core/model_decode_issues.h"

#include <QCoreApplication>
#include <QJsonObject>

#include <algorithm>

namespace vibestudio {

namespace {

// The first recorded issue that is still unclaimed and carries `text` for
// `surface`. A surface issue may have been noted before its surface took its
// final place, so any surface issue with the same text is the fallback.
int claimIssue(QVector<ModelDecodeIssue>* issues, QVector<bool>* used, int surface, const QString& text)
{
	for (int index = 0; index < issues->size(); ++index) {
		if (!used->at(index) && issues->at(index).surface == surface && issues->at(index).text == text) {
			(*used)[index] = true;
			return index;
		}
	}
	if (surface < 0) {
		return -1;
	}
	for (int index = 0; index < issues->size(); ++index) {
		if (!used->at(index) && issues->at(index).surface >= 0 && issues->at(index).text == text) {
			(*used)[index] = true;
			(*issues)[index].surface = surface;
			return index;
		}
	}
	return -1;
}

bool blocks(const ModelDecodeIssue& issue)
{
	return issue.severity == ModelDecodeIssueSeverity::Damage
		|| (issue.severity == ModelDecodeIssueSeverity::Omission && !issue.accepted);
}

} // namespace

QString modelDecodeIssueSeverityId(ModelDecodeIssueSeverity severity)
{
	switch (severity) {
	case ModelDecodeIssueSeverity::Advisory:
		return QStringLiteral("advisory");
	case ModelDecodeIssueSeverity::Omission:
		return QStringLiteral("omission");
	case ModelDecodeIssueSeverity::Damage:
		break;
	}
	return QStringLiteral("damage");
}

bool modelDecodeIssueSeverityFromId(const QString& id, ModelDecodeIssueSeverity* severity)
{
	for (const auto candidate : {ModelDecodeIssueSeverity::Advisory, ModelDecodeIssueSeverity::Omission, ModelDecodeIssueSeverity::Damage}) {
		if (modelDecodeIssueSeverityId(candidate) == id) {
			if (severity) {
				*severity = candidate;
			}
			return true;
		}
	}
	return false;
}

QString modelDecodeIssueSeverityName(ModelDecodeIssueSeverity severity)
{
	switch (severity) {
	case ModelDecodeIssueSeverity::Advisory:
		return QCoreApplication::translate("VibeStudioModelDecodeIssues", "Advisory");
	case ModelDecodeIssueSeverity::Omission:
		return QCoreApplication::translate("VibeStudioModelDecodeIssues", "Omission");
	case ModelDecodeIssueSeverity::Damage:
		break;
	}
	return QCoreApplication::translate("VibeStudioModelDecodeIssues", "Damage");
}

void noteModelDecodeIssue(ModelMesh* mesh, ModelDecodeIssueSeverity severity, const QString& id, const QString& text)
{
	if (!mesh) {
		return;
	}
	mesh->warnings << text;
	mesh->issues.append(ModelDecodeIssue{id, severity, -1, text, false});
}

void noteModelSurfaceIssue(ModelMesh* mesh, ModelSurface* surface, ModelDecodeIssueSeverity severity, const QString& id, const QString& text)
{
	if (!mesh || !surface) {
		return;
	}
	surface->warnings << text;
	const int index = surface->index >= 0 ? surface->index : int(mesh->surfaces.size());
	mesh->issues.append(ModelDecodeIssue{id, severity, index, text, false});
}

bool reclassifyModelDecodeIssue(ModelMesh* mesh, const QString& text, ModelDecodeIssueSeverity severity)
{
	if (!mesh) {
		return false;
	}
	for (auto issue = mesh->issues.rbegin(); issue != mesh->issues.rend(); ++issue) {
		if (issue->text == text) {
			issue->severity = severity;
			return true;
		}
	}
	return false;
}

QVector<ModelDecodeIssue> modelDecodeIssues(const ModelMesh& mesh)
{
	QVector<ModelDecodeIssue> result = mesh.issues;
	QVector<bool> used(result.size(), false);
	const QString unclassified = QStringLiteral("unclassified");
	for (const QString& warning : mesh.warnings) {
		if (claimIssue(&result, &used, -1, warning) < 0) {
			result.append(ModelDecodeIssue{unclassified, ModelDecodeIssueSeverity::Damage, -1, warning, false});
			used.append(true);
		}
	}
	for (int surface = 0; surface < mesh.surfaces.size(); ++surface) {
		for (const QString& warning : mesh.surfaces.at(surface).warnings) {
			if (claimIssue(&result, &used, surface, warning) < 0) {
				result.append(ModelDecodeIssue{unclassified, ModelDecodeIssueSeverity::Damage, surface, warning, false});
				used.append(true);
			}
		}
	}
	return result;
}

void classifyModelDecodeIssues(ModelMesh* mesh)
{
	if (mesh) {
		mesh->issues = modelDecodeIssues(*mesh);
	}
}

QStringList modelDecodeBlockingIssues(const ModelMesh& mesh)
{
	QVector<ModelDecodeIssue> blocking;
	for (const ModelDecodeIssue& issue : modelDecodeIssues(mesh)) {
		if (blocks(issue)) {
			blocking.append(issue);
		}
	}
	QStringList names;
	for (const ModelSurface& surface : mesh.surfaces) {
		names << surface.name;
	}
	return modelDecodeIssueLines(blocking, names);
}

bool modelDecodeNeedsOmissionAcceptance(const ModelMesh& mesh)
{
	bool omission = false;
	for (const ModelDecodeIssue& issue : modelDecodeIssues(mesh)) {
		if (issue.severity == ModelDecodeIssueSeverity::Damage) {
			return false;
		}
		omission = omission || (issue.severity == ModelDecodeIssueSeverity::Omission && !issue.accepted);
	}
	return omission;
}

void acceptModelDecodeOmissions(ModelMesh* mesh)
{
	if (!mesh) {
		return;
	}
	classifyModelDecodeIssues(mesh);
	for (ModelDecodeIssue& issue : mesh->issues) {
		if (issue.severity == ModelDecodeIssueSeverity::Omission) {
			issue.accepted = true;
		}
	}
}

void adoptModelDecodeIssues(ModelMesh* mesh)
{
	if (!mesh) {
		return;
	}
	bool warned = !mesh->warnings.isEmpty();
	for (const ModelSurface& surface : mesh->surfaces) {
		warned = warned || !surface.warnings.isEmpty();
	}
	if (!warned) {
		return;
	}
	// Anything still blocking stays as it is, so validation refuses it.
	const QVector<ModelDecodeIssue> issues = modelDecodeIssues(*mesh);
	if (std::any_of(issues.cbegin(), issues.cend(), blocks)) {
		return;
	}
	mesh->issues = issues;
	mesh->warnings.clear();
	for (ModelSurface& surface : mesh->surfaces) {
		surface.warnings.clear();
	}
}

ModelDecodeIssueCounts countModelDecodeIssues(const QVector<ModelDecodeIssue>& issues)
{
	ModelDecodeIssueCounts counts;
	for (const ModelDecodeIssue& issue : issues) {
		switch (issue.severity) {
		case ModelDecodeIssueSeverity::Advisory:
			++counts.advisories;
			break;
		case ModelDecodeIssueSeverity::Omission:
			++counts.omissions;
			counts.accepted += issue.accepted ? 1 : 0;
			break;
		case ModelDecodeIssueSeverity::Damage:
			++counts.damage;
			break;
		}
	}
	return counts;
}

QJsonArray modelDecodeIssuesJson(const QVector<ModelDecodeIssue>& issues)
{
	QJsonArray array;
	for (const ModelDecodeIssue& issue : issues) {
		QJsonObject entry{
			{QStringLiteral("id"), issue.id},
			{QStringLiteral("severity"), modelDecodeIssueSeverityId(issue.severity)},
			{QStringLiteral("text"), issue.text},
		};
		if (issue.surface >= 0) {
			entry.insert(QStringLiteral("surface"), issue.surface);
		}
		if (issue.accepted) {
			entry.insert(QStringLiteral("accepted"), true);
		}
		array.append(entry);
	}
	return array;
}

bool parseModelDecodeIssues(const QJsonValue& value, int surfaceCount, QVector<ModelDecodeIssue>* issues, QString* error)
{
	const auto malformed = [error] {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioModelDecodeIssues",
				"The source's import issues must list advisories and accepted omissions, each with an id, a severity and text.");
		}
		return false;
	};
	if (!value.isArray() || value.toArray().size() > 4096) {
		return malformed();
	}
	QVector<ModelDecodeIssue> parsed;
	for (const QJsonValue& item : value.toArray()) {
		const QJsonObject entry = item.toObject();
		ModelDecodeIssue issue;
		if (!item.isObject() || !entry.value(QStringLiteral("id")).isString() || !entry.value(QStringLiteral("text")).isString()
			|| !modelDecodeIssueSeverityFromId(entry.value(QStringLiteral("severity")).toString(), &issue.severity)
			|| issue.severity == ModelDecodeIssueSeverity::Damage) {
			return malformed();
		}
		for (const QString& key : entry.keys()) {
			if (key != QStringLiteral("id") && key != QStringLiteral("severity") && key != QStringLiteral("text")
				&& key != QStringLiteral("surface") && key != QStringLiteral("accepted")) {
				return malformed();
			}
		}
		issue.id = entry.value(QStringLiteral("id")).toString();
		issue.text = entry.value(QStringLiteral("text")).toString();
		if (issue.id.isEmpty() || issue.id.size() > 128 || issue.text.size() > 4096) {
			return malformed();
		}
		if (entry.contains(QStringLiteral("surface"))) {
			const QJsonValue surface = entry.value(QStringLiteral("surface"));
			if (!surface.isDouble() || surface.toDouble() != surface.toInt(-1) || surface.toInt() < 0 || surface.toInt() >= surfaceCount) {
				return malformed();
			}
			issue.surface = surface.toInt();
		}
		if (entry.contains(QStringLiteral("accepted"))) {
			if (!entry.value(QStringLiteral("accepted")).isBool()) {
				return malformed();
			}
			issue.accepted = entry.value(QStringLiteral("accepted")).toBool();
		}
		// An editable source records only what an import allowed.
		if (issue.severity == ModelDecodeIssueSeverity::Omission && !issue.accepted) {
			return malformed();
		}
		parsed.append(issue);
	}
	if (issues) {
		*issues = std::move(parsed);
	}
	return true;
}

QStringList modelDecodeIssueLines(const QVector<ModelDecodeIssue>& issues, const QStringList& surfaceNames)
{
	QStringList lines;
	for (const ModelDecodeIssue& issue : issues) {
		QString severity = modelDecodeIssueSeverityName(issue.severity);
		if (issue.accepted) {
			severity = QCoreApplication::translate("VibeStudioModelDecodeIssues", "%1 (accepted)", "severity of an accepted omission").arg(severity);
		}
		if (issue.surface < 0) {
			lines << QCoreApplication::translate("VibeStudioModelDecodeIssues", "%1: %2", "severity: issue").arg(severity, issue.text);
		} else {
			const QString surface = issue.surface < surfaceNames.size() && !surfaceNames.at(issue.surface).isEmpty()
				? surfaceNames.at(issue.surface)
				: QString::number(issue.surface);
			lines << QCoreApplication::translate("VibeStudioModelDecodeIssues", "%1, surface %2: %3", "severity, surface name: issue")
				.arg(severity, surface, issue.text);
		}
	}
	return lines;
}

QString modelDecodeIssueSummary(const QVector<ModelDecodeIssue>& issues)
{
	const ModelDecodeIssueCounts counts = countModelDecodeIssues(issues);
	const int open = counts.omissions - counts.accepted;
	QStringList parts;
	if (counts.advisories > 0) {
		parts << QCoreApplication::translate("VibeStudioModelDecodeIssues", "%Ln advisory note(s)", nullptr, counts.advisories);
	}
	if (open > 0) {
		parts << QCoreApplication::translate("VibeStudioModelDecodeIssues", "%Ln omission(s) to accept", nullptr, open);
	}
	if (counts.accepted > 0) {
		parts << QCoreApplication::translate("VibeStudioModelDecodeIssues", "%Ln accepted omission(s)", nullptr, counts.accepted);
	}
	if (counts.damage > 0) {
		parts << QCoreApplication::translate("VibeStudioModelDecodeIssues", "%Ln damaged part(s)", nullptr, counts.damage);
	}
	return parts.join(QCoreApplication::translate("VibeStudioModelDecodeIssues", ", ", "list separator"));
}

} // namespace vibestudio
