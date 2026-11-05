#pragma once
#include <QJsonObject>
#include <QStringList>
namespace vibestudio::cli {
struct LevelMoveRepeatCliResult {
	int exitCode = 0;
	QString error;
	QJsonObject payload;
	QStringList lines;
};
// map move --camera: the Levels page's Move to Camera (core/level_paste.h).
LevelMoveRepeatCliResult runLevelMoveToCamera(const QStringList& arguments);
// map move with --to, --pivot, --snap, --context or several --object: the
// Levels Translate tool's move of the whole selection
// (core/level_object_manipulator.h translateLevelMapObjects()).
LevelMoveRepeatCliResult runLevelObjectTranslate(const QStringList& arguments);
// map rotate with --around, --angle-step or --context: the Levels Rotate
// tool's turn of the whole selection about any axis
// (core/level_object_manipulator.h rotateLevelMapObjects()).
LevelMoveRepeatCliResult runLevelObjectRotate(const QStringList& arguments);
// map resize with --drag (and --axis or --view), --equal, --grid,
// --size-grid or --context: the Levels Scale tool's scale of the whole
// selection about its centre or --pivot (core/level_object_manipulator.h
// levelObjectScaleFactors() and scaleLevelMapObjects()).
LevelMoveRepeatCliResult runLevelObjectScale(const QStringList& arguments);
// map shear with --side and --offset (or --pivot or --context): the Levels
// Shear Tool's camera handles and the Transform tool's side lines, sliding one
// side of the whole selection's box (core/level_object_manipulator.h
// shearLevelMapObjects()).
LevelMoveRepeatCliResult runLevelObjectShear(const QStringList& arguments);
// map repeat-transforms: NetRadiant Custom's Repeat Transforms
// (core/level_repeat.h) with the transforms named on the command line.
LevelMoveRepeatCliResult runLevelRepeatTransforms(const QStringList& arguments);
// map drag-planes: NetRadiant Custom's QE4 plane drag, Alt plane and vertex
// drags and Ctrl+Alt extrusion as the Levels Select and Build tools make
// them (core/level_plane_drag.h).
LevelMoveRepeatCliResult runLevelPlaneDrag(const QStringList& arguments);
} // namespace vibestudio::cli
