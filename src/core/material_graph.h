#pragma once

// Node graphs for materials, and the edits a graph makes.
//
// A graph is built from a parsed definition: image and program nodes feed
// coordinate and colour nodes, which feed stage nodes, which feed the
// material's output node in draw order. Doom 3 expressions become trees of
// operator and term nodes. Classic textures show their frames, composition,
// switches and flags.
//
// The graph is a view of the source text, never a second copy of it. Every
// graph edit is translated into text edits (core/material_script.h for
// Quake III and Doom 3, SWANTBLS, ANIMDEFS or .wal_json for the classic
// engines), the text is parsed again, and the graph is rebuilt from it.
// Node ids are stable paths ("stage/1/tcmod/0", "stage/2/color/red/a") so a
// view can keep positions across rebuilds.

#include "core/material_model.h"

#include <QJsonObject>
#include <QPointF>
#include <QSizeF>
#include <QString>
#include <QStringList>
#include <QVector>

namespace slopstudio {

// Node sizes in scene units (pixels at zoom 1 with text at 100%). The graph's
// automatic layout and the view that draws it use the same sizes, so laid-out
// nodes never overlap on screen.
inline constexpr double kMaterialGraphNodeWidth = 196.0;
inline constexpr double kMaterialGraphHeader = 26.0;
inline constexpr double kMaterialGraphSubtitle = 18.0;
inline constexpr double kMaterialGraphPortRow = 18.0;
inline constexpr double kMaterialGraphThumbnail = 52.0;
inline constexpr double kMaterialGraphPadding = 6.0;

// The text a material's edits change.
enum class MaterialTextKind {
	None,
	Quake3Shader,
	Doom3Material,
	DoomSwantbls,
	DoomAnimdefs,
	Quake2WalJson,
};

QString materialTextKindId(MaterialTextKind kind);
bool materialTextKindFromId(const QString& id, MaterialTextKind* kind);
// The natural text form for a definition's engine.
MaterialTextKind defaultMaterialTextKind(const MaterialDefinition& definition);

enum class MaterialGraphPortType {
	Image,
	Coordinates,
	Color,
	Alpha,
	Scalar,
	Stage,
	Geometry,
	Frame,
};

QString materialGraphPortTypeId(MaterialGraphPortType type);

struct MaterialGraphPort {
	QString id;
	QString label;
	MaterialGraphPortType type = MaterialGraphPortType::Scalar;
};

enum class MaterialGraphPropertyType {
	Text,
	Number,
	Integer,
	Bool,
	Enum,
	Expression,
	Image,
	ReadOnly,
};

QString materialGraphPropertyTypeId(MaterialGraphPropertyType type);

struct MaterialGraphProperty {
	QString id;
	QString label;
	MaterialGraphPropertyType type = MaterialGraphPropertyType::Text;
	QString value;
	// Enum: the ids written to the text, and, in the same order, what a
	// person reads for each (translated). Empty labels mean the ids are
	// keywords and are shown as they are.
	QStringList options;
	QStringList optionLabels;
	QString help;
};

struct MaterialGraphNode {
	QString id;
	// "output", "stage", "image", "animmap", "lightmap", "video", "cube",
	// "program", "tcgen", "tcmod", "transform", "rgbgen", "alphagen",
	// "color", "condition", "alphatest", "texgen", "expression", "deform",
	// "sky", "fog", "light", "frame", "animation", "patch", "switch",
	// "flags", "warp", "info".
	QString kind;
	QString title;
	QString subtitle;
	QVector<MaterialGraphPort> inputs;
	QVector<MaterialGraphPort> outputs;
	QVector<MaterialGraphProperty> properties;
	int stage = -1;
	int directive = -1;
	int line = 0;
	// An image the view may draw in the node.
	QString imageReference;
	QPointF position;
	bool removable = false;
	// The node can move up or down in draw order (stages, tcMods).
	bool reorderable = false;
	// Shown dimmed: present in the text but not drawn (a failed condition,
	// a stage without an image, an ignored keyword).
	bool inactive = false;
	// Part of a Doom 3 image program: it can be wrapped in an image function
	// (heightmap, addnormals...), and a function node can be unwrapped.
	bool programNode = false;

	[[nodiscard]] const MaterialGraphProperty* property(const QString& id) const;
};

// A node's size; `thumbnail` reserves room for its image.
QSizeF materialGraphNodeSize(const MaterialGraphNode& node, bool thumbnail);
// The node's title with what it belongs to, for lists and screen readers:
// "Stage 2: Coordinates", "Output: textures/base/wall". Translated.
QString materialGraphNodeLabel(const MaterialGraphNode& node);

struct MaterialGraphLink {
	QString fromNode;
	QString fromPort;
	QString toNode;
	QString toPort;
};

struct MaterialGraph {
	MaterialEngine engine = MaterialEngine::Unknown;
	MaterialTextKind textKind = MaterialTextKind::None;
	QString material;
	QVector<MaterialGraphNode> nodes;
	QVector<MaterialGraphLink> links;

	[[nodiscard]] int indexOf(const QString& nodeId) const;
	[[nodiscard]] const MaterialGraphNode* node(const QString& nodeId) const;
};

MaterialGraph buildMaterialGraph(const MaterialDefinition& definition, MaterialTextKind textKind = MaterialTextKind::None);

// A node the palette can add, and where it goes.
struct MaterialGraphNodeTemplate {
	QString id;
	QString category;
	QString title;
	QString description;
	// Needs a selected stage (tcMod, rgbGen...) rather than the material.
	bool needsStage = false;
};

QVector<MaterialGraphNodeTemplate> materialGraphNodeTemplates(MaterialEngine engine, MaterialTextKind textKind = MaterialTextKind::None);

enum class MaterialGraphEditKind {
	SetProperty,
	AddNode,
	RemoveNode,
	// Move a stage or tcMod one step earlier (-1) or later (+1).
	MoveNode,
	// Doom 3 expressions: put an operator above a node (value: the
	// operator id, e.g. "multiply") with the node as its first operand.
	WrapExpression,
	// Doom 3 expressions: replace a node by its first operand.
	UnwrapExpression,
	// Doom 3: turn a diffusemap/bumpmap/specularmap shorthand into a block.
	ExpandShorthand,
	// Rename the material (value: the new name). Setting the material node's
	// "name" property does the same.
	RenameMaterial,
};

QString materialGraphEditKindId(MaterialGraphEditKind kind);
bool materialGraphEditKindFromId(const QString& id, MaterialGraphEditKind* kind);

struct MaterialGraphEdit {
	MaterialGraphEditKind kind = MaterialGraphEditKind::SetProperty;
	QString node;
	QString property;
	QString value;
	// AddNode: the template id; the stage it goes into (node "stage/N").
	QString nodeTemplate;
	int step = 0;
	// Classic edits: the material's frame names, so an animated range is
	// found from any of its members. Adding a Doom range also reads it for
	// the range's last picture: the names after the material in its
	// namespace, in namespace order.
	QStringList related;
};

// Node ids are positions, so moving or removing a stage or tcMod, or wrapping
// a node, gives other nodes new ids. A move names an id prefix before the
// edit and after it; `to` is empty when the node is gone.
struct MaterialGraphIdMove {
	QString from;
	QString to;
};

// The id moves an edit makes, worked out from the graph before it.
QVector<MaterialGraphIdMove> materialGraphIdMoves(const MaterialGraph& graph, const MaterialGraphEdit& edit);
// `id` after the moves; empty when the node is gone. Ids no move names stay.
QString remapMaterialGraphId(const QString& id, const QVector<MaterialGraphIdMove>& moves);

struct MaterialGraphEditResult {
	bool ok = false;
	QString error;
	QString text;
	// The node to select after the edit: the new node, the one that moved,
	// or after a removal the node that fed the removed one.
	QString focusNode;
	// The material's new name after a rename, else empty.
	QString renamedTo;
	// Nodes whose ids the edit changed (materialGraphIdMoves).
	QVector<MaterialGraphIdMove> idMoves;
};

// Applies one graph edit to the material's source text and returns the new
// text. The text is checked by parsing it again. Only the material's own
// definition is re-read while its edits are applied, so a graph edit costs
// the same in a large script as in a small one.
MaterialGraphEditResult applyMaterialGraphEdit(const QString& text, MaterialTextKind kind, const QString& material,
	const MaterialGraphEdit& edit, const QString& sourcePath = QString(), MaterialDialect dialect = MaterialDialect::Automatic);

QJsonObject materialGraphJson(const MaterialGraph& graph);
// Reads graph edits for the CLI: [{"edit": "set", "node": "stage/1",
// "property": "blend", "value": "add"}, ...].
bool parseMaterialGraphEditsJson(const QByteArray& json, QVector<MaterialGraphEdit>* edits, QString* error);

} // namespace slopstudio
