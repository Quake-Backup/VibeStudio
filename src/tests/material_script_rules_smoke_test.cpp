// Engine rules of the Quake III and Doom 3 material scripts.
// No game data: every file is generated here.

#include "core/material_eval.h"
#include "core/material_script.h"

#include <QCoreApplication>
#include <QElapsedTimer>

#include <cmath>
#include <iostream>
#include <string>

using namespace slopstudio;

namespace {

int failures = 0;

[[maybe_unused]] bool expect(bool condition, const std::string& message)
{
	if (!condition) {
		std::cerr << "FAIL: " << message << '\n';
		++failures;
	}
	return condition;
}

bool near(double a, double b, double tolerance = 1.0e-6)
{
	return std::abs(a - b) <= tolerance;
}

MaterialScript quake3(const QString& text, MaterialDialect dialect = MaterialDialect::Quake3)
{
	return parseMaterialScript(text, MaterialEngine::Quake3, QString(), dialect);
}

MaterialScript doom3(const QString& text)
{
	return parseMaterialScript(text, MaterialEngine::Doom3);
}

bool hasCode(const MaterialDefinition* definition, const QString& code)
{
	if (!definition) {
		return false;
	}
	for (const MaterialDiagnostic& diagnostic : definition->diagnostics) {
		if (diagnostic.code == code) {
			return true;
		}
	}
	return false;
}

bool scriptHasCode(const MaterialScript& script, const QString& code)
{
	for (const MaterialDiagnostic& diagnostic : script.diagnostics) {
		if (diagnostic.code == code) {
			return true;
		}
	}
	return false;
}

QString spanText(const MaterialScript& script, const MaterialSourceSpan& span)
{
	return span.isValid() ? script.text.mid(span.start, span.length()) : QString();
}

// A1: a mistake inside one definition stays inside it.
void checkErrorsStayInTheirDefinition()
{
	{
		const MaterialScript script = doom3(QStringLiteral("a\n{\n\t{\n\t\tmap x\n\t\trgb\n\t}\n}\nb\n{\n\t{\n\t\tmap y\n\t}\n}\n"));
		const MaterialDefinition* a = script.find(QStringLiteral("a"));
		const MaterialDefinition* b = script.find(QStringLiteral("b"));
		expect(a && !a->engineRejection.isEmpty(), "Doom 3: 'rgb' with no expression defaults the material");
		expect(b && b->engineRejection.isEmpty() && b->stages.size() == 1, "Doom 3: the material after a defaulted one is still read");
		expect(a && spanText(script, a->span).endsWith(QStringLiteral("}\n}")) && !spanText(script, a->span).contains(QStringLiteral("map y")),
			"Doom 3: a defaulted material's span ends at its own closing brace");
		expect(a && !hasCode(a, QStringLiteral("unexpected-brace")) && !scriptHasCode(script, QStringLiteral("unexpected-brace")),
			"Doom 3: no stray brace is left behind");
	}
	{
		const MaterialScript script = doom3(QStringLiteral("a\n{\n\t{\n\t\tmap\n\t}\n}\nb\n{\n\t{\n\t\tmap y\n\t}\n}\n"));
		const MaterialDefinition* a = script.find(QStringLiteral("a"));
		const MaterialDefinition* b = script.find(QStringLiteral("b"));
		expect(a && !a->engineRejection.isEmpty(), "Doom 3: 'map' with nothing after it defaults the material");
		expect(script.materials.size() == 2 && b && b->engineRejection.isEmpty(), "Doom 3: 'map' at a stage end does not swallow the next material");
	}
	{
		const MaterialScript script = doom3(QStringLiteral("a\n{\n\t{\n\t\tmap x\n\t\t{\n\t\t}\n\t}\n}\nb\n{\n\tdiffusemap y\n}\n"));
		const MaterialDefinition* a = script.find(QStringLiteral("a"));
		const MaterialDefinition* b = script.find(QStringLiteral("b"));
		expect(a && !a->engineRejection.isEmpty() && b && b->engineRejection.isEmpty() && script.diagnostics.isEmpty(),
			"Doom 3: a nested brace defaults only its own material");
	}
	{
		const MaterialScript script = quake3(QStringLiteral("a\n{\n\t{\n\t\tmap x.tga\n\t\t{\n\t\t}\n\t}\n}\nb\n{\n\t{\n\t\tmap y.tga\n\t}\n}\n"));
		const MaterialDefinition* a = script.find(QStringLiteral("a"));
		const MaterialDefinition* b = script.find(QStringLiteral("b"));
		expect(a && !a->engineRejection.isEmpty() && a->stages.size() == 1, "Quake III: a nested brace drops its shader, which keeps one stage");
		expect(b && b->engineRejection.isEmpty() && script.diagnostics.isEmpty(), "Quake III: the shader after a nested brace is still read");
	}
	{
		// The stage's closing brace on the same line is not an argument.
		const MaterialScript script = quake3(QStringLiteral("a\n{\n\t{ map x.tga rgbGen identity }\n}\n"));
		const MaterialDefinition* a = script.find(QStringLiteral("a"));
		expect(a && a->engineRejection.isEmpty() && a->stages.size() == 1 && a->stages.first().imagePath == QStringLiteral("x.tga")
				&& a->stages.first().rgbGen.source == MaterialColorSource::Identity,
			"Quake III: a one-line stage keeps its closing brace");
	}
	{
		// Line and column come from the lexer's own walk.
		const MaterialScript script = doom3(QStringLiteral("a\n{\n\n   bogus\n}\n"));
		const MaterialDefinition* a = script.find(QStringLiteral("a"));
		bool placed = false;
		for (const MaterialDiagnostic& diagnostic : a ? a->diagnostics : QVector<MaterialDiagnostic>()) {
			placed = placed || (diagnostic.line == 4 && diagnostic.column == 4 && diagnostic.length == 5);
		}
		expect(placed, "diagnostics point at the right line and column");
	}
}

// A2 and A11: Quake III's rules for missing and malformed parameters.
void checkQuake3Rules()
{
	{
		const MaterialScript script = quake3(QStringLiteral("a\n{\n\t{\n\t\tmap a.tga\n\t\tblendFunc\n\t}\n}\n"
															"b\n{\n\t{\n\t\tmap b.tga\n\t\tblendFunc GL_ONE\n\t}\n}\n"
															"c\n{\n\t{\n\t\tmap c.tga\n\t\trgbGen\n\t\talphaGen\n\t}\n}\n"));
		for (const QString& name : {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")}) {
			const MaterialDefinition* definition = script.find(name);
			expect(definition && definition->engineRejection.isEmpty() && hasCode(definition, QStringLiteral("missing-argument")),
				"Quake III keeps a shader whose blendFunc, rgbGen or alphaGen has no parameter: " + name.toStdString());
		}
		const MaterialDefinition* b = script.find(QStringLiteral("b"));
		expect(b && b->stages.first().blend.isOpaqueReplace() && b->stages.first().depthWrite, "an ignored blendFunc line leaves the stage opaque");
	}
	{
		const MaterialScript script = quake3(QStringLiteral("fog\n{\n\tfogparms 0.5 0.5 0.5 512\n}\n"));
		expect(!script.materials.first().engineRejection.isEmpty(), "Quake III drops a shader whose fogParms colour has no parentheses");
	}
	{
		const MaterialScript script = quake3(QStringLiteral("w\n{\n\tdeformVertexes wave 0 sin 0 1 0 1\n\t{\n\t\tmap w.tga\n\t}\n}\n"));
		const MaterialDefinition& w = script.materials.first();
		expect(w.deforms.size() == 1 && near(w.deforms.first().spread, 100.0), "a zero wave spread falls back to 100, as in ioquake3");
	}
	{
		const MaterialScript script = quake3(QStringLiteral("l\n{\n\t{\n\t\ttcGen environment\n\t\tmap $lightmap\n\t}\n}\n"));
		expect(script.materials.first().stages.first().tcGen.source == MaterialTexCoordSource::Environment,
			"an explicit tcGen before map $lightmap wins");
		const MaterialScript plain = quake3(QStringLiteral("l\n{\n\t{\n\t\tmap $lightmap\n\t}\n}\n"));
		expect(plain.materials.first().stages.first().tcGen.source == MaterialTexCoordSource::Lightmap, "map $lightmap defaults to lightmap coordinates");
	}
	{
		const MaterialScript script = quake3(QStringLiteral("c\n{\n\t{\n\t\tmap c.tga\n\t\trgbGen const ( 1 1 )\n\t}\n}\n"));
		const auto& color = script.materials.first().stages.first().rgbGen.constant;
		expect(near(color[0], 1.0) && near(color[1], 1.0) && near(color[2], 0.0), "a malformed rgbGen const keeps what it read and is black after");
	}
	{
		const MaterialScript script = quake3(QStringLiteral("s\n{\n\tskyParms env/sky 512\n}\n"));
		expect(!script.materials.first().sky.present && !script.materials.first().isSky(), "skyParms with a missing parameter is no sky");
	}
	{
		const MaterialScript script = quake3(QStringLiteral("t\n{\n\tdeformVertexes text\n\t{\n\t\tmap t.tga\n\t}\n}\n"));
		const MaterialDefinition& t = script.materials.first();
		expect(t.deforms.size() == 1 && t.deforms.first().kind == MaterialDeformKind::Text && !hasCode(&t, QStringLiteral("bad-arguments")),
			"deformVertexes text without a digit is text0");
	}
	{
		// OpenJK reads every stage keyword starting "ss" as a surface-sprite
		// option and only warns about unknown ones.
		const MaterialScript script = quake3(QStringLiteral("g\n{\n\t{\n\t\tmap g.tga\n\t\tssfadmax 100\n\t}\n}\n"), MaterialDialect::JediKnight);
		expect(script.materials.first().engineRejection.isEmpty(), "Jedi Knight keeps a shader with an unknown ss option");
	}
	{
		expect(detectMaterialDialect(QStringLiteral("a\n{\n/*\nsurfacesprites vertical 16 16 0.5 100\n*/\n\t{\n\t\tmap a.tga\n\t}\n}\n"))
				== MaterialDialect::Quake3,
			"dialect detection skips keywords inside block comments");
		expect(detectMaterialDialect(QStringLiteral("a\n{\n\t{\n\t\tmap a.tga\n\t\tsurfacesprites vertical 16 16 0.5 100\n\t}\n}\n"))
				== MaterialDialect::JediKnight,
			"dialect detection still finds keywords outside comments");
	}
	{
		// The first drawn blended stage decides the sort, not any later one.
		const MaterialScript script = quake3(QStringLiteral("b\n{\n\t{\n\t\tmap a.tga\n\t\tblendFunc add\n\t}\n\t{\n\t\tmap b.tga\n\t\tblendFunc add\n\t\tdepthWrite\n\t}\n}\n"
															"sky\n{\n\tsort opaque\n\tskyParms - 512 -\n\t{\n\t\tmap c.tga\n\t}\n}\n"
															"fogonly\n{\n\tsort opaque\n}\n"));
		expect(script.find(QStringLiteral("b"))->sort == QStringLiteral("blend0"), "the first blended stage's depth write sets the sort");
		expect(script.find(QStringLiteral("sky"))->sort == QStringLiteral("sky"), "a sky always sorts as the environment");
		expect(script.find(QStringLiteral("fogonly"))->sort == QStringLiteral("fog"), "a shader without stages sorts as fog");
	}
}

// A7: ioquake3 ignores a whole file with a missing brace.
void checkBrokenFiles()
{
	const QString missingClose = QStringLiteral("a\n{\n\t{\n\t\tmap a.tga\n\t}\n}\nb\n{\n");
	const QString missingOpen = QStringLiteral("a\n{\n\t{\n\t\tmap a.tga\n\t}\n}\nb\nc\n{\n}\n");
	const auto rejected = [](const MaterialScript& script) {
		const MaterialDefinition* a = script.find(QStringLiteral("a"));
		return a && !a->engineRejection.isEmpty();
	};
	const MaterialScript ioq3 = quake3(missingClose, MaterialDialect::Quake3);
	expect(rejected(ioq3) && scriptHasCode(ioq3, QStringLiteral("script-ignored")), "ioquake3 ignores a file with a missing closing brace");
	expect(rejected(quake3(missingOpen, MaterialDialect::Quake3)), "ioquake3 ignores a file with a missing opening brace");
	expect(!rejected(quake3(missingClose, MaterialDialect::Quake3Original)), "Quake III 1.32 keeps the other shaders");
	expect(!rejected(quake3(missingClose, MaterialDialect::Wolfenstein)), "iortcw keeps a file with a missing closing brace");
	expect(rejected(quake3(missingOpen, MaterialDialect::Wolfenstein)), "iortcw ignores a file with a missing opening brace");
	expect(!rejected(quake3(missingOpen, MaterialDialect::EnemyTerritory)) && !rejected(quake3(missingOpen, MaterialDialect::JediKnight)),
		"Enemy Territory and OpenJK keep the other shaders");
}

// A6 and A12: Doom 3 program stages and the errors that default a material.
void checkDoom3Rules()
{
	{
		const MaterialScript script = doom3(QStringLiteral("textures/sfx/haze\n{\n\ttranslucent\n\t{\n\t\tprogram heatHaze.vfp\n\t\tvertexParm 0 time * 0.1, time * 0.5\n"
														   "\t\tvertexParm 1 1.5\n\t\tfragmentMap 0 _currentRender\n\t\tfragmentMap 1 textures/sfx/haze_n\n\t}\n}\n"
														   "odd\n{\n\t{\n\t\tprogram mine.vfp\n\t\tfragmentMap 0 x\n\t}\n}\n"));
		const MaterialDefinition* haze = script.find(QStringLiteral("textures/sfx/haze"));
		expect(haze && !hasCode(haze, QStringLiteral("stage-without-image")), "a program stage has no 'no image' warning");
		expect(haze && !hasCode(haze, QStringLiteral("not-simulated")), "the approximated heat haze program has no 'not simulated' note");
		expect(hasCode(script.find(QStringLiteral("odd")), QStringLiteral("not-simulated")), "an unknown program keeps its note");
		if (haze && haze->stages.size() == 1 && haze->stages.first().vertexParms.size() == 2) {
			const auto& stage = haze->stages.first();
			const auto& one = stage.vertexParms.at(1);
			expect(one[0] >= 0 && one[1] == one[0] && one[2] == one[0] && one[3] == one[0], "one vertexParm value fills all four");
			const auto& two = stage.vertexParms.at(0);
			expect(two[2] >= 0 && near(haze->expressions.at(two[2]).value, 0.0) && two[3] >= 0 && near(haze->expressions.at(two[3]).value, 1.0),
				"two vertexParm values get z = 0 and w = 1");
		} else {
			expect(false, "the haze stage keeps its vertexParms");
		}
	}
	const auto defaults = [](const QString& body, const std::string& what) {
		const MaterialScript script = doom3(QStringLiteral("m\n{\n%1\n}\n").arg(body));
		expect(!script.materials.isEmpty() && !script.materials.first().engineRejection.isEmpty(), "Doom 3 defaults " + what);
	};
	defaults(QStringLiteral("\t{\n\t\tblend GL_FOO, GL_ONE\n\t\tmap x\n\t}"), "an unknown blend factor");
	defaults(QStringLiteral("\t{\n\t\tblend GL_ONE, GL_DST_COLOR\n\t\tmap x\n\t}"), "a source-only factor used as the destination");
	defaults(QStringLiteral("\tsort\n\t{\n\t\tmap x\n\t}"), "a sort with no parameter");
	defaults(QStringLiteral("\t{\n\t\tprogram a.vfp\n\t\tvertexParm 9 1\n\t}"), "a vertexParm above 3");
	defaults(QStringLiteral("\t{\n\t\tprogram a.vfp\n\t\tfragmentMap 8 x\n\t}"), "a fragmentMap above 7");
	{
		const MaterialScript script = doom3(QStringLiteral("m\n{\n\t{\n\t\tmap x\n\t\talphaTest 0.5\n\t}\n\ttranslucent\n}\n"));
		expect(script.materials.first().coverage == QStringLiteral("translucent"), "the last coverage keyword in the text wins");
		const MaterialScript other = doom3(QStringLiteral("m\n{\n\ttranslucent\n\t{\n\t\tmap x\n\t\talphaTest 0.5\n\t}\n}\n"));
		expect(other.materials.first().coverage == QStringLiteral("perforated"), "an alphaTest after translucent makes it perforated");
	}
}

// A3, A4 and A5: edits on implicit stages, stage moves and names.
void checkEdits()
{
	{
		const MaterialScript script = parseMaterialScript(QStringLiteral("x\n{\n\timplicitMap -\n}\n"), MaterialEngine::Quake3, QString(),
			MaterialDialect::EnemyTerritory);
		MaterialEdit edit;
		edit.material = QStringLiteral("x");
		edit.stage = 0;
		edit.kind = MaterialEditKind::RemoveStage;
		expect(!applyMaterialEdit(script, edit).ok, "an implicit stage cannot be removed");
		edit.kind = MaterialEditKind::MoveStage;
		edit.position = 1;
		expect(!applyMaterialEdit(script, edit).ok, "an implicit stage cannot be moved");
		edit.kind = MaterialEditKind::AddStage;
		edit.position = 0;
		edit.text = QStringLiteral("map y.tga");
		expect(!applyMaterialEdit(script, edit).ok, "no stage is added before an implicit one");
		edit.kind = MaterialEditKind::SetDirective;
		edit.keyword = QStringLiteral("blendFunc");
		edit.arguments = QStringLiteral("add");
		const MaterialEditResult set = applyMaterialEdit(script, edit);
		expect(!set.ok && set.error.contains(QStringLiteral("implicit")), "a directive edit on an implicit stage says why it fails");
	}
	{
		const MaterialScript script = quake3(QStringLiteral("m\n{\n\t{\n\t\tmap $lightmap\n\t}\n\t{\n\t\tmap foo.tga // map comment\n"
															"\t\t// a whole-line note\n\t\tblendFunc filter\n\t}\n}\n"));
		MaterialEdit edit;
		edit.kind = MaterialEditKind::MoveStage;
		edit.material = QStringLiteral("m");
		edit.stage = 1;
		edit.position = 0;
		const MaterialEditResult result = applyMaterialEdit(script, edit);
		const MaterialDefinition* moved = result.ok ? result.script.find(QStringLiteral("m")) : nullptr;
		expect(result.ok && result.text.contains(QStringLiteral("map foo.tga // map comment")) && result.text.contains(QStringLiteral("// a whole-line note")),
			"moving a stage keeps its comments");
		expect(moved && moved->stages.size() == 2 && moved->stages.first().imagePath == QStringLiteral("foo.tga"), "the moved stage comes first");
	}
	{
		const MaterialScript script = doom3(QStringLiteral("m\n{\n\t{\n\t\tmap a\n\t}\n\t{\n\t\tfoo\n\t\tmap x\n\t}\n}\n"));
		MaterialEdit edit;
		edit.kind = MaterialEditKind::MoveStage;
		edit.material = QStringLiteral("m");
		edit.stage = 1;
		edit.position = 0;
		const MaterialEditResult result = applyMaterialEdit(script, edit);
		expect(result.ok && result.text.contains(QStringLiteral("map x")) && result.text.contains(QStringLiteral("foo")),
			"moving a stage of a defaulted material keeps the text the parser skipped");
	}
	{
		const MaterialScript script = quake3(QStringLiteral("m\n{\n\t{\n\t\tmap a.tga\n\t}\n}\n"));
		MaterialEdit edit;
		edit.kind = MaterialEditKind::MoveStage;
		edit.material = QStringLiteral("m");
		edit.stage = 0;
		edit.position = 0;
		const MaterialEditResult result = applyMaterialEdit(script, edit);
		expect(result.ok && result.text == script.text && result.script.materials.size() == 1, "a move onto itself changes nothing");
	}
	{
		const MaterialScript q3 = quake3(QStringLiteral("textures/a\n{\n\t{\n\t\tmap a.tga\n\t}\n}\n"));
		const MaterialScript d3 = doom3(QStringLiteral("textures/a\n{\n\tdiffusemap a\n}\n"));
		const auto renamed = [](const MaterialScript& script, const QString& name) {
			MaterialEdit edit;
			edit.kind = MaterialEditKind::RenameDefinition;
			edit.material = QStringLiteral("textures/a");
			edit.text = name;
			return applyMaterialEdit(script, edit).ok;
		};
		expect(!renamed(d3, QStringLiteral("textures/a-b")), "Doom 3 refuses a name its lexer splits at '-'");
		expect(!renamed(d3, QStringLiteral("table")), "Doom 3 refuses a decl type word as a name");
		expect(!renamed(d3, QStringLiteral("x(y")), "Doom 3 refuses a name with parentheses");
		expect(!renamed(d3, QStringLiteral("2fast")), "Doom 3 refuses a name starting with a digit");
		expect(!renamed(q3, QStringLiteral("//x")), "Quake III refuses a name that reads as a comment");
		expect(!renamed(q3, QStringLiteral("a b")), "Quake III refuses a name with a space");
		expect(renamed(q3, QStringLiteral("textures/a-b_2")) && renamed(d3, QStringLiteral("textures/b_2")), "plain paths still rename");
		QString error;
		expect(!isValidMaterialName(MaterialEngine::Doom3, QStringLiteral("a+b"), &error) && !error.isEmpty(), "isValidMaterialName explains a refusal");
		expect(isValidMaterialName(MaterialEngine::Quake3, QStringLiteral("models/mapobjects/a-b"), nullptr), "Quake III keeps '-' inside a name");
	}
}

// A8, A9 and A16: model queries.
void checkModel()
{
	{
		const MaterialScript script = doom3(QStringLiteral("m\n{\n\t{\n\t\tmap _white\n\t}\n\t{\n\t\tmap _currentRender\n\t}\n\t{\n\t\tmap textures/real\n\t}\n}\n"));
		const QStringList images = script.materials.first().imageReferences();
		expect(images == QStringList {QStringLiteral("textures/real")}, "Doom 3 built-in images are not image references");
	}
	{
		const MaterialScript sprite = doom3(QStringLiteral("m\n{\n\tdeform sprite\n\t{\n\t\tmap x\n\t}\n}\n"));
		expect(!sprite.materials.first().isAnimated(), "deform sprite alone does not animate");
		const MaterialScript expand = doom3(QStringLiteral("m\n{\n\tdeform expand time * 0.1\n\t{\n\t\tmap x\n\t}\n}\n"));
		expect(expand.materials.first().isAnimated(), "deform expand over time animates");
		const MaterialScript text = quake3(QStringLiteral("m\n{\n\tdeformVertexes text0\n\t{\n\t\tmap x.tga\n\t}\n}\n"));
		expect(!text.materials.first().isAnimated(), "deformVertexes text does not animate");
	}
	{
		const MaterialScript script = doom3(QStringLiteral("m\n{\n\tdiffusemap x\n\t{\n\t\tblend GL_FOO, GL_ONE\n\t}\n}\n"));
		const QStringList lines = materialDefinitionSummaryLines(script.materials.first());
		bool role = false;
		bool problems = false;
		for (const QString& line : lines) {
			role = role || line.contains(QStringLiteral("(Diffuse)"));
			problems = problems || line.startsWith(QStringLiteral("Problems: "));
		}
		expect(role && problems, "the summary names the stage role and counts problems");
	}
}

// A10: table lookups against the tables the game sees.
void checkTables()
{
	const QString text = QStringLiteral("m\n{\n\t{\n\t\tmap x\n\t\trgb flikertable[ time ] * sinTable[ time ]\n\t}\n}\n");
	{
		MaterialScript script = doom3(text);
		MaterialTableSet tables;
		tables.addStandIns();
		MaterialDefinition& definition = script.materials.first();
		const int missing = checkMaterialTables(&definition, [&](const QString& name) { return tables.find(name); });
		expect(missing == 1 && hasCode(&definition, QStringLiteral("missing-table")) && hasCode(&definition, QStringLiteral("stand-in-table"))
				&& definition.engineRejection.isEmpty(),
			"without the game's tables a missing table warns and a stand-in is noted");
	}
	{
		MaterialScript script = doom3(QStringLiteral("table sinTable { { 0, 1, 0, -1 } }\n") + text);
		MaterialTableSet tables;
		tables.addAll(script.tables);
		tables.addStandIns();
		MaterialDefinition& definition = script.materials.first();
		const auto find = [&](const QString& name) { return tables.find(name); };
		expect(checkMaterialTables(&definition, find) == 1 && !definition.engineRejection.isEmpty() && !hasCode(&definition, QStringLiteral("stand-in-table")),
			"with the game's tables a missing table defaults the material");
		MaterialTable fixed;
		fixed.name = QStringLiteral("flikertable");
		fixed.values = {1.0, 0.5};
		tables.add(fixed);
		expect(checkMaterialTables(&definition, find) == 0 && definition.engineRejection.isEmpty() && !hasCode(&definition, QStringLiteral("missing-table")),
			"running the check again replaces its earlier result");
	}
}

// A13 and A14: keyword help and templates.
void checkHelpAndTemplates()
{
	for (MaterialEngine engine : {MaterialEngine::Quake3, MaterialEngine::Doom3}) {
		for (bool stage : {false, true}) {
			const QStringList keywords = stage ? materialStageKeywords(engine) : materialGlobalKeywords(engine);
			const QStringList plainWords = engine == MaterialEngine::Doom3 && !stage
				? materialGlobalKeywords(engine).mid(materialGlobalKeywords(engine).indexOf(QStringLiteral("solid")))
				: QStringList();
			for (const QString& keyword : keywords) {
				if (plainWords.contains(keyword)) {
					continue;
				}
				expect(!materialKeywordHelp(engine, keyword, stage).isEmpty(), "help for " + keyword.toStdString());
			}
		}
	}
	expect(!materialKeywordHelp(MaterialEngine::Doom3, QStringLiteral("texgen"), true).contains(QStringLiteral("wobbleSky, screen.")),
		"texgen help does not offer screen");
	for (const MaterialTemplate& entry : materialTemplates()) {
		const QString text = instantiateMaterialTemplate(entry, QStringLiteral("textures/test/t"), QStringLiteral("textures/test/t"));
		const MaterialScript script = parseMaterialScript(text, entry.engine);
		const MaterialDefinition* definition = script.find(QStringLiteral("textures/test/t"));
		expect(definition && definition->errorCount() == 0 && definition->engineRejection.isEmpty() && !entry.displayName.isEmpty(),
			"template " + entry.id.toStdString() + " parses cleanly");
		if (entry.id == QStringLiteral("q3-liquid")) {
			expect(!text.contains(QStringLiteral("depthFunc equal")), "the liquid template has no lightmap pass that never draws");
		}
	}
}

// A15: a large script, timed for the log.
void checkLargeScript()
{
	QString text;
	constexpr int count = 5000;
	for (int index = 0; index < count; ++index) {
		text += QStringLiteral("textures/gen/s%1\n{\n\tqer_editorimage textures/gen/s%1.tga\n\tsurfaceparm nonsolid\n\t{\n\t\tmap $lightmap\n\t\trgbGen identity\n\t}\n"
							   "\t{\n\t\tmap textures/gen/s%1.tga\n\t\tblendFunc GL_DST_COLOR GL_ZERO\n\t\ttcMod scroll 0.1 0.2\n\t}\n}\n")
					.arg(index);
	}
	QElapsedTimer timer;
	timer.start();
	const MaterialScript script = quake3(text);
	const qint64 elapsed = timer.elapsed();
	const MaterialDefinition& last = script.materials.last();
	expect(script.materials.size() == count && last.span.line == (count - 1) * 14 + 1 && last.engineRejection.isEmpty(),
		"a large script parses every shader with the right lines");
	std::cout << "parsed " << text.size() / 1024 << " KiB, " << count << " shaders in " << elapsed << " ms\n";
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	checkErrorsStayInTheirDefinition();
	checkQuake3Rules();
	checkBrokenFiles();
	checkDoom3Rules();
	checkEdits();
	checkModel();
	checkTables();
	checkHelpAndTemplates();
	checkLargeScript();
	if (failures > 0) {
		std::cerr << failures << " material script rules check(s) failed\n";
		return 1;
	}
	std::cout << "material script rules passed\n";
	return 0;
}
