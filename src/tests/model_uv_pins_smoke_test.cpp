#include "core/model_document.h"
#include "core/model_uv.h"
#include "core/model_uv_ops.h"
#include "core/model_uv_pins.h"
#include "tests/model_uv_health_test_helpers.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>

// Pinned UV corners (core/model_uv_pins.h): the pin edits, the editable
// source (version 17), how pins follow other edits, the conformal unwrap
// that reads them, and the CLI.
using namespace vibestudio;
using namespace vibestudio::tests;
namespace
{
int checks = 0, failures = 0;
bool expect(bool value, const char *message, const QString &detail = {})
{
	++checks;
	if (!value)
	{
		++failures;
		std::cerr << "FAIL: " << message << ": " << qPrintable(detail) << '\n';
	}
	return value;
}
ModelEdit operation(ModelEditKind kind, ModelSelection selection)
{
	ModelEdit edit;
	edit.kind = kind;
	edit.selection = selection;
	return edit;
}
QByteArray source(const ModelMesh &mesh, ModelSourceEncoding encoding = ModelSourceEncoding::Readable)
{
	return QJsonDocument(editableModelJson(mesh, nullptr, {}, encoding)).toJson(QJsonDocument::Compact);
}
} // namespace

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	QString error;

	// Pin and Unpin: vertices, edge ends and face corners; no-ops refused.
	{
		auto mesh = strip(3);
		const auto before = source(mesh);
		expect(!applyModelEdit(&mesh, operation(ModelEditKind::PinUv, {0}), nullptr, &error) && source(mesh) == before,
			   "pinning needs a selection");
		expect(applyModelEdit(&mesh, operation(ModelEditKind::PinUv, {0, {0, 3}}), nullptr, &error) &&
				   mesh.surfaces[0].uvPins == QSet<int>{0, 3},
			   "pin two vertices", error);
		expect(!applyModelEdit(&mesh, operation(ModelEditKind::PinUv, {0, {0}}), nullptr, &error), "pinning a pinned corner is refused");
		expect(applyModelEdit(&mesh, operation(ModelEditKind::PinUv, {0, {}, {}, {modelEdge(4, 5)}}), nullptr, &error) &&
				   mesh.surfaces[0].uvPins == QSet<int>{0, 3, 4, 5},
			   "pin the ends of an edge", error);
		expect(applyModelEdit(&mesh, operation(ModelEditKind::UnpinUv, {0, {}, {0}}), nullptr, &error) &&
				   mesh.surfaces[0].uvPins == QSet<int>{3, 4},
			   "unpin the corners of a face", error);
		auto settings = operation(ModelEditKind::PinUv, {0, {1}});
		settings.uvTools.weldTolerance = .1;
		expect(!applyModelEdit(&mesh, settings, nullptr, &error), "pin edits take no UV settings");
		ModelSelection selection;
		expect(applyModelEdit(&mesh, operation(ModelEditKind::UnpinUv, {0, {3, 4}}), &selection, &error) && mesh.surfaces[0].uvPins.isEmpty() &&
				   source(mesh) == before,
			   "unpinning everything restores the source byte for byte", error);
	}

	// The editable source: version 17 only with pins, readable and packed.
	{
		auto mesh = strip(3);
		expect(QJsonDocument::fromJson(source(mesh)).object().value("version").toInt() < 17, "no pins, no version 17");
		mesh.surfaces[0].uvPins = {6, 1};
		const auto json = QJsonDocument::fromJson(source(mesh)).object();
		expect(json.value("version").toInt() == 17 &&
				   json.value("surfaces").toArray().first().toObject().value("uvPins").toArray() == QJsonArray{1, 6},
			   "pins are saved ascending in version 17");
		for (const auto encoding : {ModelSourceEncoding::Readable, ModelSourceEncoding::Packed})
		{
			ModelMesh loaded;
			expect(parseEditableModel(source(mesh, encoding), &loaded, &error) && loaded.surfaces[0].uvPins == QSet<int>{1, 6} &&
					   source(loaded, encoding) == source(mesh, encoding),
				   "pins round-trip through the source", error);
		}
		const auto broken = [&](const std::function<void(QJsonObject &)> &change)
		{
			auto object = json;
			change(object);
			ModelMesh loaded;
			return !parseEditableModel(QJsonDocument(object).toJson(), &loaded, &error);
		};
		const auto pins = [](QJsonObject &object, const QJsonValue &value)
		{
			auto surfaces = object.value("surfaces").toArray();
			auto surface = surfaces.first().toObject();
			surface.insert("uvPins", value);
			surfaces.replace(0, surface);
			object.insert("surfaces", surfaces);
		};
		expect(broken([&](QJsonObject &o) { o.insert("version", 14); }), "pins need version 17");
		expect(broken([&](QJsonObject &o) { pins(o, QJsonArray{6, 1}); }), "pins must be ascending");
		expect(broken([&](QJsonObject &o) { pins(o, QJsonArray{1, 1}); }), "pins must be unique");
		expect(broken([&](QJsonObject &o) { pins(o, QJsonArray{1, 99}); }), "pins must be inside the surface");
		expect(broken([&](QJsonObject &o) { pins(o, QJsonArray{1.5}); }), "pins are whole numbers");
		// The document fingerprint and undo see pins.
		ModelDocument document;
		expect(document.setMesh(strip(3), &error), "open the strip", error);
		const auto revision = document.revisionFingerprint();
		expect(document.edit(operation(ModelEditKind::PinUv, {0, {2}}), &error) && document.revisionFingerprint() != revision,
			   "a pin edit is a new revision", error);
		expect(document.undo() && document.mesh().surfaces[0].uvPins.isEmpty() && document.revisionFingerprint() == revision,
			   "undo removes the pin");
	}

	// Pins follow other edits.
	{
		auto pinned = strip(3);
		pinned.surfaces[0].uvPins = {1, 5};
		// UV moves keep pins on their corners.
		auto moved = pinned;
		auto shift = operation(ModelEditKind::TransformUv, {0, {}, {0, 1}});
		shift.uvOffset = {.1f, 0};
		expect(applyModelEdit(&moved, shift, nullptr, &error) && moved.surfaces[0].uvPins == QSet<int>{1, 5}, "a UV move keeps pins", error);
		// Detach appends copies; the pinned originals keep their index.
		auto detached = pinned;
		expect(applyModelEdit(&detached, operation(ModelEditKind::DetachUv, {0, {}, {2, 3}}), nullptr, &error) &&
				   detached.surfaces[0].uvPins.contains(1) && detached.surfaces[0].uvPins.contains(5),
			   "detach keeps the pinned originals", error);
		// Weld joins a pinned corner with its copy and keeps it pinned.
		auto welded = detached;
		expect(applyModelEdit(&welded, operation(ModelEditKind::WeldUv, {0, {}, {2, 3}}), nullptr, &error) &&
				   welded.surfaces[0].vertexCount == pinned.surfaces[0].vertexCount && welded.surfaces[0].uvPins == QSet<int>{1, 5},
			   "weld keeps pins on the joined corners", error);
		// Deleting faces compacts the corners; pins move with their corner.
		auto trimmed = pinned;
		expect(applyModelEdit(&trimmed, operation(ModelEditKind::DeleteFaces, {0, {}, {0, 1}}), nullptr, &error), "delete the first quad", error);
		const auto &t = trimmed.surfaces[0];
		bool follows = t.uvPins.size() == 2;
		for (int pin : t.uvPins)
			follows = follows && (t.frames[0].positions[pin].x == 10.f);
		expect(follows, "pins follow their corners through compaction");
		// A corner a mesh edit removes loses its pin.
		auto merged = pinned;
		auto merge = operation(ModelEditKind::MergeVertices, {0, {1, 2}});
		merge.tool.merge = ModelMergeMode::Collapse;
		if (applyModelEdit(&merged, merge, nullptr, &error))
			expect(std::all_of(merged.surfaces[0].uvPins.cbegin(), merged.surfaces[0].uvPins.cend(),
							   [&](int pin) { return pin >= 0 && pin < merged.surfaces[0].vertexCount; }),
				   "pins stay inside the surface after a merge");
	}

	// A conformal unwrap keeps stored pins exactly; the rest flattens.
	{
		auto mesh = strip(3);
		auto &s = mesh.surfaces[0];
		s.texCoords[0] = {.1f, .2f};
		s.texCoords[3] = {.7f, .2f};
		s.uvPins = {0, 3};
		ModelEdit edit = operation(ModelEditKind::UnwrapUv, {0});
		for (int f = 0; f < s.triangles.size(); ++f)
			edit.selection.faces.insert(f);
		edit.uvTools.unwrapMethod = ModelUvUnwrapMethod::Conformal;
		edit.uvTools.unwrapPack = false;
		expect(applyModelEdit(&mesh, edit, nullptr, &error), "conformal unwrap with stored pins", error);
		const auto &u = mesh.surfaces[0];
		expect(u.texCoords[0].u == .1f && u.texCoords[0].v == .2f && u.texCoords[3].u == .7f && u.texCoords[3].v == .2f,
			   "pinned corners keep their UVs exactly");
		expect(std::abs(u.texCoords[1].u - .3f) < 1e-4 && std::abs(u.texCoords[1].v - .2f) < 1e-4 && std::abs(u.texCoords[4].v - .4f) < 1e-4,
			   "the strip flattens between the pins at their spacing");
		expect(u.uvPins == QSet<int>{0, 3}, "the pins stay after the unwrap");
	}

	// CLI: uv-pin writes version 17; model uv lists the pins.
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	QTemporaryDir temp(QDir(root.isEmpty() ? QDir::tempPath() : root).filePath("uv-pins-XXXXXX"));
	const auto input = temp.filePath("strip.mesh.json"), output = temp.filePath("pinned.mesh.json");
	QFile file(input);
	expect(file.open(QIODevice::WriteOnly) && file.write(serializeEditableModel(strip(3), &error)) > 0, "write CLI source");
	file.close();
	if (argc > 1)
	{
		const auto run = [&](const QStringList &arguments, QJsonObject *json)
		{
			QProcess process;
			process.setWorkingDirectory(temp.path());
			process.start(QString::fromLocal8Bit(argv[1]), QStringList{"--cli", "model"} + arguments + QStringList{"--json", "--settings-file", temp.filePath("s.ini")});
			if (!process.waitForFinished(60000))
				return -1;
			*json = QJsonDocument::fromJson(process.readAllStandardOutput()).object();
			return process.exitCode();
		};
		QJsonObject json;
		expect(run({"edit", input, "--operation", "uv-pin", "--vertices", "2,6", "--output", output}, &json) == 0, "CLI pin");
		ModelDocument loaded;
		expect(loaded.load(output, &error) && loaded.mesh().surfaces[0].uvPins == QSet<int>{2, 6}, "CLI pins match the document edit", error);
		expect(run({"uv", output}, &json) == 0 && json.value("pins").toArray() == QJsonArray{2, 6}, "model uv lists the pins");
		expect(run({"edit", output, "--operation", "uv-unpin", "--vertices", "2", "--output", output, "--overwrite"}, &json) == 0 &&
				   loaded.load(output, &error) && loaded.mesh().surfaces[0].uvPins == QSet<int>{6},
			   "CLI unpin", error);
		expect(run({"edit", output, "--operation", "uv-pin", "--vertices", "1", "--uv-weld-tolerance", "0.1", "--output", output, "--overwrite"}, &json) == 2,
			   "CLI refuses UV operator options on a pin");
	}
	std::cout << checks << " UV pin checks; " << failures << " failures\n";
	return failures ? 1 : 0;
}
