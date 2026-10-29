#include "core/model_frame_import.h"

#include "core/model_document.h"
#include "core/model_file_io.h"
#include "core/model_frame_export.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QHash>

#include <algorithm>
#include <cmath>

namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelFrameTools)
};

bool fail(QString *error, const QString &message)
{
	if (error)
		*error = message;
	return false;
}

double distance(ModelVec3 a, ModelVec3 b)
{
	const double x = double(a.x) - b.x, y = double(a.y) - b.y, z = double(a.z) - b.z;
	return std::sqrt(x * x + y * y + z * z);
}

bool nameOk(const QString &name)
{
	return !name.isEmpty() && name == name.trimmed() && name.size() <= 128 &&
		   std::none_of(name.cbegin(), name.cend(), [](QChar c) { return c.isNull() || c.category() == QChar::Other_Control; });
}

// For each target vertex of `target`, the source vertex it takes its pose
// from; false with a translated reason naming the first mismatch.
bool matchSurface(const ModelSurface &target, const ModelSurface &source, const ModelFrameImportOptions &options, int referenceFrame,
				  const QString &sourceName, QVector<int> *map, QString *error, ModelWorkProgress &work)
{
	map->fill(-1, target.vertexCount);
	if (options.match == ModelFrameMatch::Exact)
	{
		if (source.vertexCount != target.vertexCount || source.triangles.size() != target.triangles.size())
			return fail(error, Text::tr("%1, surface %2: %3 vertices and %4 triangles, where the model has %5 and %6. Exact matching needs "
										"identical vertex and triangle lists.")
								   .arg(sourceName, target.name)
								   .arg(source.vertexCount)
								   .arg(source.triangles.size())
								   .arg(target.vertexCount)
								   .arg(target.triangles.size()));
		for (int t = 0; t < target.triangles.size(); ++t)
		{
			const auto &a = target.triangles[t], &b = source.triangles[t];
			if (a.a != b.a || a.b != b.b || a.c != b.c)
				return fail(error, Text::tr("%1, surface %2: triangle %3 uses different vertices. Try corner matching.")
									   .arg(sourceName, target.name)
									   .arg(t));
		}
		for (int v = 0; v < target.vertexCount; ++v)
			(*map)[v] = v;
		return true;
	}
	if (options.match == ModelFrameMatch::Corners)
	{
		if (source.triangles.size() != target.triangles.size())
			return fail(error, Text::tr("%1, surface %2: %3 triangles, where the model has %4. Export every frame with the same faces in the "
										"same order.")
								   .arg(sourceName, target.name)
								   .arg(source.triangles.size())
								   .arg(target.triangles.size()));
		for (int t = 0; t < target.triangles.size(); ++t)
		{
			const auto &a = target.triangles[t], &b = source.triangles[t];
			const int targets[]{a.a, a.b, a.c}, sources[]{b.a, b.b, b.c};
			for (int k = 0; k < 3; ++k)
			{
				if (!work.step())
					return false;
				int &mapped = (*map)[targets[k]];
				if (mapped < 0)
				{
					mapped = sources[k];
					continue;
				}
				if (mapped == sources[k])
					continue;
				// The source split this vertex (by normal or UV): its copies
				// must sit at the same place in every frame.
				for (const auto &frame : source.frames)
					if (distance(frame.positions[mapped], frame.positions[sources[k]]) > 1e-4)
						return fail(error, Text::tr("%1, surface %2: triangle %3 does not match the model's corners. Export every frame "
													"with the same faces in the same order, or try nearest matching.")
											   .arg(sourceName, target.name)
											   .arg(t));
			}
		}
		for (int v = 0; v < target.vertexCount; ++v)
			if ((*map)[v] < 0)
				return fail(error, Text::tr("Surface %1: vertex %2 belongs to no face, so corner matching cannot place it. Remove unused "
											"vertices first.")
									   .arg(target.name)
									   .arg(v));
		return true;
	}
	// Nearest: same UV, closest position within the tolerance.
	QHash<QPair<qint64, qint64>, QVector<int>> buckets;
	const auto key = [](ModelTexCoord uv) { return QPair<qint64, qint64>(std::llround(uv.u * 4096.0), std::llround(uv.v * 4096.0)); };
	for (int v = 0; v < source.vertexCount; ++v)
		buckets[key(source.texCoords[v])].append(v);
	for (int v = 0; v < target.vertexCount; ++v)
	{
		if (!work.step())
			return false;
		const auto uv = target.texCoords[v];
		const auto p = target.frames[referenceFrame].positions[v];
		const auto centre = key(uv);
		double best = options.tolerance;
		for (qint64 du = -1; du <= 1; ++du)
			for (qint64 dv = -1; dv <= 1; ++dv)
				for (int candidate : buckets.value({centre.first + du, centre.second + dv}))
				{
					const auto other = source.texCoords[candidate];
					if (std::abs(other.u - uv.u) > 1e-4 || std::abs(other.v - uv.v) > 1e-4)
						continue;
					const double d = distance(source.frames[0].positions[candidate], p);
					if (d <= best)
					{
						best = d;
						(*map)[v] = candidate;
					}
				}
		if ((*map)[v] < 0)
			return fail(error, Text::tr("%1, surface %2: no source vertex has the UV and position of vertex %3 within %4 units. Check "
										"the reference frame and tolerance.")
								   .arg(sourceName, target.name)
								   .arg(v)
								   .arg(options.tolerance));
	}
	return true;
}
} // namespace

bool ModelFrameImportOptions::isDefault() const
{
	const ModelFrameImportOptions defaults{};
	return sources.isEmpty() && sourceNames.isEmpty() && match == defaults.match && tolerance == defaults.tolerance &&
		   referenceFrame == defaults.referenceFrame && insertAfter == defaults.insertAfter && replaceFirst == defaults.replaceFirst &&
		   clipName.isEmpty() && clipFps == defaults.clipFps;
}

QStringList modelFrameSourceSuffixes()
{
	return {QStringLiteral("obj"), QStringLiteral("md2"), QStringLiteral("md3"), QStringLiteral("mdl"), QStringLiteral("mesh.json")};
}

QStringList naturalSortedModelFramePaths(const QStringList &paths)
{
	// Digit runs compare by value, everything else case-insensitively, so
	// walk_2 sorts before walk_10 on every platform and locale.
	const auto less = [](const QString &left, const QString &right)
	{
		const auto a = QFileInfo(left).fileName(), b = QFileInfo(right).fileName();
		qsizetype i = 0, j = 0;
		while (i < a.size() && j < b.size())
		{
			if (a[i].isDigit() && b[j].isDigit())
			{
				qsizetype ei = i, ej = j;
				while (ei < a.size() && a[ei].isDigit())
					++ei;
				while (ej < b.size() && b[ej].isDigit())
					++ej;
				auto da = a.mid(i, ei - i), db = b.mid(j, ej - j);
				while (da.size() > 1 && da.front() == QLatin1Char('0'))
					da.remove(0, 1);
				while (db.size() > 1 && db.front() == QLatin1Char('0'))
					db.remove(0, 1);
				if (da.size() != db.size())
					return da.size() < db.size();
				if (da != db)
					return da < db;
				i = ei;
				j = ej;
				continue;
			}
			const auto ca = a[i].toCaseFolded(), cb = b[j].toCaseFolded();
			if (ca != cb)
				return ca < cb;
			++i;
			++j;
		}
		return a.size() - i < b.size() - j;
	};
	auto sorted = paths;
	std::stable_sort(sorted.begin(), sorted.end(), less);
	return sorted;
}

bool readModelFrameSources(const QStringList &paths, const ModelObjImportOptions &obj, const QString &protectedPath,
						   QVector<ModelMesh> *sources, QStringList *names, QString *error, const ModelWorkControl &control)
{
	if (!sources || !names)
		return false;
	sources->clear();
	names->clear();
	if (paths.isEmpty() || paths.size() > modelDocumentMaxFrames)
		return fail(error, Text::tr("Choose from 1 to 1024 files with frames to import."));
	int frames = 0;
	for (const auto &path : paths)
	{
		const QFileInfo info(path);
		const auto suffix = info.fileName().toLower();
		const auto suffixes = modelFrameSourceSuffixes();
		if (std::none_of(suffixes.cbegin(), suffixes.cend(), [&](const QString &allowed) { return suffix.endsWith(QLatin1Char('.') + allowed); }))
			return fail(error, Text::tr("%1 is not an OBJ, MD2, MD3, MDL or editable source.").arg(info.fileName()));
		if (!protectedPath.isEmpty() && modelPathsReferToSameFile(path, protectedPath))
			return fail(error, Text::tr("%1 is the model's own source or output; choose other files to import frames from.").arg(info.fileName()));
		QByteArray bytes;
		QString problem;
		if (!readModelFile(path, &bytes, &problem, control))
			return fail(error, Text::tr("%1: %2").arg(info.fileName(), problem));
		ModelImportOptions options;
		if (suffix.endsWith(QStringLiteral(".obj")))
			options.obj = obj;
		ModelMesh mesh;
		if (!importEditableModel(info.absoluteFilePath(), bytes, &mesh, options, nullptr, &problem, nullptr, control))
			return fail(error, Text::tr("%1: %2").arg(info.fileName(), problem));
		frames += int(mesh.frames.size());
		if (frames > modelDocumentMaxFrames)
			return fail(error, Text::tr("The files hold more than 1024 frames together."));
		sources->append(std::move(mesh));
		names->append(info.fileName());
	}
	return true;
}

int modelFrameSourceCount(const QVector<ModelMesh> &sources)
{
	int count = 0;
	for (const auto &source : sources)
		count += int(source.frames.size());
	return count;
}

bool applyModelFrameImportEdit(ModelMesh *candidate, const ModelEdit &edit, QString *error, const ModelWorkControl &control)
{
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	if (!candidate || edit.kind != ModelEditKind::ImportFrames || !work.check())
		return fail(error, Text::tr("Choose files to import frames from."));
	const auto &o = edit.frameImport;
	if (o.sources.isEmpty() || (!o.sourceNames.isEmpty() && o.sourceNames.size() != o.sources.size()))
		return fail(error, Text::tr("Choose files to import frames from."));
	if (int(o.match) < 0 || int(o.match) > int(ModelFrameMatch::Nearest) || !std::isfinite(o.tolerance) || o.tolerance <= 0 ||
		o.tolerance > 1000)
		return fail(error, Text::tr("Choose exact, corner or nearest matching, with a tolerance above zero and at most 1000 units."));
	if (o.referenceFrame < 0 || o.referenceFrame >= candidate->frames.size())
		return fail(error, Text::tr("Choose an existing reference frame."));
	if (!std::isfinite(o.clipFps) || o.clipFps < 0 || o.clipFps > 1000 || (o.clipFps > 0 && o.clipFps < .001) ||
		(o.clipFps > 0 && o.clipName.isEmpty()))
		return fail(error, Text::tr("A clip FPS needs a clip name, and must be zero (unspecified) or from 0.001 to 1000."));
	if (!o.clipName.isEmpty())
	{
		if (!nameOk(o.clipName))
			return fail(error, Text::tr("Use a clip name of 1–128 characters without surrounding spaces or control characters."));
		for (const auto &clip : std::as_const(candidate->animations))
			if (clip.name == o.clipName)
				return fail(error, Text::tr("That animation clip name is already used."));
		if (candidate->animations.size() >= modelDocumentMaxAnimations)
			return fail(error, Text::tr("A model supports at most 1024 animation clips."));
	}
	if (edit.frame != -1)
		return fail(error, Text::tr("Frame import uses Insert after or Replace from, not the displayed frame."));
	QVector<int> order;
	QVector<bool> generated;
	if (!modelFrameRangeOrder(*candidate, edit, &order, error, &generated))
		return false;
	// Match every source before changing anything.
	QVector<QVector<QVector<int>>> mapsOfSource;
	for (int s = 0; s < o.sources.size(); ++s)
	{
		const auto &source = o.sources[s];
		const auto name = o.sourceNames.value(s, Text::tr("Source %1").arg(s + 1));
		QVector<QVector<int>> maps;
		for (int t = 0; t < candidate->surfaces.size(); ++t)
		{
			const auto &target = candidate->surfaces[t];
			int match = -1;
			for (int i = 0; i < source.surfaces.size() && match < 0; ++i)
				if (source.surfaces[i].name == target.name)
					match = i;
			if (match < 0 && candidate->surfaces.size() == source.surfaces.size())
				match = t;
			if (match < 0)
				return fail(error, Text::tr("%1 has no surface named %2, and its surface count differs from the model's.").arg(name, target.name));
			QVector<int> map;
			if (!matchSurface(target, source.surfaces[match], o, o.referenceFrame, name, &map, error, work))
				return false;
			// Keep the source surface index in front of the vertex map.
			map.prepend(match);
			maps.append(map);
		}
		mapsOfSource.append(maps);
	}
	const int added = modelFrameSourceCount(o.sources);
	const int first = o.replaceFirst >= 0 ? o.replaceFirst : (o.insertAfter < 0 ? int(candidate->frames.size()) - 1 : o.insertAfter) + 1;
	ModelFrameRemap remap;
	remap.order = order;
	if (o.replaceFirst >= 0)
	{
		remap.animations = candidate->animations;
		remap.explicitMdlGroups = true;
		remap.mdlGroups = candidate->mdl.frameGroups;
	}
	else
	{
		// Like in-between frames: a clip the new frames land strictly inside
		// grows; later clips move.
		for (auto clip : candidate->animations)
		{
			if (clip.firstFrame >= first)
				clip.firstFrame += added;
			else if (clip.firstFrame + clip.frameCount > first)
				clip.frameCount += added;
			remap.animations.append(clip);
		}
	}
	if (!o.clipName.isEmpty())
		remap.animations.append(ModelAnimation{o.clipName, first, added, o.clipFps});
	if (!remapModelFrames(candidate, remap, error, control))
		return false;
	const QString stem = o.clipName.isEmpty() ? QString() : modelClipFrameStem(o.clipName, added);
	int frame = first;
	for (int s = 0; s < o.sources.size(); ++s)
	{
		const auto &source = o.sources[s];
		const auto &maps = mapsOfSource[s];
		for (int k = 0; k < source.frames.size(); ++k, ++frame)
		{
			if (!work.step())
				return false;
			for (int t = 0; t < candidate->surfaces.size(); ++t)
			{
				const auto &map = maps[t];
				const auto &from = source.surfaces[map[0]].frames[k];
				auto &to = candidate->surfaces[t].frames[frame];
				for (int v = 0; v < to.positions.size(); ++v)
				{
					to.positions[v] = from.positions[map[v + 1]];
					to.normals[v] = from.normals[map[v + 1]];
				}
			}
			auto &info = candidate->frames[frame];
			info.origin = source.frames[k].origin;
			if (!stem.isEmpty())
				info.name = modelClipFrameName(stem, frame - first + 1);
			else if (source.frames.size() > 1 || o.sourceNames.isEmpty())
				info.name = source.frames[k].name;
			else
			{
				auto base = o.sourceNames[s];
				const int dot = base.indexOf(QLatin1Char('.'));
				info.name = dot > 0 ? base.left(dot) : base;
			}
			for (auto &tag : candidate->tags)
			{
				if (tag.frameIndex != frame)
					continue;
				const auto found = std::find_if(source.tags.cbegin(), source.tags.cend(),
												[&](const ModelTag &other) { return other.name == tag.name && other.frameIndex == k; });
				if (found == source.tags.cend())
					continue;
				tag.origin = found->origin;
				std::copy(std::begin(found->axis), std::end(found->axis), std::begin(tag.axis));
			}
		}
	}
	return work.check();
}

bool exportModelFrameObjSequence(const ModelMesh &mesh, int first, int last, const QString &pattern, bool overwrite, bool dryRun,
								 const QStringList &protectedFiles, QStringList *written, QString *error, const ModelWorkControl &control)
{
	if (written)
		written->clear();
	if (first < 0 || last < first || last >= mesh.frames.size())
		return fail(error, Text::tr("Choose a frame range inside the model, with the first frame no later than the last."));
	if (!pattern.endsWith(QStringLiteral(".obj"), Qt::CaseInsensitive))
		return fail(error, Text::tr("Name the OBJ files with a pattern ending in .obj, such as walk_###.obj."));
	const int digits = std::max(3, int(QString::number(last).size()));
	const auto fileFor = [&](int frame)
	{
		const int end = int(pattern.lastIndexOf(QLatin1Char('#')));
		if (end < 0)
			return pattern.left(pattern.size() - 4) + QLatin1Char('_') + QString::number(frame).rightJustified(digits, QLatin1Char('0')) +
				   pattern.right(4);
		int start = end;
		while (start > 0 && pattern[start - 1] == QLatin1Char('#'))
			--start;
		return pattern.left(start) + QString::number(frame).rightJustified(end - start + 1, QLatin1Char('0')) + pattern.mid(end + 1);
	};
	QStringList files;
	for (int frame = first; frame <= last; ++frame)
	{
		const auto file = fileFor(frame);
		if (files.contains(file))
			return fail(error, Text::tr("The pattern gives two frames the same file name. Use enough # characters for the frame numbers."));
		files.append(file);
	}
	// The guarded export expects a decoded model; an editable source has no
	// native format of its own.
	ModelMesh exportable = mesh;
	if (exportable.format == ModelMeshFormat::Unknown)
		exportable.format = ModelMeshFormat::WavefrontObj;
	// Review every destination before writing any.
	for (int pass = 0; pass < (dryRun ? 1 : 2); ++pass)
		for (int i = 0; i < files.size(); ++i)
		{
			ModelFrameExportRequest request;
			request.frame = first + i;
			request.outputPath = files[i];
			request.protectedFiles = protectedFiles;
			request.overwrite = overwrite;
			request.dryRun = pass == 0;
			const auto result = exportModelFrame(exportable, request, control);
			if (!result.error.isEmpty())
				return fail(error, Text::tr("%1: %2").arg(QFileInfo(files[i]).fileName(), result.error));
		}
	if (written)
		*written = files;
	return true;
}
} // namespace vibestudio
