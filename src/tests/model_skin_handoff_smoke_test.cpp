#include "core/idtech_image.h"
#include "core/model_document.h"
#include "core/model_mdl.h"
#include "core/model_skin_handoff.h"
#include "core/model_skin_roundtrip.h"
#include "core/model_uv_layout.h"
#include "core/texture_project.h"
#include "tests/model_mdl_test_helpers.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <iostream>

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
} // namespace

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	QString error;
	auto palette = generatedIdTechPalette("quake");
	ModelMesh mesh;
	expect(importEditableModel("fixture.mdl", groupedMdlFixture().bytes, &mesh, &error, &palette) && mesh.mdl.enabled, "import the MDL fixture",
		   error);
	const auto &member = mesh.embeddedSkins[0].indexedFrames[0];
	const auto paletteBytes = modelMdlPaletteBytes(modelMdlPreviewPalette(mesh));
	// Export and re-import keep every index and the palette.
	for (const auto &format : {QStringLiteral("pcx"), QStringLiteral("png")})
	{
		QByteArray bytes;
		ModelMdlSkinInput decoded;
		expect(exportModelMdlSkinMember(mesh, 0, 0, format, &bytes, &error) &&
				   decodeModelMdlSkin("member." + format, bytes, generatedIdTechPalette("quake2"), &decoded, &error) &&
				   decoded.size == mesh.mdl.skinSize && decoded.pixels == member && decoded.palette == paletteBytes,
			   "an exported member reads back with identical indices and palette", error);
	}
	expect(modelMdlSkinMemberImage(mesh, 9, 0, &error).isNull() && !error.isEmpty(), "unknown members are refused");
	// An edited colour image maps to the palette under each fullbright mode.
	auto image = modelMdlSkinMemberImage(mesh, 0, 0, &error).convertToFormat(QImage::Format_ARGB32);
	ModelMdlSkinInput input;
	expect(modelMdlSkinInputFromImage(mesh, modelMdlSkinMemberImage(mesh, 0, 0, &error), TextureFullbrightMode::Exclude, &input, &error) &&
			   input.pixels == member,
		   "an indexed image in the model's palette keeps its indices, fullbrights included");
	expect(modelMdlSkinInputFromImage(mesh, image, TextureFullbrightMode::Allow, &input, &error) && input.pixels.size() == member.size(),
		   "a colour image maps to the palette", error);
	expect(modelMdlSkinInputFromImage(mesh, image, TextureFullbrightMode::Exclude, &input, &error) &&
			   std::none_of(input.pixels.cbegin(), input.pixels.cend(), [](char index) { return quint8(index) >= 224; }),
		   "excluding fullbrights keeps the result out of indices 224-255", error);
	expect(!modelMdlSkinInputFromImage(mesh, image.scaled(4, 4), TextureFullbrightMode::Allow, &input, &error) && error.contains("8"),
		   "an image of another size is refused with the skin size");
	// Replacing the member from the mapped image is one undo step.
	ModelDocument document;
	expect(document.setMesh(mesh, &error), "open the MDL", error);
	ModelEdit edit;
	edit.kind = ModelEditKind::ReplaceMdlSkinMember;
	edit.mdlSkinSlot = 0;
	edit.mdlSkinMember = 0;
	modelMdlSkinInputFromImage(mesh, image, TextureFullbrightMode::Exclude, &edit.mdlSkin, &error);
	expect(document.edit(edit, &error) && document.undoSteps() == 1 && document.mesh().embeddedSkins[0].indexedFrames[0] == edit.mdlSkin.pixels,
		   "replacing a member is one undo step", error);
	expect(document.undo() && document.mesh().embeddedSkins[0].indexedFrames[0] == member, "undo restores the member");
	// MD2 skin size checks.
	auto md2 = mesh;
	md2.md2SkinSize = {256, 256};
	expect(!modelMd2SkinSizeMismatch(md2, {128, 64}).isEmpty() && modelMd2SkinSizeMismatch(md2, {256, 256}).isEmpty(),
		   "a skin image of another size is reported");
	ModelEdit size;
	size.kind = ModelEditKind::SetMd2SkinSize;
	size.md2SkinSize = {128, 64};
	expect(applyModelEdit(&md2, size, nullptr, &error) && modelMd2SkinSizeMismatch(md2, {128, 64}).isEmpty(), "setting the MD2 size clears it",
		   error);
	// CLI: model mdl --operation export-skin.
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	QTemporaryDir temp(QDir(root.isEmpty() ? QDir::tempPath() : root).filePath("skin-handoff-XXXXXX"));
	if (argc > 1 && temp.isValid())
	{
		const auto input = temp.filePath("fixture.mesh.json"), output = temp.filePath("member.pcx");
		QFile file(input);
		expect(file.open(QIODevice::WriteOnly) && file.write(serializeEditableModel(mesh, &error)) > 0, "write CLI source");
		file.close();
		const auto run = [&](const QStringList &extra, QJsonObject *json)
		{
			QProcess process;
			process.setWorkingDirectory(temp.path());
			process.start(QString::fromLocal8Bit(argv[1]), QStringList{"--cli", "model", "mdl", input, "--operation", "export-skin", "--skin", "0",
																	   "--member", "0", "--output"} +
															   extra + QStringList{"--json", "--settings-file", temp.filePath("s.ini")});
			if (!process.waitForFinished(60000))
				return -1;
			*json = QJsonDocument::fromJson(process.readAllStandardOutput()).object();
			return process.exitCode();
		};
		QJsonObject json;
		expect(run({output, "--dry-run"}, &json) == 0 && !QFile::exists(output), "CLI export-skin dry run");
		expect(run({output}, &json) == 0 && QFile::exists(output), "CLI export-skin writes the member");
		QFile written(output);
		ModelMdlSkinInput decoded;
		expect(written.open(QIODevice::ReadOnly) && decodeModelMdlSkin(output, written.readAll(), palette, &decoded, &error) &&
				   decoded.pixels == member,
			   "the CLI member keeps its indices", error);
		expect(run({output}, &json) != 0, "an existing output needs --overwrite");
		expect(run({input, "--overwrite"}, &json) != 0, "the source is protected");
	}
	// Edit Skin round trip (core/model_skin_roundtrip.h): the texture project
	// keeps the member's indices, the UV guide never comes back, and edits map
	// to the palette with unchanged texels keeping their index.
	if (temp.isValid())
	{
		ModelSkinRoundTripLink link;
		link.model = QStringLiteral("models/fixture.mdl");
		link.mdlSkin = 0;
		link.mdlMember = 0;
		const auto skin = modelMdlSkinMemberImage(mesh, 0, 0, &error);
		link.size = skin.size();
		link.indexed = true;
		expect(modelSkinRoundTripFileName(link) == QStringLiteral("fixture-skin0-0.vtexture"), "round-trip files are named after the skin");
		ModelSkinRoundTripLink parsed;
		expect(modelSkinRoundTripLinkFromJson(modelSkinRoundTripLinkJson(link), &parsed) && parsed == link, "the link survives its JSON");
		QImage guide;
		ModelUvLayoutOptions layout;
		layout.edge = QColor(255, 255, 0);
		expect(renderModelUvLayout(mesh, {0}, skin.size(), layout, &guide, nullptr, &error), "draw the UV guide", error);
		const auto path = temp.filePath("fixture-skin0-0.vtexture");
		expect(writeModelSkinRoundTrip(path, skin, guide, link, &error), "write the round-trip project", error);
		TextureDocument project;
		QJsonObject metadata;
		expect(readTextureProject(path, &project, nullptr, &metadata, &error) && project.layers().size() == 2 && project.layers()[1].locked &&
				   project.layers()[1].opacity == 50 && project.activeLayerIndex() == 0 && metadata.contains("palette"),
			   "the project holds the skin under a locked guide layer with the skin's palette", error);
		QImage back;
		ModelSkinRoundTripLink readLink;
		expect(readModelSkinRoundTrip(path, &back, &readLink, &error) && back.format() == QImage::Format_Indexed8 && back == skin && readLink == link,
			   "an unedited project reads back as the exact member", error);
		ModelMdlSkinInput roundTrip;
		expect(modelSkinRoundTripMdlInput(mesh, 0, 0, back, &roundTrip, &error) && roundTrip.pixels == member,
			   "an unedited skin gives back every index", error);
		// Paint one texel on a new layer, as Textures would save it.
		auto layers = project.layers();
		QImage paint(skin.size(), QImage::Format_ARGB32);
		paint.fill(Qt::transparent);
		paint.setPixel(1, 1, qRgb(255, 0, 0));
		layers.insert(1, TextureLayer{3, QStringLiteral("Paint"), paint, true, false, 100, TextureBlendMode::Normal});
		TextureProjectSaveRequest save;
		save.path = path;
		save.overwrite = true;
		expect(project.restoreLayers(project.size(), layers, 1, &error) && writeTextureProject(project, save, metadata).succeeded,
			   "save the painted project", error);
		expect(readModelSkinRoundTrip(path, &back, nullptr, &error) && back.format() == QImage::Format_ARGB32 && back.pixel(1, 1) == qRgb(255, 0, 0) &&
				   back.pixel(0, 0) == skin.color(skin.pixelIndex(0, 0)),
			   "painted layers composite without the guide", error);
		expect(modelSkinRoundTripMdlInput(mesh, 0, 0, back, &roundTrip, &error), "map the painted skin", error);
		int changed = 0;
		for (int i = 0; i < member.size(); ++i)
			changed += roundTrip.pixels[i] != member[i];
		expect(changed <= 1 && roundTrip.pixels.size() == member.size(), "only the painted texel changes index");
		expect(!modelSkinRoundTripMdlInput(mesh, 0, 0, back.scaled(4, 4), &roundTrip, &error), "another canvas size is refused");
		// A project whose skin layers are all hidden is refused.
		for (auto &layer : layers)
			layer.visible = layer.id == 2;
		expect(project.restoreLayers(project.size(), layers, 0, &error) && writeTextureProject(project, save, metadata).succeeded &&
				   !readModelSkinRoundTrip(path, &back, nullptr, &error),
			   "a project showing only the guide is refused");
		metadata.remove("modelSkin");
		expect(writeTextureProject(project, save, metadata).succeeded && !readModelSkinRoundTrip(path, &back, nullptr, &error),
			   "a texture project Edit Skin did not write is refused");
	}
	std::cout << checks << " skin handoff checks; " << failures << " failures\n";
	return failures ? 1 : 0;
}
