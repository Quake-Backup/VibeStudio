#include "core/model_audit.h"

#include "core/model_decode_issues.h"
#include "core/model_document.h"
#include "core/model_skeleton.h"
#include "core/package_archive.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <memory>

namespace vibestudio {

namespace {

// Package formats a game installation keeps its models in.
const QStringList& packageSuffixes()
{
	static const QStringList suffixes{QStringLiteral("pak"), QStringLiteral("pk3"), QStringLiteral("pk4"), QStringLiteral("wad"),
		QStringLiteral("zip"), QStringLiteral("pk2"), QStringLiteral("dlc")};
	return suffixes;
}

// The formats written back for the round trip: the model's own.
QString roundTripFormat(const QString& formatId)
{
	static const QSet<QString> writable{QStringLiteral("mdl"), QStringLiteral("md2"), QStringLiteral("md3"), QStringLiteral("md5mesh"),
		QStringLiteral("iqm"), QStringLiteral("ase"), QStringLiteral("obj")};
	return writable.contains(formatId) ? formatId : QString();
}

QString suffixOf(const QString& path)
{
	const int slash = int(path.lastIndexOf(QLatin1Char('/')));
	const QString name = path.mid(slash + 1);
	const int dot = int(name.lastIndexOf(QLatin1Char('.')));
	return dot < 0 ? QString() : name.mid(dot + 1).toLower();
}

// Suffix -> the format ids that use it.
QHash<QString, QStringList> formatsBySuffix()
{
	QHash<QString, QStringList> map;
	for (const ModelFormatCapability& capability : modelFormatCapabilities()) {
		for (const QString& suffix : capability.suffixes) {
			map[suffix.toLower()].append(capability.id);
		}
	}
	return map;
}

double extent(const ModelMesh& mesh)
{
	ModelVec3 mins{0, 0, 0}, maxs{0, 0, 0};
	bool any = false;
	for (const ModelSurface& surface : mesh.surfaces) {
		for (const ModelFrameGeometry& frame : surface.frames) {
			for (const ModelVec3& p : frame.positions) {
				if (!any) {
					mins = maxs = p;
					any = true;
				}
				mins = ModelVec3{std::min(mins.x, p.x), std::min(mins.y, p.y), std::min(mins.z, p.z)};
				maxs = ModelVec3{std::max(maxs.x, p.x), std::max(maxs.y, p.y), std::max(maxs.z, p.z)};
			}
		}
	}
	return std::max({double(maxs.x - mins.x), double(maxs.y - mins.y), double(maxs.z - mins.z)});
}

// Half a quantisation step on each axis, as a distance. MDL stores one 8-bit
// grid for the whole model and MD2 one per frame (at most the model's
// extent); MD3 and MDC store 1/64 unit; the text and float formats print or
// store enough digits for a thousandth of the extent.
double tolerance(const QString& format, const ModelMesh& mesh)
{
	const double size = extent(mesh);
	if (format == QStringLiteral("mdl") || format == QStringLiteral("md2")) {
		return std::sqrt(3.0) * (size / 255.0) + 1e-4;
	}
	if (format == QStringLiteral("md3")) {
		return std::sqrt(3.0) * (1.0 / 64.0) + 1e-4;
	}
	return 1e-3 * std::max(1.0, size);
}

double distance(const ModelVec3& a, const ModelVec3& b)
{
	const double dx = double(a.x) - b.x, dy = double(a.y) - b.y, dz = double(a.z) - b.z;
	return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// The largest corner distance between two meshes with the same surfaces,
// triangles and frames, matching each triangle's corners up to rotation (the
// writers may start a triangle at another corner); -1 when they differ.
double compareGeometry(const ModelMesh& before, const ModelMesh& after, const ModelWorkControl& control)
{
	if (before.surfaces.size() != after.surfaces.size() || before.frames.size() != after.frames.size()) {
		return -1;
	}
	QString ignored;
	ModelWorkProgress work(control, ModelWorkPhase::Validating, &ignored);
	double worst = 0;
	for (int s = 0; s < before.surfaces.size(); ++s) {
		const ModelSurface& a = before.surfaces.at(s);
		const ModelSurface& b = after.surfaces.at(s);
		if (a.triangles.size() != b.triangles.size() || a.frames.size() != b.frames.size()) {
			return -1;
		}
		for (int f = 0; f < a.frames.size(); ++f) {
			const QVector<ModelVec3>& pa = a.frames.at(f).positions;
			const QVector<ModelVec3>& pb = b.frames.at(f).positions;
			for (int t = 0; t < a.triangles.size(); ++t) {
				if (!work.step()) {
					return -1;
				}
				const ModelTriangle& ta = a.triangles.at(t);
				const ModelTriangle& tb = b.triangles.at(t);
				const int ca[3] = {ta.a, ta.b, ta.c};
				const int cb[3] = {tb.a, tb.b, tb.c};
				double best = -1;
				for (int rotation = 0; rotation < 3; ++rotation) {
					double error = 0;
					for (int corner = 0; corner < 3; ++corner) {
						const int i = ca[corner], j = cb[(corner + rotation) % 3];
						if (i < 0 || j < 0 || i >= pa.size() || j >= pb.size()) {
							return -1;
						}
						error = std::max(error, distance(pa.at(i), pb.at(j)));
					}
					best = best < 0 ? error : std::min(best, error);
				}
				worst = std::max(worst, best);
			}
		}
	}
	return worst;
}

struct Container {
	QString label;
	std::shared_ptr<PackageArchive> archive;
	QVector<PackageEntry> entries;
};

QString verdictOf(const ModelMesh& mesh)
{
	if (!mesh.error.isEmpty()) {
		return QStringLiteral("failed");
	}
	const ModelDecodeIssueCounts counts = countModelDecodeIssues(modelDecodeIssues(mesh));
	if (counts.damage > 0) {
		return QStringLiteral("damaged");
	}
	if (counts.omissions > counts.accepted) {
		return QStringLiteral("accept-omissions");
	}
	return counts.advisories > 0 ? QStringLiteral("advisories") : QStringLiteral("clean");
}

void auditEntry(const PackageArchive& archive, const ModelAuditOptions& options, ModelAuditEntry* entry, const ModelWorkControl& control)
{
	QElapsedTimer timer;
	timer.start();
	const ModelMesh mesh = decodeModelMeshFromArchive(archive, entry->path, QString(), control);
	entry->decodeMilliseconds = double(timer.nsecsElapsed()) / 1.0e6;
	entry->format = mesh.formatId;
	entry->issues = modelDecodeIssues(mesh);
	entry->verdict = verdictOf(mesh);
	if (!mesh.error.isEmpty()) {
		entry->error = mesh.error;
		return;
	}
	entry->decoded = true;
	entry->surfaces = int(mesh.surfaces.size());
	entry->frames = int(mesh.frames.size());
	entry->vertices = mesh.vertexCount;
	entry->triangles = mesh.triangleCount;
	entry->joints = int(mesh.skeleton.joints.size());
	entry->clips = int(mesh.skeleton.clips.size());
	if (!mesh.geometryAvailable) {
		entry->editableError = QCoreApplication::translate("VibeStudioModelAudit", "Animation only: it opens through its mesh.");
		return;
	}
	// The editable verdict importEditableModel would give, accepting the
	// omissions when they are all that stands in the way.
	ModelMesh editable = mesh;
	entry->needsAcceptance = modelDecodeNeedsOmissionAcceptance(editable);
	if (entry->needsAcceptance) {
		acceptModelDecodeOmissions(&editable);
	}
	QString fitError;
	if (!fitEditableModelBudget(&editable, nullptr, &fitError, control)) {
		entry->editableError = fitError;
		return;
	}
	const QStringList refused = validateEditableModel(editable, control);
	if (!refused.isEmpty()) {
		entry->editableError = refused.join(QLatin1Char(' '));
		return;
	}
	entry->editable = true;
	updateEditableModelMetadata(&editable);
	const QString format = roundTripFormat(mesh.formatId);
	if (!options.roundTrip || format.isEmpty()) {
		return;
	}
	ModelAuditRoundTrip& trip = entry->roundTrip;
	trip.attempted = true;
	trip.format = format;
	trip.tolerance = tolerance(format, editable);
	QString exportError;
	const QByteArray bytes = exportEditableModel(editable, format, 0, &exportError, control);
	if (bytes.isEmpty()) {
		trip.error = exportError;
		return;
	}
	trip.written = true;
	trip.bytes = bytes.size();
	ModelMesh back = decodeModelMesh(QStringLiteral("audit-round-trip.") + format, bytes, nullptr, control);
	if (!back.error.isEmpty()) {
		trip.error = back.error;
		return;
	}
	trip.decoded = true;
	trip.triangleDelta = back.triangleCount - editable.triangleCount;
	trip.frameDelta = int(back.frames.size()) - int(editable.frames.size());
	trip.maxPositionError = compareGeometry(editable, back, control);
}

QJsonObject issueCountsJson(const QVector<ModelDecodeIssue>& issues)
{
	const ModelDecodeIssueCounts counts = countModelDecodeIssues(issues);
	return QJsonObject{{QStringLiteral("advisory"), counts.advisories}, {QStringLiteral("omission"), counts.omissions},
		{QStringLiteral("damage"), counts.damage}};
}

// Rounded so platform float differences do not change the JSON.
double rounded(double value)
{
	return std::round(value * 1.0e6) / 1.0e6;
}

} // namespace

QStringList modelAuditSuffixes()
{
	QStringList suffixes = formatsBySuffix().keys();
	std::sort(suffixes.begin(), suffixes.end());
	return suffixes;
}

bool auditModels(const QString& path, const ModelAuditOptions& options, ModelAuditReport* report, QString* error, const ModelWorkControl& control)
{
	if (!report) {
		return false;
	}
	*report = {};
	const QFileInfo info(path);
	report->source = info.absoluteFilePath();
	const auto fail = [error](const QString& message) {
		if (error) {
			*error = message;
		}
		return false;
	};
	if (!info.exists()) {
		return fail(QCoreApplication::translate("VibeStudioModelAudit", "%1 does not exist.").arg(QDir::toNativeSeparators(path)));
	}
	const auto cancelled = [&control]() { return control.cancelled && control.cancelled(); };
	PackageReadControl readControl;
	readControl.isCancelled = control.cancelled;
	const QHash<QString, QStringList> bySuffix = formatsBySuffix();
	const auto wanted = [&](const QString& virtualPath) {
		const QStringList ids = bySuffix.value(suffixOf(virtualPath));
		if (ids.isEmpty()) {
			return false;
		}
		if (options.formats.isEmpty()) {
			return true;
		}
		return std::any_of(ids.cbegin(), ids.cend(), [&](const QString& id) { return options.formats.contains(id); });
	};

	// Open every container first so progress knows the total.
	QVector<Container> containers;
	QStringList packages;
	if (info.isDir()) {
		QDirIterator it(info.absoluteFilePath(), QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
		while (it.hasNext()) {
			const QString file = it.next();
			if (packageSuffixes().contains(QFileInfo(file).suffix().toLower())) {
				packages << file;
			}
			if (cancelled()) {
				report->cancelled = true;
				return true;
			}
		}
		std::sort(packages.begin(), packages.end());
		if (packages.size() > options.maxPackages) {
			report->problems << QCoreApplication::translate("VibeStudioModelAudit", "%1 packages were found; only the first %2 were audited.")
				.arg(packages.size()).arg(options.maxPackages);
			packages = packages.mid(0, options.maxPackages);
		}
		Container folder;
		folder.label = QDir::toNativeSeparators(info.absoluteFilePath());
		folder.archive = std::make_shared<PackageArchive>();
		QString problem;
		if (!folder.archive->load(info.absoluteFilePath(), &problem, readControl)) {
			report->problems << QCoreApplication::translate("VibeStudioModelAudit", "%1: %2").arg(folder.label, problem);
		} else {
			containers.append(folder);
		}
	} else {
		packages << info.absoluteFilePath();
	}
	for (const QString& package : std::as_const(packages)) {
		if (cancelled()) {
			report->cancelled = true;
			return true;
		}
		Container container;
		container.label = QDir::toNativeSeparators(package);
		container.archive = std::make_shared<PackageArchive>();
		QString problem;
		if (!container.archive->load(package, &problem, readControl)) {
			report->problems << QCoreApplication::translate("VibeStudioModelAudit", "%1: %2").arg(container.label, problem);
			continue;
		}
		containers.append(container);
	}
	if (containers.isEmpty()) {
		return fail(report->problems.isEmpty() ? QCoreApplication::translate("VibeStudioModelAudit", "Nothing could be opened at %1.").arg(QDir::toNativeSeparators(path))
											   : report->problems.join(QLatin1Char('\n')));
	}
	int total = 0;
	for (Container& container : containers) {
		report->containers << container.label;
		for (const PackageEntry& entry : container.archive->entries()) {
			if (entry.kind == PackageEntryKind::File && wanted(entry.virtualPath)) {
				container.entries.append(entry);
			}
		}
		std::sort(container.entries.begin(), container.entries.end(),
			[](const PackageEntry& a, const PackageEntry& b) { return a.virtualPath.compare(b.virtualPath, Qt::CaseInsensitive) < 0; });
		total += int(container.entries.size());
	}

	int done = 0;
	for (const Container& container : std::as_const(containers)) {
		for (const PackageEntry& packageEntry : container.entries) {
			if (cancelled()) {
				report->cancelled = true;
				break;
			}
			if (options.progress) {
				options.progress(done, total, packageEntry.virtualPath);
			}
			++done;
			ModelAuditEntry entry;
			entry.container = container.label;
			entry.path = packageEntry.virtualPath;
			entry.sizeBytes = qint64(packageEntry.sizeBytes);
			if (int(report->entries.size()) >= options.maxEntries) {
				++report->skippedEntries;
				continue;
			}
			if (qint64(packageEntry.sizeBytes) > options.maxEntryBytes) {
				entry.skipped = true;
				entry.skipReason = QCoreApplication::translate("VibeStudioModelAudit", "Larger than %1 MiB; not read.").arg(options.maxEntryBytes / (1024 * 1024));
				entry.verdict = QStringLiteral("skipped");
				report->entries.append(entry);
				continue;
			}
			auditEntry(*container.archive, options, &entry, control);
			if (cancelled()) {
				report->cancelled = true;
				break;
			}
			// A suffix shared by several formats (.mdl) is filtered by the
			// format the bytes turned out to be.
			if (!options.formats.isEmpty() && !entry.format.isEmpty() && entry.format != QStringLiteral("unknown") && !options.formats.contains(entry.format)) {
				continue;
			}
			report->entries.append(entry);
		}
		if (report->cancelled) {
			break;
		}
	}
	if (options.progress) {
		options.progress(done, total, QString());
	}

	// One summary per format, in id order.
	QHash<QString, ModelAuditFormatSummary> summaries;
	for (const ModelAuditEntry& entry : std::as_const(report->entries)) {
		const QString key = entry.format.isEmpty() ? QStringLiteral("unknown") : entry.format;
		ModelAuditFormatSummary& summary = summaries[key];
		summary.format = key;
		++summary.entries;
		if (entry.skipped) {
			++summary.skipped;
			continue;
		}
		summary.decoded += entry.decoded ? 1 : 0;
		summary.failed += entry.decoded ? 0 : 1;
		summary.clean += entry.verdict == QStringLiteral("clean") ? 1 : 0;
		summary.advisories += entry.verdict == QStringLiteral("advisories") ? 1 : 0;
		summary.needsAcceptance += entry.verdict == QStringLiteral("accept-omissions") ? 1 : 0;
		summary.damaged += entry.verdict == QStringLiteral("damaged") ? 1 : 0;
		summary.editable += entry.editable ? 1 : 0;
		summary.decodeMilliseconds += entry.decodeMilliseconds;
		if (entry.roundTrip.attempted) {
			++summary.roundTrips;
			summary.roundTripsWithin += entry.roundTrip.withinTolerance() ? 1 : 0;
			summary.maxPositionError = std::max(summary.maxPositionError, entry.roundTrip.maxPositionError);
		}
	}
	QStringList keys = summaries.keys();
	std::sort(keys.begin(), keys.end());
	for (const QString& key : std::as_const(keys)) {
		report->formats.append(summaries.value(key));
	}
	return true;
}

QJsonObject modelAuditJson(const ModelAuditReport& report, bool timings)
{
	QVector<ModelAuditEntry> entries = report.entries;
	std::sort(entries.begin(), entries.end(), [](const ModelAuditEntry& a, const ModelAuditEntry& b) {
		return a.container != b.container ? a.container < b.container : a.path.compare(b.path, Qt::CaseInsensitive) < 0;
	});
	QJsonArray entryArray;
	for (const ModelAuditEntry& entry : std::as_const(entries)) {
		QJsonObject object{{QStringLiteral("container"), entry.container}, {QStringLiteral("path"), entry.path},
			{QStringLiteral("format"), entry.format}, {QStringLiteral("bytes"), double(entry.sizeBytes)},
			{QStringLiteral("verdict"), entry.verdict}, {QStringLiteral("decoded"), entry.decoded}};
		if (entry.skipped) {
			object.insert(QStringLiteral("skipped"), entry.skipReason);
		}
		if (!entry.error.isEmpty()) {
			object.insert(QStringLiteral("error"), entry.error);
		}
		if (entry.decoded) {
			object.insert(QStringLiteral("surfaces"), entry.surfaces);
			object.insert(QStringLiteral("frames"), entry.frames);
			object.insert(QStringLiteral("vertices"), entry.vertices);
			object.insert(QStringLiteral("triangles"), entry.triangles);
			if (entry.joints > 0) {
				object.insert(QStringLiteral("joints"), entry.joints);
				object.insert(QStringLiteral("clips"), entry.clips);
			}
			object.insert(QStringLiteral("editable"), entry.editable);
			object.insert(QStringLiteral("needsAcceptance"), entry.needsAcceptance);
			if (!entry.editableError.isEmpty()) {
				object.insert(QStringLiteral("editableError"), entry.editableError);
			}
		}
		object.insert(QStringLiteral("issueCounts"), issueCountsJson(entry.issues));
		object.insert(QStringLiteral("issues"), modelDecodeIssuesJson(entry.issues));
		if (timings) {
			object.insert(QStringLiteral("decodeMilliseconds"), rounded(entry.decodeMilliseconds));
		}
		if (entry.roundTrip.attempted) {
			const ModelAuditRoundTrip& trip = entry.roundTrip;
			QJsonObject roundTrip{{QStringLiteral("format"), trip.format}, {QStringLiteral("written"), trip.written},
				{QStringLiteral("decoded"), trip.decoded}, {QStringLiteral("withinTolerance"), trip.withinTolerance()},
				{QStringLiteral("tolerance"), rounded(trip.tolerance)}};
			if (trip.written) {
				roundTrip.insert(QStringLiteral("bytes"), double(trip.bytes));
			}
			if (trip.decoded) {
				roundTrip.insert(QStringLiteral("triangleDelta"), trip.triangleDelta);
				roundTrip.insert(QStringLiteral("frameDelta"), trip.frameDelta);
				roundTrip.insert(QStringLiteral("maxPositionError"), rounded(trip.maxPositionError));
			}
			if (!trip.error.isEmpty()) {
				roundTrip.insert(QStringLiteral("error"), trip.error);
			}
			object.insert(QStringLiteral("roundTrip"), roundTrip);
		}
		entryArray.append(object);
	}
	QJsonArray formatArray;
	for (const ModelAuditFormatSummary& summary : report.formats) {
		QJsonObject object{{QStringLiteral("format"), summary.format}, {QStringLiteral("entries"), summary.entries},
			{QStringLiteral("skipped"), summary.skipped}, {QStringLiteral("decoded"), summary.decoded}, {QStringLiteral("failed"), summary.failed},
			{QStringLiteral("clean"), summary.clean}, {QStringLiteral("advisories"), summary.advisories},
			{QStringLiteral("acceptOmissions"), summary.needsAcceptance}, {QStringLiteral("damaged"), summary.damaged},
			{QStringLiteral("editable"), summary.editable}, {QStringLiteral("roundTrips"), summary.roundTrips},
			{QStringLiteral("roundTripsWithinTolerance"), summary.roundTripsWithin},
			{QStringLiteral("maxPositionError"), rounded(summary.maxPositionError)}};
		if (timings) {
			object.insert(QStringLiteral("decodeMilliseconds"), rounded(summary.decodeMilliseconds));
		}
		formatArray.append(object);
	}
	return QJsonObject{{QStringLiteral("schema"), QStringLiteral("vibestudio.model-audit")}, {QStringLiteral("version"), 1},
		{QStringLiteral("source"), report.source}, {QStringLiteral("containers"), QJsonArray::fromStringList(report.containers)},
		{QStringLiteral("cancelled"), report.cancelled}, {QStringLiteral("skippedEntries"), report.skippedEntries},
		{QStringLiteral("problems"), QJsonArray::fromStringList(report.problems)}, {QStringLiteral("formats"), formatArray},
		{QStringLiteral("entries"), entryArray}};
}

QStringList modelAuditLines(const ModelAuditReport& report)
{
	QStringList lines;
	int entries = 0;
	for (const ModelAuditFormatSummary& summary : report.formats) {
		entries += summary.entries;
		lines << QCoreApplication::translate("VibeStudioModelAudit",
			"%1: %2 model(s); %3 decoded, %4 failed; %5 clean, %6 with advisories, %7 need omissions accepted, %8 damaged; %9 editable.")
			.arg(summary.format).arg(summary.entries).arg(summary.decoded).arg(summary.failed).arg(summary.clean).arg(summary.advisories)
			.arg(summary.needsAcceptance).arg(summary.damaged).arg(summary.editable);
		if (summary.roundTrips > 0) {
			lines << QCoreApplication::translate("VibeStudioModelAudit", "  Round trips: %1 of %2 within tolerance; largest corner error %3.")
				.arg(summary.roundTripsWithin).arg(summary.roundTrips).arg(summary.maxPositionError, 0, 'g', 4);
		}
	}
	for (const ModelAuditEntry& entry : report.entries) {
		if (entry.verdict == QStringLiteral("clean") && (!entry.roundTrip.attempted || entry.roundTrip.withinTolerance())) {
			continue;
		}
		QString detail = entry.error;
		if (detail.isEmpty() && entry.skipped) {
			detail = entry.skipReason;
		}
		if (detail.isEmpty() && !entry.issues.isEmpty()) {
			detail = modelDecodeIssueLines({entry.issues.first()}).value(0);
		}
		if (detail.isEmpty() && entry.roundTrip.attempted && !entry.roundTrip.withinTolerance()) {
			detail = entry.roundTrip.error.isEmpty()
				? QCoreApplication::translate("VibeStudioModelAudit", "The %1 round trip moved a corner by %2 (tolerance %3).")
					  .arg(entry.roundTrip.format).arg(entry.roundTrip.maxPositionError, 0, 'g', 4).arg(entry.roundTrip.tolerance, 0, 'g', 4)
				: entry.roundTrip.error;
		}
		lines << QCoreApplication::translate("VibeStudioModelAudit", "%1 (%2): %3 — %4").arg(entry.path, entry.format, entry.verdict, detail);
	}
	if (report.skippedEntries > 0) {
		lines << QCoreApplication::translate("VibeStudioModelAudit", "%1 more model(s) were not audited (the entry limit).").arg(report.skippedEntries);
	}
	for (const QString& problem : report.problems) {
		lines << problem;
	}
	if (report.cancelled) {
		lines << QCoreApplication::translate("VibeStudioModelAudit", "The audit was cancelled after %1 model(s).").arg(entries);
	}
	return lines;
}

} // namespace vibestudio
