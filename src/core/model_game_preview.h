#pragma once

// Engine-parity skins for the modeller's Game preview (UVSKIN-08).
//
// The Mesh Editor draws each surface with one image times flat shade. The
// Game preview replaces that image, per surface and per moment, with what the
// engine would put on the model, split into a lit layer (multiplied by the
// viewport's shade) and an unlit glow layer (added after shading):
//
// - Quake MDL skins keep their palette indices, so the classic renderers are
//   emulated from them: the skin group member shown at the time, player
//   top/bottom colour translation (indices 16-31 and 96-111), fullbright
//   indices 224-255 drawn unlit by the software renderer and modern ports
//   (GLQuake lights them like any texel), GLQuake's flood fill of the skin's
//   first-texel background, and point sampling for the software renderer.
// - Quake III shaders are composed in texture space, stage by stage, with
//   the stage blend, alphaFunc cut-outs, rgbGen/alphaGen (identity, const,
//   wave, lightingDiffuse) and tcMod (scroll, scale, rotate, stretch,
//   transform, turb), and animMap frames at the time. Stages lit by the light
//   grid (rgbGen lightingDiffuse, vertex) go to the lit layer; identity and
//   constant stages are fullbright in Quake III and go to the glow layer.
//
// What a texture-space composition cannot show is listed in notes: tcGen
// environment (needs the view), deforms, sky and fog, and Doom 3's bump and
// specular interaction.
//
// Behaviour follows the released GPL sources: Quake's R_TranslatePlayerSkin
// and colormap fullbrights, GLQuake's Mod_FloodFillSkin, and Quake III's
// tr_shade.c / tr_shade_calc.c stage evaluation (see docs/CREDITS.md). No
// code is copied.

#include "core/material_images.h"
#include "core/material_model.h"
#include "core/material_render.h"
#include "core/model_mesh.h"

#include <QImage>
#include <QStringList>

namespace vibestudio {

struct ModelGamePreviewOptions {
	double time = 0.0;
	MaterialQuakeRenderer quakeRenderer = MaterialQuakeRenderer::ModernPort;
	// Quake player colour rows (0-13), as `color top bottom`; -1 leaves the
	// skin untranslated.
	int topColor = -1;
	int bottomColor = -1;
	// GLQuake and the GL ports flood-fill the skin's first-texel colour
	// region when loading it; the software renderer does not.
	bool floodFill = true;
};

struct ModelGameSkin {
	// Lit by the viewport's shade. Straight ARGB32.
	QImage base;
	// Added after shading; null when the surface has no unlit part.
	QImage glow;
	// The engine samples the skin point-wise (Quake's software renderer).
	bool nearest = false;
	// Which skin group member or animMap frame shows, -1 when not animated.
	int frame = -1;
	int stagesDrawn = 0;
	// What the composition approximated or could not show, translated.
	QStringList notes;
};

// GLQuake's Mod_FloodFillSkin on palette indices: the region of the
// top-left texel's colour connected to it takes the colour of its neighbours.
// `black` is the palette index the engine fills with when a texel has no
// other neighbour (the palette's first pure black).
void modelQuakeFloodFill(QByteArray* indices, int width, int height, int black);
// Quake's player translation table: top and bottom rows 0-13, -1 unchanged.
QByteArray modelQuakePlayerTranslation(int topColor, int bottomColor);

// The Quake classic skin of an MDL surface: skin `skin` of the mesh's
// embedded indexed skins, at options.time. False with an error when the mesh
// has no indexed skin.
bool composeModelQuakeSkin(const ModelMesh& mesh, int skin, const ModelGamePreviewOptions& options, ModelGameSkin* out,
	QString* error = nullptr);

// A Quake III shader composed in texture space at options.time. `images`
// holds the stage images (resolveMaterialImages). The output takes the size
// of the first stage image with pixels, else 256 x 256.
bool composeModelQuake3Skin(const MaterialDefinition& definition, const MaterialImageSet& images, const ModelGamePreviewOptions& options,
	ModelGameSkin* out, QString* error = nullptr);

// The Game preview the editor and `model game-skin` use: Quake classic for
// a mesh with indexed MDL skins, else the surface's Quake III shader when one
// is given, else its plain image (`fallback`) unchanged.
bool composeModelGameSkin(const ModelMesh& mesh, int surface, const MaterialDefinition* definition, const MaterialImageSet* images,
	const QImage& fallback, const ModelGamePreviewOptions& options, ModelGameSkin* out, QString* error = nullptr);

} // namespace vibestudio
