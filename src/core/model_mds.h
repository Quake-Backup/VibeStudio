#pragma once

// Return to Castle Wolfenstein MDS writer (FMT-09): a skeletal model written
// as MDSW version 4 from the studio's generic skeleton.
//
// MDS bones carry absolute rotations every frame, and each child sits at a
// fixed distance from its parent along its own offset angles; only the root
// moves freely (the frame's parentOffset). So a skeleton is written when:
// - every joint's rotation is a pure rotation (no scale or shear);
// - every child's distance from its parent stays the same in every frame,
//   within `distanceTolerance`;
// - every root sits at the same place in each frame;
// - it has at most 128 bones and every vertex at most `maxInfluences` weights.
// Angles are stored as shorts (1/65536 of a turn), so poses round to that.
// Frames are the skeleton's clips one after another (the game's animation
// scripts name the ranges), or the bind pose alone when it has no clips.
// The full level of detail is written with an identity collapse map.
//
// Layouts follow mdsHeader_t, mdsFrame_t, mdsBoneFrameCompressed_t,
// mdsBoneInfo_t, mdsSurface_t, mdsVertex_t, mdsWeight_t and mdsTag_t in the
// GPL Return to Castle Wolfenstein source (src/qcommon/qfiles.h), read
// together with VibeStudio's own decoder (model_format_mds.cpp), whose file
// comment describes the bone maths. Original VibeStudio code.

#include "core/model_document.h"

namespace vibestudio {

struct ModelMdsExportOptions {
	// The bone torso animation pivots about, or -1 for none.
	int torsoParent = -1;
	float lodScale = 1.0f;
	float lodBias = 0.0f;
	// How far a child's distance from its parent may vary between frames.
	double distanceTolerance = 0.01;
	int maxInfluences = 8;
};

QByteArray exportModelMds(const ModelMesh& mesh, const ModelMdsExportOptions& options, QString* error = nullptr,
	const ModelWorkControl& control = {}, ModelExportReport* report = nullptr);

} // namespace vibestudio
