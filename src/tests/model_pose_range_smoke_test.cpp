#include "core/model_document.h"
#include "core/model_frame_tools.h"
#include "core/model_transform.h"
#include "tests/model_frame_tools_test_helpers.h"

#include <QCoreApplication>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>

#include <cmath>
#include <cstdlib>
#include <iostream>
#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#endif

using namespace vibestudio;
namespace
{
bool ok = true;
bool expect(bool condition, const QString &message)
{
	if (!condition)
	{
		std::cerr << "FAIL: " << qPrintable(message) << '\n';
		ok = false;
	}
	return condition;
}
QByteArray bytes(const ModelMesh &mesh) { return QJsonDocument(editableModelJson(mesh)).toJson(QJsonDocument::Compact); }
bool near(ModelVec3 a, ModelVec3 b, double tolerance = 1e-4)
{
	return std::abs(a.x - b.x) <= tolerance && std::abs(a.y - b.y) <= tolerance && std::abs(a.z - b.z) <= tolerance;
}
bool same(ModelVec3 a, ModelVec3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
ModelEdit rangeEdit(ModelEditKind kind, int first, int last, ModelFrameFalloff falloff = ModelFrameFalloff::Constant)
{
	ModelEdit edit;
	edit.kind = kind;
	edit.selection.surface = 0;
	edit.selection.vertices = {0, 1};
	edit.poseRange.first = first;
	edit.poseRange.last = last;
	edit.poseRange.falloff = falloff;
	return edit;
}
// Only selected vertices in frames first..last moved.
bool untouchedOutside(const ModelMesh &before, const ModelMesh &after, int first, int last)
{
	for (int f = 0; f < before.frames.size(); ++f)
		for (int v = 0; v < before.surfaces[0].vertexCount; ++v)
		{
			const bool inside = f >= first && f <= last && (v == 0 || v == 1);
			if (!inside && !same(before.surfaces[0].frames[f].positions[v], after.surfaces[0].frames[f].positions[v]))
				return false;
		}
	return before.tags.size() == after.tags.size() && before.collisionBoxes[0].framePoses[2].centre.x == after.collisionBoxes[0].framePoses[2].centre.x;
}

void checkWeights()
{
	expect(modelFrameFalloffWeight(ModelFrameFalloff::Constant, 1, 3) == 1 && modelFrameFalloffWeight(ModelFrameFalloff::RampIn, 0, 3) == 1.0 / 3 &&
			   modelFrameFalloffWeight(ModelFrameFalloff::RampIn, 2, 3) == 1 && modelFrameFalloffWeight(ModelFrameFalloff::RampOut, 0, 3) == 1 &&
			   std::abs(modelFrameFalloffWeight(ModelFrameFalloff::Smooth, 1, 3) - 1) < 1e-12 &&
			   modelFrameFalloffWeight(ModelFrameFalloff::Smooth, 0, 3) < 1 && modelFrameFalloffWeight(ModelFrameFalloff::RampIn, 3, 3) == 0,
		   QStringLiteral("falloff weights"));
}

void checkEdits()
{
	const auto mesh = tests::frameRangeFixture(6);
	QString error;
	ModelDocument document;
	auto move = rangeEdit(ModelEditKind::TransformFrameRange, 1, 3, ModelFrameFalloff::RampIn);
	move.translation = {0, 0, 6};
	expect(document.setMesh(mesh, &error) && document.edit(move, &error), QStringLiteral("range transform: ") + error);
	auto after = document.mesh();
	bool offsets = true;
	for (int k = 0; k < 3; ++k)
		for (int v : {0, 1})
		{
			auto expected = mesh.surfaces[0].frames[1 + k].positions[v];
			expected.z += float(2 * (k + 1));
			offsets = offsets && near(after.surfaces[0].frames[1 + k].positions[v], expected);
		}
	expect(offsets && untouchedOutside(mesh, after, 1, 3), QStringLiteral("a ramp-in move offsets each frame by its weight and nothing else"));
	expect(document.undo() && bytes(document.mesh()) == bytes(mesh) && !document.canUndo(), QStringLiteral("one undo step"));
	// The pivot is resolved once, in the pivot frame.
	auto turn = rangeEdit(ModelEditKind::TransformFrameRange, 0, 2);
	turn.rotation = {0, 0, 90};
	turn.pivotMode = ModelTransformPivot::SelectionCentre;
	turn.pivotFrame = 1;
	expect(document.edit(turn, &error), QStringLiteral("range rotation: ") + error);
	const auto p1 = mesh.surfaces[0].frames[1].positions;
	const ModelVec3 pivot{(p1[0].x + p1[1].x) / 2, (p1[0].y + p1[1].y) / 2, (p1[0].z + p1[1].z) / 2};
	ModelTransform transform;
	transform.rotation = {0, 0, 90};
	transform.pivot = pivot;
	expect(near(document.mesh().surfaces[0].frames[2].positions[0], transformModelPoint(mesh.surfaces[0].frames[2].positions[0], transform)),
		   QStringLiteral("every frame turns about the pivot from the pivot frame"));
	document.undo();
	// Blending toward another frame's pose.
	auto blend = rangeEdit(ModelEditKind::BlendFramePose, 0, 1);
	blend.poseRange.sourceFrame = 5;
	blend.poseRange.amount = .5;
	expect(document.edit(blend, &error), QStringLiteral("blend: ") + error);
	after = document.mesh();
	const auto a = mesh.surfaces[0].frames[0].positions[0], s = mesh.surfaces[0].frames[5].positions[0];
	expect(near(after.surfaces[0].frames[0].positions[0], {(a.x + s.x) / 2, (a.y + s.y) / 2, (a.z + s.z) / 2}) && untouchedOutside(mesh, after, 0, 1),
		   QStringLiteral("half a blend moves the selection halfway to the source pose"));
	document.undo();
	blend.poseRange.amount = 1;
	expect(document.edit(blend, &error) && same(document.mesh().surfaces[0].frames[1].positions[1], mesh.surfaces[0].frames[5].positions[1]),
		   QStringLiteral("a full blend takes the source pose"));
	document.undo();
	// A relative pose adds the difference between two frames.
	auto relative = rangeEdit(ModelEditKind::ApplyRelativePose, 1, 2);
	relative.poseRange.sourceFrame = 5;
	relative.poseRange.baseFrame = 0;
	expect(document.edit(relative, &error), QStringLiteral("relative pose: ") + error);
	const auto d = mesh.surfaces[0].frames[5].positions[1], b = mesh.surfaces[0].frames[0].positions[1], base = mesh.surfaces[0].frames[2].positions[1];
	expect(near(document.mesh().surfaces[0].frames[2].positions[1], {base.x + d.x - b.x, base.y + d.y - b.y, base.z + d.z - b.z}) &&
			   untouchedOutside(mesh, document.mesh(), 1, 2),
		   QStringLiteral("the difference is added to the selection"));
	document.undo();
	// Refusals.
	const auto before = bytes(document.mesh());
	auto empty = rangeEdit(ModelEditKind::BlendFramePose, 0, 1);
	empty.selection.vertices.clear();
	empty.poseRange.sourceFrame = 5;
	expect(!document.edit(empty, &error) && bytes(document.mesh()) == before, QStringLiteral("an empty selection is refused"));
	auto mirror = rangeEdit(ModelEditKind::TransformFrameRange, 0, 1);
	mirror.scale = {-1, 1, 1};
	expect(!document.edit(mirror, &error) && error.contains("mirror"), QStringLiteral("mirroring is refused"));
	auto stray = rangeEdit(ModelEditKind::BlendFramePose, 0, 1);
	stray.poseRange.sourceFrame = 5;
	stray.translation = {1, 0, 0};
	expect(!document.edit(stray, &error) && error.contains("range transforms"), QStringLiteral("transform values apply to range transforms only"));
	auto self = rangeEdit(ModelEditKind::ApplyRelativePose, 0, 1);
	self.poseRange.sourceFrame = self.poseRange.baseFrame = 2;
	expect(!document.edit(self, &error) && bytes(document.mesh()) == before, QStringLiteral("a difference needs two frames"));
}

void checkCli(const QString &program, const QDir &dir)
{
	const auto source = dir.filePath("range.mesh.json");
	const auto mesh = tests::frameRangeFixture(6);
	ModelDocument document;
	QString error;
	if (!expect(document.setMesh(mesh, &error) && document.save(source, false, &error), QStringLiteral("save the CLI fixture")))
		return;
	const auto output = dir.filePath("sculpted.mesh.json");
	QProcess process;
	process.start(program, {"--cli", "model", "frames", source, "--operation", "transform-range", "--first", "1", "--last", "3", "--falloff",
							"ramp-in", "--vertices", "0,1", "--offset", "0,0,6", "--output", output, "--json", "--settings-file",
							dir.filePath("settings.ini")});
	const bool finished = process.waitForStarted(10000) && process.waitForFinished(120000);
	auto move = rangeEdit(ModelEditKind::TransformFrameRange, 1, 3, ModelFrameFalloff::RampIn);
	move.translation = {0, 0, 6};
	move.pivotMode = ModelTransformPivot::SelectionCentre;
	move.pivotFrame = 1;
	ModelDocument written, expected;
	expect(finished && process.exitCode() == 0 && written.load(output, &error) && expected.setMesh(mesh, &error) && expected.edit(move, &error) &&
			   bytes(written.mesh()) == bytes(expected.mesh()),
		   QStringLiteral("model frames transform-range writes the shared edit: ") + error);
}
} // namespace

int main(int argc, char **argv)
{
#if defined(_MSC_VER) && defined(_DEBUG)
	for (int type : {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT})
	{
		_CrtSetReportMode(type, _CRTDBG_MODE_FILE);
		_CrtSetReportFile(type, _CRTDBG_FILE_STDERR);
	}
#endif
	QCoreApplication app(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	QTemporaryDir temporary(QDir(root.isEmpty() ? QDir::tempPath() : root).filePath("pose-range-XXXXXX"));
	if (!temporary.isValid())
		return EXIT_FAILURE;
	checkWeights();
	checkEdits();
	if (argc > 1)
		checkCli(QString::fromLocal8Bit(argv[1]), QDir(temporary.path()));
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
