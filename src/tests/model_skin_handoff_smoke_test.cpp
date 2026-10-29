#include "core/idtech_image.h"
#include "core/model_document.h"
#include "core/model_mdl.h"
#include "core/model_skin_handoff.h"
#include "tests/model_mdl_test_helpers.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
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
	std::cout << checks << " skin handoff checks; " << failures << " failures\n";
	return failures ? 1 : 0;
}
