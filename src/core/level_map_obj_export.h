#pragma once

// Export Map as OBJ and Export Selection as OBJ: brush faces and Quake III
// patches written as a Wavefront OBJ with an MTL material library, for
// Blender and other modellers. Workflow references: TrenchBroom's File >
// Export > Wavefront OBJ (lib/TbUiLib/src/ObjExportDialog.cpp and
// MapWindow::exportDocumentAsObj at 4c756be1, GPL-3.0-or-later: material
// paths relative to the game or the export folder, layers marked Omit From
// Export left out) and NetRadiant Custom's brushexport plugin
// (contrib/brushexport/export.cpp at 449778b3, GPL-2.0-or-later: ignored
// materials, collapse by object, by material or into one mesh, objects
// instead of groups, vertex welding, short material names). Original Qt
// implementation; see docs/CREDITS.md.
//
// The geometry is the Levels camera's: buildLevelMapPreviewMesh() solves the
// brushes, tessellates the patches and works out UVs from the material sizes,
// and the export puts each face's fan back together as one polygon.

#include "core/level_document.h"
#include "core/level_map.h"

#include <QColor>
#include <QHash>
#include <QJsonObject>
#include <QSize>
#include <QStringList>

#include <functional>

namespace vibestudio {

struct LevelPreviewAssets;

// How faces are gathered into OBJ objects or groups: NetRadiant Custom's
// "Don't collapse", "Collapse by material" and "Collapse mesh".
enum class LevelMapObjGrouping {
	// One per brush and patch, named entity<id>_brush<id> or entity<id>_patch<id>.
	Object,
	// One per material.
	Material,
	// Everything in one mesh.
	Single,
};

// What the MTL's map_Kd paths are relative to, as TrenchBroom offers.
enum class LevelMapObjMaterialPaths {
	// The package path, as the game loads it (textures/base_wall/metal.tga).
	Game,
	// The image's place under `materialRoot`, seen from the OBJ's folder.
	Export,
};

// What the open packages say about one material.
struct LevelMapObjMaterialSource {
	// Package path of its image; empty when no package holds one.
	QString imagePath;
	// Original texel size, for classic and Valve 220 UVs.
	QSize size;
	// Average colour of the image, for the MTL's Kd.
	QColor colour;
};

struct LevelMapObjExportOptions {
	// The selected brushes and patches (a selected entity brings its own)
	// instead of the whole map.
	bool selectionOnly = false;
	LevelMapObjGrouping grouping = LevelMapObjGrouping::Object;
	// `o` records, which Blender imports as separate objects; `g` otherwise.
	bool objects = true;
	// Share corners at the same place within each object or group.
	bool weld = true;
	// Write each face as triangles instead of one polygon.
	bool triangulate = false;
	// Write the .mtl beside the OBJ, with mtllib and usemtl records.
	bool materialLibrary = true;
	// Keep the last 20 characters of each material name, as NetRadiant
	// Custom's short names do for old importers.
	bool shortMaterialNames = false;
	// Write the map's own axes (Z up). Off writes Y up, as TrenchBroom and
	// NetRadiant Custom do and modellers expect.
	bool keepZUp = false;
	// Faces whose material is named here are left out: a bare name matches
	// the last part of the material path (caulk matches common/caulk), a path
	// matches its end. Any case.
	QStringList ignoredMaterials;
	LevelMapObjMaterialPaths materialPaths = LevelMapObjMaterialPaths::Game;
	// The folder package paths start from, for LevelMapObjMaterialPaths::Export.
	QString materialRoot;
	// Keyed by the case-folded material name with forward slashes.
	QHash<QString, LevelMapObjMaterialSource> materials;
	// Whole-map exports leave out layers marked Omit From Export, unless
	// asked, and the layers named here (by name or id).
	bool includeOmittedLayers = false;
	QStringList omitLayers;
	// Past this many triangles the export is refused.
	int triangleLimit = 4000000;
};

// The tool materials an export leaves out unless told otherwise: caulk,
// clip, trigger, hint, skip, origin, nodraw and the portal and bot materials.
QStringList levelMapObjDefaultIgnoredMaterials();
LevelMapObjExportOptions levelMapObjDefaultOptions();
// Whether `material` matches an entry of `ignored` (see ignoredMaterials).
bool levelMapObjMaterialIgnored(const QString& material, const QStringList& ignored);
QString levelMapObjGroupingId(LevelMapObjGrouping grouping);
bool levelMapObjGroupingFromId(const QString& id, LevelMapObjGrouping* grouping);
// The image paths, sizes and average colours of resolved preview assets.
QHash<QString, LevelMapObjMaterialSource> levelMapObjMaterials(const LevelPreviewAssets& assets);
// The .mtl written beside `objPath`, with any spaces in its name turned into
// underscores, since an OBJ's mtllib record separates names with spaces.
QString levelMapObjMaterialLibraryPath(const QString& objPath);

struct LevelMapObjExportReport {
	QString objPath;
	QString mtlPath;
	bool dryRun = false;
	bool written = false;
	bool cancelled = false;
	QString objBackup;
	QString mtlBackup;
	qint64 objBytes = 0;
	qint64 mtlBytes = 0;
	int brushes = 0;
	int patches = 0;
	int polygons = 0;
	int triangles = 0;
	int vertices = 0;
	int materials = 0;
	// Faces left out because their material is ignored.
	int ignoredFaces = 0;
	// Brushes and patches left out because they are hidden.
	int hiddenObjects = 0;
	QStringList omittedLayers;
	int omittedObjects = 0;
	// Materials with no known image size: their classic and Valve 220 UVs
	// assume 64 x 64 texels.
	QStringList unsizedMaterials;
	// Materials whose MTL entry has no image path.
	QStringList unresolvedMaterials;
	QStringList warnings;
	QStringList errors;
	[[nodiscard]] bool succeeded() const { return errors.isEmpty(); }
};

// The brushes and patches an export writes, as a document of their own (the
// worldspawn entity and those records only): hidden objects are left out,
// and so are the selection's complement or, for the whole map, the omitted
// layers. Fails for Doom maps, an empty selection, unknown layer names and a
// result with nothing to write.
bool prepareLevelMapObjExport(const LevelMapDocument& document, const LevelMapObjExportOptions& options, LevelMapDocument* exported,
	LevelMapObjExportReport* report = nullptr, QString* error = nullptr);

struct LevelMapObjFiles {
	QByteArray obj;
	// Empty without a material library.
	QByteArray mtl;
};
// Writes `exported` (from prepareLevelMapObjExport) as OBJ and MTL text for an
// OBJ at `objPath`, whose name the mtllib record and export-relative material
// paths use. `isCancelled` is polled throughout.
bool buildLevelMapObj(const LevelMapDocument& exported, const QString& objPath, const LevelMapObjExportOptions& options, LevelMapObjFiles* files,
	LevelMapObjExportReport* report = nullptr, QString* error = nullptr, const std::function<bool()>& isCancelled = {});

struct LevelMapObjExportRequest {
	// The OBJ path (.obj), dry run, overwrite, backups and cancellation.
	LevelDocumentSaveRequest save;
	LevelMapObjExportOptions options;
};
// Prepares, builds and writes the OBJ and its MTL through writeLevelMapBytes(),
// checking both destinations before either is written, with backups of
// replaced files. The document is never changed.
LevelMapObjExportReport exportLevelMapObj(const LevelMapDocument& document, const LevelMapObjExportRequest& request);
QJsonObject levelMapObjExportReportJson(const LevelMapObjExportReport& report);

} // namespace vibestudio
