// The work an edit skips for arrays it did not replace: block digests of the
// revision fingerprint (core/model_fingerprint.h) and validation against a
// baseline (validateEditableModel). Skipping must never change an answer.
#include "core/model_design.h"
#include "core/model_document.h"
#include "core/model_fingerprint.h"

#include <QCoreApplication>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

using namespace vibestudio;

namespace
{
int failures = 0;
bool expect(bool condition, const char *message)
{
	if (!condition)
	{
		std::cerr << "FAIL: " << message << '\n';
		++failures;
	}
	return condition;
}

// A waved grid of cells x cells quads with `frames` differing poses. The
// first row stays flat in every pose and the poses share one normals array.
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
	const QVector<ModelVec3> normals(surface.vertexCount, ModelVec3{0, 0, 1});
	mesh.frames.clear();
	surface.frames.clear();
	for (int f = 0; f < frames; ++f)
	{
		ModelFrameGeometry geometry;
		for (int j = 0; j <= cells; ++j)
		{
			for (int i = 0; i <= cells; ++i)
				geometry.positions.append({float(i * 4), float(j * 4), float(3.0 * std::sin(i * 0.4 + f) * std::sin(j * 0.5))});
		}
		geometry.normals = normals;
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

// The bytes a summary hashed, from the progress it reports.
struct HashedBytes
{
	qint64 most = 0;
	ModelWorkControl control;
	HashedBytes()
	{
		control.progress = [this](ModelWorkPhase phase, qint64 done, qint64)
		{
			if (phase == ModelWorkPhase::Serializing)
				most = std::max(most, done);
		};
	}
};

void checkBlockDigests()
{
	QString error;
	const auto mesh = wavedGrid(24, 12);
	expect(validateEditableModel(mesh).isEmpty(), "the grid fixture is a valid editable model");
	const auto fresh = summarizeModelState(mesh, &error);
	expect(fresh.fingerprint.size() == 32 && fresh.packedSourceBytes > 0, "a summary without blocks gives an identity and a size");

	ModelStateBlocks blocks;
	HashedBytes whole;
	const auto first = summarizeModelState(mesh, &error, whole.control, &blocks);
	expect(first.fingerprint == fresh.fingerprint && first.packedSourceBytes == fresh.packedSourceBytes,
		   "collecting blocks does not change the identity or the size");
	// Triangles, texture coordinates, and positions and normals per pose. The
	// fixture's poses share one normals array.
	expect(blocks.triangles.size() == 1 && blocks.texCoords.size() == 1 && blocks.vectors.size() == 12 + 1,
		   "every distinct bulk array has one block");

	// The same model again: nothing left to hash but the identity stream.
	HashedBytes again;
	auto unchanged = blocks;
	expect(summarizeModelState(mesh, &error, again.control, &unchanged).fingerprint == fresh.fingerprint && again.most < whole.most / 8,
		   "an unchanged model reuses every block");

	// One pose moved: one array hashed, the same identity as hashing everything.
	auto moved = mesh;
	moved.surfaces[0].frames[5].positions[7].z += 1.5f;
	HashedBytes one;
	auto afterMove = blocks;
	const auto reused = summarizeModelState(moved, &error, one.control, &afterMove);
	expect(reused.fingerprint == summarizeModelState(moved, &error).fingerprint && reused.fingerprint != fresh.fingerprint,
		   "a reused summary equals a full one after a pose changes");
	expect(one.most < whole.most / 4, "a frame-local change hashes one pose, not the model");
	expect(afterMove.vectors.size() == 12 + 1 && !afterMove.vectors.contains(mesh.surfaces[0].frames[5].positions.constData()),
		   "the blocks returned are exactly the summarised model's");

	// The caller's blocks are left alone when the summary is cancelled.
	bool cancelled = false;
	ModelWorkControl stop;
	stop.cancelled = [&] { return cancelled; };
	stop.progress = [&](ModelWorkPhase, qint64, qint64) { cancelled = true; };
	auto kept = blocks;
	auto changedAll = mesh;
	for (auto &pose : changedAll.surfaces[0].frames)
		pose.positions[0].z += 0.25f;
	expect(summarizeModelState(changedAll, &error, stop, &kept).fingerprint.isEmpty() && cancelled &&
			   kept.vectors.size() == blocks.vectors.size() && kept.vectors.contains(mesh.surfaces[0].frames[0].positions.constData()),
		   "cancellation returns no identity and keeps the caller's blocks");

	// Structural edits: order, sharing and lengths are part of the identity.
	struct Change
	{
		const char *name;
		void (*apply)(ModelMesh *);
	};
	const Change changes[] = {
		{"a duplicated pose", [](ModelMesh *m)
		 {
			 m->surfaces[0].frames.append(m->surfaces[0].frames[3]);
			 m->frames.append(m->frames[3]);
			 m->frames.last().name = QStringLiteral("copy");
			 m->frames.last().index = int(m->frames.size()) - 1;
		 }},
		{"two poses swapped", [](ModelMesh *m) { m->surfaces[0].frames.swapItemsAt(2, 9); }},
		{"a removed pose", [](ModelMesh *m)
		 {
			 m->surfaces[0].frames.removeAt(4);
			 m->frames.removeLast();
		 }},
		{"a flipped triangle", [](ModelMesh *m) { std::swap(m->surfaces[0].triangles[10].b, m->surfaces[0].triangles[10].c); }},
		{"a moved texture coordinate", [](ModelMesh *m) { m->surfaces[0].texCoords[11].u += 0.125f; }},
		{"a changed normal", [](ModelMesh *m) { m->surfaces[0].frames[8].normals[2] = {0, 1, 0}; }},
		{"positions and normals exchanged", [](ModelMesh *m) { std::swap(m->surfaces[0].frames[1].positions, m->surfaces[0].frames[1].normals); }},
	};
	QSet<QByteArray> seen{fresh.fingerprint};
	for (const auto &change : changes)
	{
		auto edited = mesh;
		change.apply(&edited);
		auto carried = blocks;
		const auto incremental = summarizeModelState(edited, &error, {}, &carried).fingerprint;
		const auto full = summarizeModelState(edited, &error).fingerprint;
		if (!expect(!full.isEmpty() && incremental == full, "a reused summary equals a full one after a structural change"))
			std::cerr << "  " << change.name << '\n';
		if (!expect(!seen.contains(full), "every structural change gives a new identity"))
			std::cerr << "  " << change.name << '\n';
		seen.insert(full);
	}

	// Blocks of another model never match by accident: they hold their arrays.
	const auto other = wavedGrid(24, 12);
	auto foreign = blocks;
	expect(summarizeModelState(other, &error, {}, &foreign).fingerprint == fresh.fingerprint,
		   "an equal model built separately has the same identity whatever blocks it is given");
	auto different = wavedGrid(20, 6);
	auto stale = blocks;
	expect(summarizeModelState(different, &error, {}, &stale).fingerprint == summarizeModelState(different, &error).fingerprint,
		   "blocks of an unrelated model change nothing");

	// Writing to an array a block holds detaches it, so the old digest is not found.
	auto written = mesh;
	ModelStateBlocks held;
	summarizeModelState(written, &error, {}, &held);
	for (auto &point : written.surfaces[0].frames[0].positions)
		point.x += 2;
	written.surfaces[0].triangles[0] = {written.surfaces[0].triangles[0].b, written.surfaces[0].triangles[0].c, written.surfaces[0].triangles[0].a};
	auto afterWrite = held;
	expect(summarizeModelState(written, &error, {}, &afterWrite).fingerprint == summarizeModelState(written, &error).fingerprint,
		   "arrays written after a summary are hashed again");

	// Signed zero is one value in a source, so it is one value in the identity.
	auto negativeZero = mesh;
	auto positiveZero = mesh;
	negativeZero.surfaces[0].frames[0].positions[0].x = -0.0f;
	positiveZero.surfaces[0].frames[0].positions[0].x = 0.0f;
	negativeZero.surfaces[0].texCoords[0].u = -0.0f;
	positiveZero.surfaces[0].texCoords[0].u = 0.0f;
	expect(modelStateFingerprint(negativeZero) == modelStateFingerprint(positiveZero), "negative and positive zero are the same identity");

	// The earlier identity scheme stays available for work staged by older builds.
	const auto legacy = modelStateLegacyFingerprint(mesh, &error);
	expect(legacy.size() == 32 && legacy != fresh.fingerprint && legacy == modelStateLegacyFingerprint(other) &&
			   legacy != modelStateLegacyFingerprint(moved),
		   "the legacy identity is stable, content based and distinct from the current one");
}

void checkDocumentIdentity()
{
	QString error;
	ModelDocument document;
	expect(document.setMesh(wavedGrid(12, 6), &error), "the document adopts the grid");
	const auto sameAsFresh = [&](const char *message)
	{
		ModelDocument fresh;
		const bool adopted = fresh.setMesh(document.mesh(), &error);
		expect(adopted && fresh.revisionFingerprint() == document.revisionFingerprint() && fresh.sourceBytes() == document.sourceBytes(), message);
	};
	ModelEdit move;
	move.kind = ModelEditKind::Transform;
	move.selection.vertices = {5, 6, 18};
	move.frame = 2;
	move.translation = {0, 0, 1};
	const auto start = document.revisionFingerprint();
	expect(document.edit(move, &error), "a frame-local move applies");
	const auto afterMove = document.revisionFingerprint();
	sameAsFresh("the identity after a frame-local move equals a full summary");
	move.frame = -1;
	expect(document.edit(move, &error), "an all-frame move applies");
	sameAsFresh("the identity after an all-frame move equals a full summary");
	expect(document.undo() && document.revisionFingerprint() == afterMove, "undo returns to the earlier identity");
	// An edit made after undo starts from a state other than the one summarised last.
	ModelEdit remove;
	remove.kind = ModelEditKind::DeleteFaces;
	remove.selection.faces = {0, 1};
	expect(document.edit(remove, &error), "faces delete after an undo");
	sameAsFresh("the identity after undo and a topology edit equals a full summary");
	ModelEdit duplicate;
	duplicate.kind = ModelEditKind::DuplicateFrame;
	duplicate.frame = 1;
	duplicate.text = QStringLiteral("copy");
	expect(document.edit(duplicate, &error), "a frame duplicates");
	sameAsFresh("the identity after duplicating a frame equals a full summary");
	while (document.canUndo())
		document.undo();
	expect(document.revisionFingerprint() == start && !document.isModified(), "undoing everything restores the first identity");
	while (document.canRedo())
		document.redo();
	sameAsFresh("the identity after redoing everything equals a full summary");
	// A refused edit leaves the document, and what it reuses, as it was.
	ModelEdit collapse;
	collapse.kind = ModelEditKind::Transform;
	collapse.selection.faces = {4};
	collapse.frame = 3;
	collapse.scale = {0, 0, 0};
	collapse.pivotMode = ModelTransformPivot::SelectionCentre;
	const auto before = document.revisionFingerprint();
	expect(!document.edit(collapse, &error) && !error.isEmpty() && document.revisionFingerprint() == before,
		   "collapsing a face in one frame is refused and changes nothing");
	move.frame = 0;
	expect(document.edit(move, &error), "an edit after a refused one applies");
	sameAsFresh("the identity after a refused edit and a move equals a full summary");
}

void checkValidationBaseline()
{
	const auto mesh = wavedGrid(10, 5);
	expect(validateEditableModel(mesh).isEmpty() && validateEditableModel(mesh, {}, &mesh).isEmpty(), "a valid model passes with and without a baseline");
	struct Damage
	{
		const char *name;
		void (*apply)(ModelMesh *);
	};
	const Damage damages[] = {
		{"a position that is not a number in one pose", [](ModelMesh *m) { m->surfaces[0].frames[3].positions[4].y = std::numeric_limits<float>::quiet_NaN(); }},
		{"a zero normal in one pose", [](ModelMesh *m) { m->surfaces[0].frames[2].normals[9] = {0, 0, 0}; }},
		{"a collapsed triangle in one pose", [](ModelMesh *m)
		 {
			 const auto triangle = m->surfaces[0].triangles[6];
			 auto &positions = m->surfaces[0].frames[1].positions;
			 positions[triangle.b] = positions[triangle.a];
		 }},
		{"a triangle index out of range", [](ModelMesh *m) { m->surfaces[0].triangles[3].c = m->surfaces[0].vertexCount; }},
		{"a degenerate triangle index", [](ModelMesh *m) { m->surfaces[0].triangles[3].c = m->surfaces[0].triangles[3].a; }},
		{"a texture coordinate that is not a number", [](ModelMesh *m) { m->surfaces[0].texCoords[5].v = std::numeric_limits<float>::infinity(); }},
		{"a pose with a vertex missing", [](ModelMesh *m) { m->surfaces[0].frames[4].positions.removeLast(); }},
		// The poses stay the baseline's own arrays; only the triangles change.
		{"a triangle of three vertices on one line", [](ModelMesh *m) { m->surfaces[0].triangles[0] = {0, 1, 2}; }},
	};
	for (const auto &damage : damages)
	{
		auto broken = mesh;
		damage.apply(&broken);
		const auto alone = validateEditableModel(broken);
		const auto against = validateEditableModel(broken, {}, &mesh);
		if (!expect(alone == against, "validation against a baseline gives the same answer as validation alone"))
			std::cerr << "  " << damage.name << '\n';
	}
	// Vertices 0, 1 and 2 lie on the flat first row: a triangle of them is
	// collapsed in every pose, though each pose is still the baseline's array.
	auto collinear = mesh;
	collinear.surfaces[0].triangles[0] = {0, 1, 2};
	expect(!validateEditableModel(collinear, {}, &mesh).isEmpty(), "new triangles are checked against poses the baseline shares");
	// A baseline with other counts is no baseline for that surface.
	const auto smaller = wavedGrid(8, 5);
	auto broken = mesh;
	broken.surfaces[0].frames[0].positions[0].x = std::numeric_limits<float>::quiet_NaN();
	expect(!validateEditableModel(broken, {}, &smaller).isEmpty() && validateEditableModel(mesh, {}, &smaller).isEmpty(),
		   "an unrelated baseline skips nothing");
}
} // namespace

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	checkBlockDigests();
	checkDocumentIdentity();
	checkValidationBaseline();
	if (failures)
	{
		std::cerr << failures << " model edit reuse check(s) failed\n";
		return EXIT_FAILURE;
	}
	std::cout << "Model edit reuse checks passed\n";
	return EXIT_SUCCESS;
}
