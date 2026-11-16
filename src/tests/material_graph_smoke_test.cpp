// The material node graph: what it shows for each engine, and that every
// graph edit lands in the source text, keeps the rest of it, and parses
// again.

#include "core/material_classic.h"
#include "core/material_graph.h"
#include "core/material_script.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QRectF>
#include <QSet>

#include <iostream>

using namespace slopstudio;

namespace {

int failures = 0;

bool expect(bool condition, const std::string& message)
{
	if (!condition) {
		std::cerr << "FAIL: " << message << '\n';
		++failures;
	}
	return condition;
}

MaterialGraphEditResult apply(const QString& text, MaterialTextKind kind, const QString& material, MaterialGraphEditKind editKind, const QString& node,
	const QString& property = {}, const QString& value = {}, const QString& nodeTemplate = {}, int step = 0)
{
	MaterialGraphEdit edit;
	edit.kind = editKind;
	edit.node = node;
	edit.property = property;
	edit.value = value;
	edit.nodeTemplate = nodeTemplate;
	edit.step = step;
	const MaterialGraphEditResult result = applyMaterialGraphEdit(text, kind, material, edit);
	if (!result.ok) {
		std::cerr << "  edit error: " << result.error.toStdString() << '\n';
	}
	return result;
}

const MaterialDefinition* parsed(const QString& text, MaterialEngine engine, MaterialScript* holder)
{
	*holder = parseMaterialScript(text, engine);
	return holder->materials.isEmpty() ? nullptr : &holder->materials.first();
}

// No two laid-out nodes overlap, with room kept for every thumbnail.
bool laidOutWithoutOverlap(const MaterialGraph& graph, std::string* detail)
{
	QVector<QRectF> rects;
	for (const MaterialGraphNode& node : graph.nodes) {
		rects.push_back(QRectF(node.position, materialGraphNodeSize(node, true)));
	}
	for (int a = 0; a < rects.size(); ++a) {
		for (int b = a + 1; b < rects.size(); ++b) {
			if (rects.at(a).intersects(rects.at(b))) {
				*detail = (graph.nodes.at(a).id + QStringLiteral(" overlaps ") + graph.nodes.at(b).id).toStdString();
				return false;
			}
		}
	}
	return true;
}

// Every node the graph offers to remove can be removed.
void checkRemovable(const QString& text, MaterialTextKind kind, const QString& name, MaterialEngine engine, const std::string& what)
{
	MaterialScript script;
	const MaterialDefinition* definition = parsed(text, engine, &script);
	if (!definition) {
		expect(false, what + ": the fixture parses");
		return;
	}
	const MaterialGraph graph = buildMaterialGraph(*definition, kind);
	for (const MaterialGraphNode& node : graph.nodes) {
		if (!node.removable) {
			continue;
		}
		MaterialGraphEdit edit;
		edit.kind = MaterialGraphEditKind::RemoveNode;
		edit.node = node.id;
		const MaterialGraphEditResult result = applyMaterialGraphEdit(text, kind, name, edit);
		expect(result.ok, what + ": a node marked removable can be removed: " + node.id.toStdString() + " " + result.error.toStdString());
	}
}

int lineCount(const QString& text, const QString& keyword)
{
	int count = 0;
	for (const QString& line : text.split(QLatin1Char('\n'))) {
		count += line.trimmed().startsWith(keyword + QLatin1Char(' '), Qt::CaseInsensitive) ? 1 : 0;
	}
	return count;
}

void checkQuake3Edits()
{
	const MaterialTextKind kind = MaterialTextKind::Quake3Shader;
	// Wolfenstein's mapnocomp and Jedi Knight's oneshotAnimMap set the stage's
	// image; an edit changes that line, not a new map line before it.
	const QString wolf = QStringLiteral("textures/test/wolf\n{\n\t{\n\t\tmapnocomp textures/a.tga\n\t\trgbGen identity\n\t}\n}\n");
	MaterialGraphEditResult result = apply(wolf, kind, QStringLiteral("textures/test/wolf"), MaterialGraphEditKind::SetProperty,
		QStringLiteral("stage/0/image"), QStringLiteral("path"), QStringLiteral("textures/b.tga"));
	MaterialScript script;
	const MaterialDefinition* definition = parsed(result.text, MaterialEngine::Quake3, &script);
	expect(result.ok && definition && definition->stages.size() == 1 && definition->stages.first().imagePath == QStringLiteral("textures/b.tga")
			&& lineCount(result.text, QStringLiteral("mapnocomp")) == 1 && lineCount(result.text, QStringLiteral("map")) == 0,
		"an image written with mapnocomp is edited in place");
	result = apply(wolf, kind, QStringLiteral("textures/test/wolf"), MaterialGraphEditKind::SetProperty, QStringLiteral("stage/0/image"),
		QStringLiteral("clamp"), QStringLiteral("true"));
	expect(!result.ok, "mapnocomp has no clamped form, so Clamp is refused rather than adding a second image line");
	const QString jedi = QStringLiteral("textures/test/jk\n{\n\t{\n\t\toneshotAnimMap 10 textures/a.tga textures/b.tga\n\t}\n}\n");
	result = apply(jedi, kind, QStringLiteral("textures/test/jk"), MaterialGraphEditKind::SetProperty, QStringLiteral("stage/0/image"),
		QStringLiteral("frequency"), QStringLiteral("5"));
	expect(result.ok && result.text.contains(QStringLiteral("oneshotAnimMap 5 textures/a.tga textures/b.tga"))
			&& lineCount(result.text, QStringLiteral("animMap")) == 0,
		"an oneshotAnimMap's speed changes on its own line");

	// Surface parameters the parser does not know are shown and kept.
	const QString parms = QStringLiteral("textures/test/wood\n{\n\tsurfaceparm woodsteps\n\tsurfaceparm nolightmap\n\t{\n\t\tmap textures/a.tga\n\t}\n}\n");
	definition = parsed(parms, MaterialEngine::Quake3, &script);
	const MaterialGraph parmGraph = buildMaterialGraph(*definition);
	expect(parmGraph.node(QStringLiteral("material"))->property(QStringLiteral("surfaceparms"))->value.contains(QStringLiteral("woodsteps")),
		"an unknown surfaceparm is listed");
	result = apply(parms, kind, QStringLiteral("textures/test/wood"), MaterialGraphEditKind::SetProperty, QStringLiteral("material"),
		QStringLiteral("surfaceparms"), QStringLiteral("woodsteps trans"));
	expect(result.ok && result.text.contains(QStringLiteral("surfaceparm woodsteps")) && result.text.contains(QStringLiteral("surfaceparm trans"))
			&& !result.text.contains(QStringLiteral("nolightmap")) && lineCount(result.text, QStringLiteral("surfaceparm")) == 2,
		"editing the surface parameters keeps the unknown one and adds no duplicate");

	// tcMods: the limit, focus after adding and removing, and id moves.
	const QString mods = QStringLiteral(
		"textures/test/mods\n{\n\t{\n\t\tmap textures/a.tga\n\t\ttcMod scroll 1 0\n\t\ttcMod scale 2 2\n\t}\n\t{\n\t\tmap $lightmap\n\t}\n}\n");
	const QString modsName = QStringLiteral("textures/test/mods");
	result = apply(mods, kind, modsName, MaterialGraphEditKind::AddNode, QStringLiteral("stage/0"), {}, {}, QStringLiteral("q3.tcmod.rotate"));
	expect(result.ok && result.focusNode == QStringLiteral("stage/0/tcmod/2"), "adding a tcMod selects the new tcMod");
	result = apply(result.text, kind, modsName, MaterialGraphEditKind::AddNode, QStringLiteral("stage/0"), {}, {}, QStringLiteral("q3.tcmod.turb"));
	expect(result.ok, "a fourth tcMod is accepted");
	result = apply(result.text, kind, modsName, MaterialGraphEditKind::AddNode, QStringLiteral("stage/0"), {}, {}, QStringLiteral("q3.tcmod.scroll"));
	expect(!result.ok && !result.error.isEmpty(), "a fifth tcMod is refused (Quake III would drop the shader)");
	result = apply(mods, kind, modsName, MaterialGraphEditKind::RemoveNode, QStringLiteral("stage/0/tcmod/1"));
	expect(result.ok && result.focusNode == QStringLiteral("stage/0/tcmod/0"), "removing a tcMod selects the one before it");
	result = apply(mods, kind, modsName, MaterialGraphEditKind::RemoveNode, QStringLiteral("stage/0/tcmod/0"));
	expect(result.ok && result.focusNode == QStringLiteral("stage/0/tcgen"), "removing the first tcMod selects the coordinates it read");
	result = apply(mods, kind, modsName, MaterialGraphEditKind::AddNode, QStringLiteral("stage/1"), {}, {}, QStringLiteral("q3.rgbgen.wave"));
	expect(result.ok && result.focusNode == QStringLiteral("stage/1/rgbgen"), "adding an rgbGen selects it");
	result = apply(mods, kind, modsName, MaterialGraphEditKind::RemoveNode, QStringLiteral("stage/1"));
	expect(result.ok && result.focusNode == QStringLiteral("stage/0"), "removing the last stage selects the one before it");
	result = apply(mods, kind, modsName, MaterialGraphEditKind::MoveNode, QStringLiteral("stage/1"), {}, {}, {}, -1);
	expect(result.ok && remapMaterialGraphId(QStringLiteral("stage/1/image"), result.idMoves) == QStringLiteral("stage/0/image")
			&& remapMaterialGraphId(QStringLiteral("stage/0"), result.idMoves) == QStringLiteral("stage/1"),
		"moving a stage reports the ids that swapped");
	result = apply(mods, kind, modsName, MaterialGraphEditKind::RemoveNode, QStringLiteral("stage/0/tcmod/0"));
	expect(result.ok && remapMaterialGraphId(QStringLiteral("stage/0/tcmod/1"), result.idMoves) == QStringLiteral("stage/0/tcmod/0")
			&& remapMaterialGraphId(QStringLiteral("stage/0/tcmod/0"), result.idMoves).isEmpty(),
		"removing a tcMod moves the later ones up");

	// Labels name the stage, so two stages' coordinates are told apart.
	definition = parsed(mods, MaterialEngine::Quake3, &script);
	const MaterialGraph modGraph = buildMaterialGraph(*definition);
	expect(materialGraphNodeLabel(*modGraph.node(QStringLiteral("stage/0/tcgen"))) != materialGraphNodeLabel(*modGraph.node(QStringLiteral("stage/1/tcgen"))),
		"node labels include the stage");
	for (const MaterialGraphNode& node : modGraph.nodes) {
		for (const MaterialGraphProperty& property : node.properties) {
			expect(property.optionLabels.isEmpty() || property.optionLabels.size() == property.options.size(),
				"an enum's labels match its options: " + property.id.toStdString());
		}
	}
	const MaterialGraphProperty* cull = modGraph.node(QStringLiteral("material"))->property(QStringLiteral("cull"));
	expect(cull && cull->value == QStringLiteral("default") && !cull->options.contains(QStringLiteral("front")),
		"Cull offers the default, back and none; 'cull front' is not a Quake III keyword");
	result = apply(QString(mods).replace(QStringLiteral("{\n\t{"), QStringLiteral("{\n\tcull none\n\t{")), kind, modsName,
		MaterialGraphEditKind::SetProperty, QStringLiteral("material"), QStringLiteral("cull"), QStringLiteral("default"));
	expect(result.ok && !result.text.contains(QStringLiteral("cull")), "Cull's default removes the cull line");
	result = apply(mods, kind, modsName, MaterialGraphEditKind::RenameMaterial, QStringLiteral("material"), {}, QStringLiteral("  "));
	expect(!result.ok, "an empty name is refused");
	const MaterialGraphProperty* blend = modGraph.node(QStringLiteral("stage/0"))->property(QStringLiteral("blend"));
	expect(blend && blend->options.first() == QStringLiteral("opaque") && !blend->optionLabels.isEmpty(), "Blend's choices have readable labels");

	// A large shader lays out without overlaps: 8 stages with 4 tcMods each.
	QString big = QStringLiteral("textures/test/big\n{\n\tqer_editorimage textures/a.tga\n\tdeformVertexes wave 64 sin 0 2 0 0.5\n\tskyParms env/sky 512 -\n");
	for (int stage = 0; stage < 8; ++stage) {
		big += QStringLiteral("\t{\n\t\tmap textures/s%1.tga\n\t\ttcMod scroll 1 0\n\t\ttcMod scale 2 2\n\t\ttcMod rotate 3\n\t\ttcMod turb 0 0.1 0 1\n"
							  "\t\trgbGen wave sin 0 1 0 1\n\t\talphaGen const 0.5\n\t\tblendFunc add\n\t}\n")
				   .arg(stage);
	}
	big += QStringLiteral("}\n");
	definition = parsed(big, MaterialEngine::Quake3, &script);
	std::string overlap;
	expect(definition && laidOutWithoutOverlap(buildMaterialGraph(*definition), &overlap), "the Quake III layout has no overlapping nodes: " + overlap);

	checkRemovable(mods, kind, modsName, MaterialEngine::Quake3, "Quake III");
	checkRemovable(big, kind, QStringLiteral("textures/test/big"), MaterialEngine::Quake3, "Quake III, large");
}

void checkQuake3Graph()
{
	const QString text = QStringLiteral(
		"textures/test/glow // the glow\n{\n\tqer_editorimage textures/test/glow.tga\n\tsurfaceparm nolightmap\n"
		"\t{\n\t\tmap textures/test/glow.tga\n\t\ttcMod scroll 0.5 0\n\t\ttcMod rotate 90\n\t}\n"
		"\t{\n\t\tmap $lightmap\n\t\tblendFunc filter\n\t}\n}\n");
	MaterialScript script;
	const MaterialDefinition* definition = parsed(text, MaterialEngine::Quake3, &script);
	if (!expect(definition != nullptr, "the Quake III shader parses")) {
		return;
	}
	const MaterialGraph graph = buildMaterialGraph(*definition);
	expect(graph.node(QStringLiteral("material")) && graph.node(QStringLiteral("stage/0")) && graph.node(QStringLiteral("stage/0/image"))
			&& graph.node(QStringLiteral("stage/0/tcmod/1")) && graph.node(QStringLiteral("stage/1/image")),
		"the Quake III graph has output, stages, images and tcMods");
	expect(graph.node(QStringLiteral("stage/1/image"))->kind == QStringLiteral("lightmap"), "$lightmap is a lightmap node");
	bool chained = false;
	for (const MaterialGraphLink& link : graph.links) {
		chained |= link.fromNode == QStringLiteral("stage/0/tcmod/0") && link.toNode == QStringLiteral("stage/0/tcmod/1");
	}
	expect(chained, "tcMods are chained in order");
	expect(graph.node(QStringLiteral("stage/0"))->property(QStringLiteral("blend"))->value == QStringLiteral("opaque"), "an unblended stage is opaque");

	const MaterialTextKind kind = MaterialTextKind::Quake3Shader;
	const QString name = QStringLiteral("textures/test/glow");
	MaterialGraphEditResult result = apply(text, kind, name, MaterialGraphEditKind::SetProperty, QStringLiteral("stage/0"), QStringLiteral("blend"), QStringLiteral("add"));
	expect(result.ok && result.text.contains(QStringLiteral("blendFunc add")) && result.text.contains(QStringLiteral("// the glow")),
		"setting a stage's blend writes blendFunc and keeps comments");
	result = apply(result.text, kind, name, MaterialGraphEditKind::SetProperty, QStringLiteral("stage/0"), QStringLiteral("blend"), QStringLiteral("opaque"));
	expect(result.ok && !result.text.contains(QStringLiteral("blendFunc add")), "opaque removes blendFunc");
	result = apply(text, kind, name, MaterialGraphEditKind::SetProperty, QStringLiteral("stage/0/tcmod/0"), QStringLiteral("s"), QStringLiteral("0.25"));
	expect(result.ok && result.text.contains(QStringLiteral("tcMod scroll 0.25 0")), "a tcMod field rewrites its directive");
	result = apply(text, kind, name, MaterialGraphEditKind::RemoveNode, QStringLiteral("stage/0/tcmod/1"));
	expect(result.ok && !result.text.contains(QStringLiteral("rotate")), "removing a tcMod node removes its directive");
	result = apply(text, kind, name, MaterialGraphEditKind::MoveNode, QStringLiteral("stage/0/tcmod/1"), {}, {}, {}, -1);
	expect(result.ok && result.text.indexOf(QStringLiteral("rotate")) < result.text.indexOf(QStringLiteral("scroll")), "moving a tcMod reorders it");
	result = apply(text, kind, name, MaterialGraphEditKind::AddNode, QStringLiteral("stage/1"), {}, {}, QStringLiteral("q3.rgbgen.wave"));
	expect(result.ok && result.text.contains(QStringLiteral("rgbGen wave sin 0.5 0.5 0 1")), "adding an rgbGen wave node to a stage");
	result = apply(result.text, kind, name, MaterialGraphEditKind::SetProperty, QStringLiteral("stage/1/rgbgen"), QStringLiteral("frequency"), QStringLiteral("2"));
	expect(result.ok && result.text.contains(QStringLiteral("rgbGen wave sin 0.5 0.5 0 2")), "editing a wave field");
	result = apply(text, kind, name, MaterialGraphEditKind::MoveNode, QStringLiteral("stage/1"), {}, {}, {}, -1);
	MaterialScript moved;
	const MaterialDefinition* movedDefinition = parsed(result.text, MaterialEngine::Quake3, &moved);
	expect(result.ok && movedDefinition && movedDefinition->stages.first().imageKind == MaterialImageKind::Lightmap, "moving a stage up");
	result = apply(text, kind, name, MaterialGraphEditKind::AddNode, QStringLiteral("material"), {}, {}, QStringLiteral("q3.deform.wave"));
	expect(result.ok && result.text.contains(QStringLiteral("deformVertexes wave 64 sin 0 2 0 0.5")), "adding a deform");
	result = apply(text, kind, name, MaterialGraphEditKind::SetProperty, QStringLiteral("material"), QStringLiteral("surfaceparms"),
		QStringLiteral("trans nonsolid"));
	expect(result.ok && !result.text.contains(QStringLiteral("nolightmap")) && result.text.contains(QStringLiteral("surfaceparm trans"))
			&& result.text.contains(QStringLiteral("surfaceparm nonsolid")),
		"surface parameters are diffed into directives");
	result = apply(text, kind, name, MaterialGraphEditKind::SetProperty, QStringLiteral("material"), QStringLiteral("cull"), QStringLiteral("none"));
	expect(result.ok && result.text.contains(QStringLiteral("cull none")), "cull none");
	result = apply(text, kind, name, MaterialGraphEditKind::RemoveNode, QStringLiteral("stage/0/image"));
	expect(!result.ok, "a stage's image cannot be removed on its own");
	result = apply(text, kind, name, MaterialGraphEditKind::SetProperty, QStringLiteral("stage/0/image"), QStringLiteral("clamp"), QStringLiteral("true"));
	expect(result.ok && result.text.contains(QStringLiteral("clampmap textures/test/glow.tga")), "clamp turns map into clampmap");
	result = apply(text, kind, name, MaterialGraphEditKind::AddNode, QStringLiteral("material"), {}, {}, QStringLiteral("q3.stage.add"));
	MaterialScript added;
	const MaterialDefinition* addedDefinition = parsed(result.text, MaterialEngine::Quake3, &added);
	expect(result.ok && addedDefinition && addedDefinition->stages.size() == 3 && result.focusNode == QStringLiteral("stage/2"), "adding a stage");
}

void checkDoom3Graph()
{
	const QString text = QStringLiteral(
		"textures/test/panel\n{\n\tdiffusemap textures/test/panel\n\tbumpmap heightmap( textures/test/panel_h, 4 )\n"
		"\t{\n\t\tblend add\n\t\tmap textures/test/panel_add\n\t\trgb 0.5 + 0.5 * sinTable[ time ]\n\t\tscroll time * 0.1, 0\n\t}\n}\n");
	MaterialScript script;
	const MaterialDefinition* definition = parsed(text, MaterialEngine::Doom3, &script);
	if (!expect(definition != nullptr && definition->engineRejection.isEmpty(), "the Doom 3 material parses")) {
		return;
	}
	const MaterialGraph graph = buildMaterialGraph(*definition);
	expect(graph.node(QStringLiteral("stage/1/image")) && graph.node(QStringLiteral("stage/1/image"))->kind == QStringLiteral("program")
			&& graph.node(QStringLiteral("stage/1/image/0")),
		"image programs become node trees");
	expect(graph.node(QStringLiteral("stage/2/color/red/e")) && graph.node(QStringLiteral("stage/2/color/red/e/b/b/a")),
		"colour expressions become operator trees");
	expect(graph.node(QStringLiteral("stage/2/transform/0/a/e")), "transform arguments are expression nodes");
	const MaterialTextKind kind = MaterialTextKind::Doom3Material;
	const QString name = QStringLiteral("textures/test/panel");
	MaterialGraphEditResult result = apply(text, kind, name, MaterialGraphEditKind::SetProperty, QStringLiteral("stage/2/color/red/e/a"),
		QStringLiteral("value"), QStringLiteral("0.25"));
	expect(result.ok && result.text.contains(QStringLiteral("rgb 0.25 + 0.5 * sinTable[ time ]")), "editing a constant node rewrites the expression");
	result = apply(text, kind, name, MaterialGraphEditKind::WrapExpression, QStringLiteral("stage/2/color/red/e"), {}, QStringLiteral("multiply"));
	expect(result.ok && result.text.contains(QStringLiteral("( 0.5 + 0.5 * sinTable[ time ] ) * 1")), "wrapping keeps the grouping with parentheses");
	result = apply(text, kind, name, MaterialGraphEditKind::UnwrapExpression, QStringLiteral("stage/2/color/red/e"));
	expect(result.ok && result.text.contains(QStringLiteral("rgb 0.5\n")), "unwrapping keeps the first operand");
	result = apply(text, kind, name, MaterialGraphEditKind::SetProperty, QStringLiteral("stage/2/color/red/e/b/b"), QStringLiteral("table"),
		QStringLiteral("cosTable"));
	expect(result.ok && result.text.contains(QStringLiteral("cosTable[ time ]")), "renaming a table lookup");
	result = apply(text, kind, name, MaterialGraphEditKind::SetProperty, QStringLiteral("stage/2/color"), QStringLiteral("alpha"), QStringLiteral("0.5"));
	expect(result.ok && result.text.contains(QStringLiteral("color 0.5 + 0.5 * sinTable[ time ], 0.5 + 0.5 * sinTable[ time ], 0.5 + 0.5 * sinTable[ time ], 0.5")),
		"a different alpha turns rgb into color");
	result = apply(text, kind, name, MaterialGraphEditKind::SetProperty, QStringLiteral("stage/2/transform/0"), QStringLiteral("b"), QStringLiteral("time * 0.2"));
	expect(result.ok && result.text.contains(QStringLiteral("scroll time * 0.1, time * 0.2")), "editing a transform argument");
	result = apply(text, kind, name, MaterialGraphEditKind::SetProperty, QStringLiteral("stage/1/image"), QStringLiteral("scale"), QStringLiteral("8"));
	expect(result.ok && result.text.contains(QStringLiteral("bumpmap heightmap( textures/test/panel_h, 8 )")), "editing a shorthand's image program");
	result = apply(text, kind, name, MaterialGraphEditKind::WrapExpression, QStringLiteral("stage/0/image"), {}, QStringLiteral("makeintensity"));
	expect(result.ok && result.text.contains(QStringLiteral("diffusemap makeintensity( textures/test/panel )")), "wrapping an image in a program");
	result = apply(text, kind, name, MaterialGraphEditKind::ExpandShorthand, QStringLiteral("stage/0"));
	MaterialScript expanded;
	const MaterialDefinition* expandedDefinition = parsed(result.text, MaterialEngine::Doom3, &expanded);
	expect(result.ok && expandedDefinition && expandedDefinition->stages.size() == 3 && result.text.contains(QStringLiteral("blend diffusemap")),
		"expanding a shorthand makes a stage block");
	result = apply(text, kind, name, MaterialGraphEditKind::AddNode, QStringLiteral("stage/2"), {}, {}, QStringLiteral("d3.condition"));
	expect(result.ok && result.text.contains(QStringLiteral("if parm7 == 0")), "adding a condition");
	result = apply(text, kind, name, MaterialGraphEditKind::SetProperty, QStringLiteral("stage/2"), QStringLiteral("blend"), QStringLiteral("blend"));
	expect(result.ok && result.text.contains(QStringLiteral("blend blend")), "changing a stage's blend");
	result = apply(text, kind, name, MaterialGraphEditKind::SetProperty, QStringLiteral("material"), QStringLiteral("translucent"), QStringLiteral("true"));
	expect(result.ok && result.text.contains(QStringLiteral("\ttranslucent\n")), "setting a material flag");
	result = apply(text, kind, name, MaterialGraphEditKind::SetProperty, QStringLiteral("stage/2/color/red/e"), QStringLiteral("text"),
		QStringLiteral("time -"));
	expect(!result.ok, "a broken expression is refused before the text changes");
}

void checkDoom3Edits()
{
	const MaterialTextKind kind = MaterialTextKind::Doom3Material;
	// Expanding a shorthand keeps the draw order and selects the new block.
	const QString interactions = QStringLiteral(
		"textures/test/set\n{\n\tdiffusemap textures/test/a\n\tbumpmap textures/test/b\n\tspecularmap textures/test/c\n}\n");
	MaterialGraphEditResult result =
		apply(interactions, kind, QStringLiteral("textures/test/set"), MaterialGraphEditKind::ExpandShorthand, QStringLiteral("stage/1"));
	MaterialScript script;
	const MaterialDefinition* definition = parsed(result.text, MaterialEngine::Doom3, &script);
	expect(result.ok && definition && definition->stages.size() == 3 && definition->stages.at(1).shorthandDirective < 0
			&& definition->stages.at(1).role == MaterialStageRole::Bump && definition->stages.at(2).role == MaterialStageRole::Specular
			&& definition->stages.at(2).shorthandDirective >= 0 && result.focusNode == QStringLiteral("stage/1"),
		"expanding a shorthand puts the block where the shorthand was and selects it");

	// Image programs: wrapping needs a known function; every node marked
	// removable can be removed.
	const QString text = QStringLiteral(
		"textures/test/panel\n{\n\tdiffusemap textures/test/panel\n\tbumpmap addnormals( textures/test/n, heightmap( textures/test/h, 4 ) )\n"
		"\t{\n\t\tif parm7 == 0\n\t\tblend add\n\t\tmap textures/test/panel_add\n\t\tred 0.5 + 0.5 * sinTable[ time ]\n\t\tgreen parm1 * 2\n"
		"\t\tblue ( time * 0.5 ) % 1\n\t\talpha 1 - parm3\n\t\tscroll time * 0.1, time * -0.2\n\t\tscale 2 + parm4, 2 - parm5\n"
		"\t\tshear 0.1, sinTable[ time * 0.2 ]\n\t\talphaTest 0.5 * parm6\n\t}\n}\n");
	const QString name = QStringLiteral("textures/test/panel");
	result = apply(text, kind, name, MaterialGraphEditKind::WrapExpression, QStringLiteral("stage/0/image"), {}, QStringLiteral("frobnicate"));
	expect(!result.ok, "wrapping an image in an unknown function is refused");
	result = apply(text, kind, name, MaterialGraphEditKind::WrapExpression, QStringLiteral("stage/0/image"), {}, QStringLiteral("heightmap"));
	expect(result.ok && result.text.contains(QStringLiteral("diffusemap heightmap( textures/test/panel, 4 )"))
			&& remapMaterialGraphId(QStringLiteral("stage/0/image"), result.idMoves) == QStringLiteral("stage/0/image/0"),
		"wrapping an image in heightmap, and the image's id moves below it");
	definition = parsed(text, MaterialEngine::Doom3, &script);
	if (expect(definition != nullptr, "the Doom 3 fixture parses")) {
		const MaterialGraph graph = buildMaterialGraph(*definition);
		expect(graph.node(QStringLiteral("stage/1/image/0")) && !graph.node(QStringLiteral("stage/1/image/0"))->removable
				&& graph.node(QStringLiteral("stage/1/image/1"))->removable && graph.node(QStringLiteral("stage/1/image"))->removable,
			"in an image program only function nodes are removable");
		expect(graph.node(QStringLiteral("stage/1/image"))->programNode, "image program nodes say they can be wrapped");
		std::string overlap;
		expect(laidOutWithoutOverlap(graph, &overlap), "the Doom 3 layout has no overlapping nodes: " + overlap);
		QSet<QString> names;
		for (const MaterialGraphNode& node : graph.nodes) {
			if (node.kind == QStringLiteral("stage") || node.kind == QStringLiteral("texgen")) {
				names.insert(materialGraphNodeLabel(node));
			}
		}
		expect(names.size() == 4, "stages and their coordinates have distinct labels");
	}
	checkRemovable(text, kind, name, MaterialEngine::Doom3, "Doom 3");
	result = apply(text, kind, name, MaterialGraphEditKind::RemoveNode, QStringLiteral("stage/2/transform/1"));
	expect(result.ok && result.focusNode == QStringLiteral("stage/2/transform/0"), "removing a transform selects the one before it");
	result = apply(text, kind, name, MaterialGraphEditKind::AddNode, QStringLiteral("stage/2"), {}, {}, QStringLiteral("d3.transform.rotate"));
	expect(result.ok && result.focusNode == QStringLiteral("stage/2/transform/3"), "adding a transform selects it");

	// A colour edit rewrites the colour line in place and keeps its comment.
	const QString colored = QStringLiteral("textures/test/tint\n{\n\t{\n\t\tblend add\n\t\tmap textures/a\n\t\trgb 0.5 // dim\n\t}\n}\n");
	result = apply(colored, kind, QStringLiteral("textures/test/tint"), MaterialGraphEditKind::SetProperty, QStringLiteral("stage/0/color"),
		QStringLiteral("alpha"), QStringLiteral("0.25"));
	expect(result.ok && result.text.contains(QStringLiteral("color 0.5, 0.5, 0.5, 0.25 // dim")), "a colour edit keeps the line's comment");

	// One edit in a large script costs about the same as in a small one:
	// only the material's own text is re-read per primitive edit, and the
	// rest of the script stays byte for byte.
	QString large;
	for (int index = 0; index < 4000; ++index) {
		large += QStringLiteral("textures/gen/m%1\n{\n\tqer_editorimage textures/gen/m%1\n\tdiffusemap textures/gen/m%1_d\n\t{\n\t\tblend add\n"
								"\t\tmap textures/gen/m%1_glow\n\t\tred 0.5\n\t\tgreen 0.5\n\t\tblue 0.5\n\t\tscroll time * 0.1, 0\n\t}\n}\n\n")
					 .arg(index);
	}
	const QString target = QStringLiteral("textures/gen/m2000");
	QElapsedTimer timer;
	timer.start();
	result = apply(large, kind, target, MaterialGraphEditKind::SetProperty, QStringLiteral("stage/1/color"), QStringLiteral("alpha"), QStringLiteral("0.5"));
	const qint64 elapsed = timer.elapsed();
	const int start = static_cast<int>(large.indexOf(target + QLatin1Char('\n')));
	const int end = static_cast<int>(large.indexOf(QStringLiteral("textures/gen/m2001\n")));
	expect(result.ok && result.text.left(start) == large.left(start) && result.text.mid(result.text.indexOf(QStringLiteral("textures/gen/m2001\n"))) == large.mid(end)
			&& result.text.contains(QStringLiteral("\t\trgba 0.5\n")),
		"an edit in a large script changes only its material");
	std::cout << "  colour edit in a " << large.size() / 1024 << " KiB script: " << elapsed << " ms\n";
	expect(elapsed < 30000, "an edit in a large script finishes within its budget");
}

void checkClassicGraph()
{
	QVector<DoomAnimationRange> ranges = doomVanillaAnimations();
	const QString swantbls = swantblsText(ranges, doomVanillaSwitches());
	MaterialGraphEdit edit;
	edit.kind = MaterialGraphEditKind::SetProperty;
	edit.node = QStringLiteral("animation");
	edit.property = QStringLiteral("tics");
	edit.value = QStringLiteral("4");
	edit.related = {QStringLiteral("NUKAGE2"), QStringLiteral("NUKAGE3"), QStringLiteral("NUKAGE1")};
	MaterialGraphEditResult result = applyMaterialGraphEdit(swantbls, MaterialTextKind::DoomSwantbls, QStringLiteral("NUKAGE2"), edit);
	QVector<DoomAnimationRange> parsedRanges;
	QVector<DoomSwitchPair> parsedSwitches;
	QVector<MaterialDiagnostic> diagnostics;
	expect(result.ok && parseSwantblsText(result.text, &parsedRanges, &parsedSwitches, &diagnostics) && parsedRanges.first().tics == 4,
		"a range's tics change from any member");
	edit.node = QStringLiteral("switch");
	edit.property = QStringLiteral("partner");
	edit.value = QStringLiteral("SW2TEST");
	result = applyMaterialGraphEdit(swantbls, MaterialTextKind::DoomSwantbls, QStringLiteral("SW1TEST"), edit);
	expect(result.ok && result.text.contains(QStringLiteral("SW1TEST")) && result.text.contains(QStringLiteral("SW2TEST")), "adding a switch pair");
	const QString animdefs = QStringLiteral("warp flat WATER1\n");
	edit.node = QStringLiteral("warp");
	edit.property = QStringLiteral("speed");
	edit.value = QStringLiteral("2");
	result = applyMaterialGraphEdit(animdefs, MaterialTextKind::DoomAnimdefs, QStringLiteral("WATER1"), edit);
	expect(result.ok && result.text.contains(QStringLiteral("warp flat WATER1 2")), "a warp's speed changes in ANIMDEFS");
	edit.node = QStringLiteral("flags");
	edit.property = QStringLiteral("flag:warp");
	edit.value = QStringLiteral("true");
	result = applyMaterialGraphEdit(QStringLiteral("{\"flags\": 1, \"value\": 300}"), MaterialTextKind::Quake2WalJson, QStringLiteral("e1u1/water"), edit);
	Quake2WalInfo info;
	QString error;
	expect(result.ok && parseQuake2WalInfoText(result.text, &info, &error) && info.surfaceFlags == (kQuake2SurfLight | kQuake2SurfWarp) && info.value == 300,
		"a Quake II flag toggles in .wal_json");
	MaterialDefinition water = quakeMaterialDefinition(QStringLiteral("+0slime"), {QStringLiteral("+1slime")});
	const MaterialGraph graph = buildMaterialGraph(water);
	expect(graph.node(QStringLiteral("frame/1")) && graph.node(QStringLiteral("animation")) && graph.node(QStringLiteral("info")),
		"Quake frames, animation and name rules show as nodes");
	expect(!materialGraphNodeTemplates(MaterialEngine::Quake3).isEmpty() && !materialGraphNodeTemplates(MaterialEngine::Doom3).isEmpty(),
		"the palettes list node templates");
	QVector<MaterialGraphEdit> edits;
	expect(parseMaterialGraphEditsJson(QByteArray(R"([{"edit":"set","node":"stage/0","property":"blend","value":"add"}])"), &edits, &error)
			&& edits.size() == 1,
		"graph edits read from JSON");
	edits.clear();
	expect(parseMaterialGraphEditsJson(QByteArray(R"([{"op":"move","node":"stage/1","step":-1},{"node":"stage/0","property":"blend","value":true}])"), &edits,
			   &error)
			&& edits.size() == 2 && edits.at(0).kind == MaterialGraphEditKind::MoveNode && edits.at(0).step == -1
			&& edits.at(1).kind == MaterialGraphEditKind::SetProperty && edits.at(1).step == 1,
		"'op' names a graph edit's kind like 'edit' does: " + error.toStdString());
	const auto graphRefused = [](const char* json, const QString& needle) {
		QVector<MaterialGraphEdit> ignored;
		QString message;
		return !parseMaterialGraphEditsJson(QByteArray(json), &ignored, &message) && message.contains(needle);
	};
	expect(graphRefused(R"([{"edit":"move","op":"move","node":"stage/0"}])", QStringLiteral("both 'edit' and 'op'")), "'edit' and 'op' together are refused");
	expect(graphRefused(R"([{"edit":"move","node":"stage/0","step":0.5}])", QStringLiteral("whole number")), "a fractional step is refused");
	expect(graphRefused(R"([{"edit":"move","node":"stage/0","step":"1"}])", QStringLiteral("whole number")), "a step given as text is refused");
	expect(graphRefused(R"([{"edit":"set","node":3,"property":"blend","value":"add"}])", QStringLiteral("'node' must be text")), "a numeric node is refused");
	expect(graphRefused(R"([{"edit":"set","node":"stage/0","property":["blend"],"value":"add"}])", QStringLiteral("'property' must be text")),
		"a property given as an array is refused");
	expect(graphRefused(R"([{"edit":"add","template":{},"node":"stage/0"}])", QStringLiteral("'template' must be text")), "an object template is refused");
	expect(graphRefused(R"([{"edit":7}])", QStringLiteral("'edit' must be text")), "a numeric kind is refused");
	expect(graphRefused(R"([{"edit":"set","node":"stage/0","property":"blend","value":["add"]}])", QStringLiteral("'value'")),
		"a value given as an array is refused");
	expect(graphRefused(R"([{"edit":"set","node":"stage/0","property":"blend","value":null}])", QStringLiteral("'value'")), "a null value is refused");
	expect(graphRefused(R"({"edit":"set"})", QStringLiteral("JSON object")), "an object instead of an array says what the file holds");

	// SWANTBLS edits keep the user's comments, layout and line endings.
	const QString table = QStringLiteral(
		"# my animations\r\n[FLATS]\r\n#spd    last        first\r\n8       NUKAGE3     NUKAGE1 # slime\r\n\r\n[TEXTURES]\r\n8       SLADRIP3    SLADRIP1\r\n");
	MaterialGraphEdit tics;
	tics.kind = MaterialGraphEditKind::SetProperty;
	tics.node = QStringLiteral("animation");
	tics.property = QStringLiteral("tics");
	tics.value = QStringLiteral("12");
	tics.related = {QStringLiteral("NUKAGE1"), QStringLiteral("NUKAGE2"), QStringLiteral("NUKAGE3")};
	result = applyMaterialGraphEdit(table, MaterialTextKind::DoomSwantbls, QStringLiteral("NUKAGE2"), tics);
	const QStringList before = table.split(QStringLiteral("\r\n"));
	const QStringList after = result.text.split(QStringLiteral("\r\n"));
	int differing = 0;
	for (int line = 0; line < std::min(before.size(), after.size()); ++line) {
		differing += before.at(line) != after.at(line) ? 1 : 0;
	}
	expect(result.ok && before.size() == after.size() && differing == 1 && result.text.contains(QStringLiteral("# my animations\r\n"))
			&& result.text.contains(QStringLiteral("12      NUKAGE3     NUKAGE1 # slime\r\n")) && !QString(result.text).remove(QStringLiteral("\r\n")).contains(QLatin1Char('\n')),
		"a tics edit changes one line and keeps comments, columns and CRLF");
	MaterialGraphEdit last = tics;
	last.property = QStringLiteral("last");
	last.value = QStringLiteral("NUKAGE2");
	result = applyMaterialGraphEdit(table, MaterialTextKind::DoomSwantbls, QStringLiteral("NUKAGE1"), last);
	expect(result.ok && parseSwantblsText(result.text, &parsedRanges, &parsedSwitches, &diagnostics) && parsedRanges.first().last == QStringLiteral("NUKAGE2"),
		"a range's last picture is editable");
	last.value = QStringLiteral("NUKAGE1");
	expect(!applyMaterialGraphEdit(table, MaterialTextKind::DoomSwantbls, QStringLiteral("NUKAGE1"), last).ok,
		"a range whose last picture is its first is refused");

	// Adding a range finds its last picture; one picture alone is refused.
	MaterialGraphEdit range;
	range.kind = MaterialGraphEditKind::AddNode;
	range.node = QStringLiteral("material");
	range.nodeTemplate = QStringLiteral("doom.range");
	range.value = QStringLiteral("flat");
	range.related = {QStringLiteral("BLOOD1"), QStringLiteral("BLOOD2"), QStringLiteral("BLOOD3"), QStringLiteral("FLAT5")};
	result = applyMaterialGraphEdit(table, MaterialTextKind::DoomSwantbls, QStringLiteral("BLOOD1"), range);
	bool added = false;
	if (result.ok && parseSwantblsText(result.text, &parsedRanges, &parsedSwitches, &diagnostics)) {
		for (const DoomAnimationRange& each : parsedRanges) {
			added |= each.first == QStringLiteral("BLOOD1") && each.last == QStringLiteral("BLOOD3") && !each.texture;
		}
	}
	expect(added && result.text.contains(QStringLiteral("# my animations")), "a new range runs to the last picture with the same name");
	range.related = {QStringLiteral("BLOOD1")};
	expect(!applyMaterialGraphEdit(table, MaterialTextKind::DoomSwantbls, QStringLiteral("BLOOD1"), range).ok,
		"a range of one picture is refused (Boom stops at start-up)");
	range.related = {QStringLiteral("NUKAGE1"), QStringLiteral("NUKAGE3")};
	expect(!applyMaterialGraphEdit(table, MaterialTextKind::DoomSwantbls, QStringLiteral("NUKAGE1"), range).ok,
		"a texture already in a range is not added again");
	MaterialGraphEdit animdefsRange = range;
	animdefsRange.nodeTemplate = QStringLiteral("doom.animdefs.range");
	animdefsRange.related = {QStringLiteral("NUKAGE1"), QStringLiteral("NUKAGE2"), QStringLiteral("NUKAGE3")};
	result = applyMaterialGraphEdit(animdefs, MaterialTextKind::DoomAnimdefs, QStringLiteral("NUKAGE1"), animdefsRange);
	expect(result.ok && result.text.contains(QStringLiteral("NUKAGE3")) && result.text.contains(QStringLiteral("flat NUKAGE1")),
		"an ANIMDEFS range ends at the last picture with the same name");
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	checkQuake3Graph();
	checkQuake3Edits();
	checkDoom3Graph();
	checkDoom3Edits();
	checkClassicGraph();
	if (failures > 0) {
		std::cerr << failures << " graph check(s) failed\n";
		return 1;
	}
	std::cout << "material graph smoke passed\n";
	return 0;
}
