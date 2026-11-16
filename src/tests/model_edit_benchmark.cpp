// Explicit performance harness for the model editor's document layer: timings
// are evidence for a build on one machine, not portable CI thresholds.
//
//   model_edit_benchmark [cells] [frames]
//
// builds a waved grid of cells x cells quads (two triangles each) with the
// given number of vertex-animation frames and prints one JSON object of
// milliseconds per operation.
#include "core/model_design.h"
#include "core/model_document.h"
#include "core/model_fingerprint.h"
#include "core/model_geometric_topology.h"
#include "core/model_topology.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>

#include <cmath>
#include <iostream>

using namespace vibestudio;

namespace
{
ModelMesh wavedGrid(int cells, int frames)
{
	ModelDesign design;
	ModelDesignPart part;
	part.primitive = QStringLiteral("plane");
	design.parts << part;
	auto mesh = buildModelDesignMesh(design);
	auto &surface = mesh.surfaces[0];
	surface.triangles.clear();
	surface.texCoords.clear();
	surface.vertexCount = (cells + 1) * (cells + 1);
	const auto at = [cells](int i, int j) { return j * (cells + 1) + i; };
	for (int j = 0; j <= cells; ++j)
	{
		for (int i = 0; i <= cells; ++i)
			surface.texCoords.append({float(i) / float(cells), float(j) / float(cells)});
	}
	for (int j = 0; j < cells; ++j)
	{
		for (int i = 0; i < cells; ++i)
		{
			surface.triangles.append({at(i, j), at(i + 1, j), at(i + 1, j + 1)});
			surface.triangles.append({at(i, j), at(i + 1, j + 1), at(i, j + 1)});
		}
	}
	const auto first = mesh.frames[0];
	mesh.frames.clear();
	surface.frames.clear();
	for (int f = 0; f < frames; ++f)
	{
		ModelFrameGeometry geometry;
		geometry.positions.reserve(surface.vertexCount);
		const double phase = double(f) * 0.21;
		for (int j = 0; j <= cells; ++j)
		{
			for (int i = 0; i <= cells; ++i)
				geometry.positions.append({float(i * 4), float(j * 4), float(6.0 * std::sin(i * 0.31 + phase) * std::cos(j * 0.27 + phase))});
		}
		geometry.normals.fill({0, 0, 1}, surface.vertexCount);
		surface.frames.append(geometry);
		auto info = first;
		info.index = f;
		info.name = QStringLiteral("wave%1").arg(f);
		mesh.frames.append(info);
	}
	surface.uvSeams.clear();
	updateEditableModelMetadata(&mesh);
	return mesh;
}
} // namespace

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	bool validCells = true, validFrames = true;
	const int cells = argc > 1 ? QString::fromLocal8Bit(argv[1]).toInt(&validCells) : 100;
	const int frames = argc > 2 ? QString::fromLocal8Bit(argv[2]).toInt(&validFrames) : 120;
	if (!validCells || !validFrames || cells < 2 || cells > 250 || frames < 1 || frames > modelDocumentMaxFrames ||
		qint64(cells + 1) * (cells + 1) > modelDocumentMaxVertices || qint64(cells + 1) * (cells + 1) * frames > modelDocumentMaxFrameVertices)
	{
		std::cerr << "Choose 2-250 cells and a frame count that keeps vertices x frames within the editable budget.\n";
		return 2;
	}
	QString error;
	bool ok = true;
	const auto mesh = wavedGrid(cells, frames);
	QJsonObject report{{"cells", cells}, {"frames", frames}, {"vertices", mesh.surfaces[0].vertexCount},
					   {"triangles", int(mesh.surfaces[0].triangles.size())}};
	const auto measure = [&](const char *key, const auto &action)
	{
		QElapsedTimer timer;
		timer.start();
		const bool passed = action();
		report.insert(QString::fromLatin1(key), double(timer.nsecsElapsed()) / 1.0e6);
		if (!passed)
		{
			std::cerr << key << " failed: " << qPrintable(error) << '\n';
			ok = false;
		}
	};

	ModelDocument document;
	measure("validateMs", [&] { return validateEditableModel(mesh).isEmpty(); });
	// The identity three ways in one run, so load on the machine cancels out:
	// one stream over every array (how builds before block digests did it), the
	// block digests from nothing, and the block digests after one pose changed.
	measure("fingerprintOneStreamMs", [&] { return !modelStateLegacyFingerprint(mesh, &error).isEmpty(); });
	ModelStateBlocks blocks;
	measure("fingerprintMs", [&] { return !summarizeModelState(mesh, &error, {}, &blocks).fingerprint.isEmpty(); });
	auto moved = mesh;
	moved.surfaces[0].frames[0].positions[0].z += 1;
	measure("fingerprintOnePoseChangedMs", [&] { return !summarizeModelState(moved, &error, {}, &blocks).fingerprint.isEmpty(); });
	measure("validateOnePoseChangedMs", [&] { return validateEditableModel(moved, {}, &mesh).isEmpty(); });
	measure("topologyMs", [&]
	{
		ModelSurfaceTopology topology;
		return prepareModelSurfaceTopology(mesh.surfaces[0], &topology, &error);
	});
	measure("geometricTopologyMs", [&]
	{
		ModelGeometricTopology topology;
		return buildModelGeometricTopology(mesh.surfaces[0], &topology);
	});
	measure("setMeshMs", [&] { return document.setMesh(mesh, &error); });

	// A handful of vertices and faces near the middle of the grid.
	const int middle = (cells / 2) * (cells + 1) + cells / 2;
	const QSet<int> vertices{middle, middle + 1, middle + cells + 1, middle + cells + 2};
	const int face = (cells / 2) * cells * 2 + (cells / 2) * 2;
	const QSet<int> faces{face, face + 1};
	const auto edit = [&](ModelEditKind kind)
	{
		ModelEdit operation;
		operation.kind = kind;
		operation.selection.surface = 0;
		return operation;
	};

	measure("selectMs", [&]
	{
		auto selection = document.selection();
		selection.vertices = vertices;
		document.setSelection(selection);
		return true;
	});
	measure("moveOneFrameMs", [&]
	{
		auto move = edit(ModelEditKind::Transform);
		move.selection.vertices = vertices;
		move.frame = 0;
		move.translation = {0, 0, 2};
		return document.edit(move, &error);
	});
	measure("moveAllFramesMs", [&]
	{
		auto move = edit(ModelEditKind::Transform);
		move.selection.vertices = vertices;
		move.frame = -1;
		move.translation = {0, 0, 2};
		return document.edit(move, &error);
	});
	measure("undoMs", [&] { return document.undo(); });
	measure("redoMs", [&] { return document.redo(); });
	measure("extrudeMs", [&]
	{
		auto extrude = edit(ModelEditKind::ExtrudeFaces);
		extrude.selection.faces = faces;
		extrude.tool.offset = 4;
		return document.edit(extrude, &error);
	});
	measure("subdivideMs", [&]
	{
		auto subdivide = edit(ModelEditKind::Subdivide);
		subdivide.selection.faces = faces;
		return document.edit(subdivide, &error);
	});
	measure("recalculateNormalsMs", [&]
	{
		auto normals = edit(ModelEditKind::RecalculateNormals);
		QSet<int> all;
		for (int index = 0; index < document.mesh().surfaces[0].triangles.size(); ++index)
			all.insert(index);
		normals.selection.faces = all;
		return document.edit(normals, &error);
	});
	measure("smoothMs", [&]
	{
		auto smooth = edit(ModelEditKind::SmoothVertices);
		smooth.selection.vertices = vertices;
		smooth.frame = 0;
		return document.edit(smooth, &error);
	});
	measure("duplicateFrameMs", [&]
	{
		if (frames >= modelDocumentMaxFrames)
			return true;
		auto duplicate = edit(ModelEditKind::DuplicateFrame);
		duplicate.frame = 0;
		duplicate.text = QStringLiteral("copy");
		return document.edit(duplicate, &error);
	});
	measure("deleteFacesMs", [&]
	{
		auto remove = edit(ModelEditKind::DeleteFaces);
		remove.selection.faces = QSet<int>{0, 1};
		return document.edit(remove, &error);
	});
	report.insert(QStringLiteral("historyMiB"), double(document.historyBytes()) / (1024.0 * 1024.0));
	report.insert(QStringLiteral("undoSteps"), document.undoSteps());

	QByteArray source;
	measure("serializeMs", [&]
	{
		source = serializeEditableModel(document.mesh(), &error);
		return !source.isEmpty();
	});
	report.insert(QStringLiteral("sourceMiB"), double(source.size()) / (1024.0 * 1024.0));
	// How much of reading a source is the JSON parser itself.
	measure("jsonParseMs", [&] { return QJsonDocument::fromJson(source).isObject(); });
	measure("parseMs", [&]
	{
		ModelMesh parsed;
		return parseEditableModel(source, &parsed, &error);
	});
	measure("exportMd3Ms", [&]
	{
		// A grid over the MD3 surface limits is refused; the refusal is timed too.
		QString exportError;
		exportEditableModel(document.mesh(), QStringLiteral("md3"), 0, &exportError);
		return true;
	});

	std::cout << QJsonDocument(report).toJson(QJsonDocument::Indented).constData();
	return ok ? 0 : 1;
}
