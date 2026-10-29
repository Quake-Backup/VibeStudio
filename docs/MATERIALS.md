# Materials, Shaders And Textures

The Materials page and the `material` CLI family manage the surface
appearance of idTech 1 to 4 games in one place: Doom wall textures and flats
with their animations and switches, Quake WAD textures, Quake II WAL textures,
Quake III shaders and Doom 3 materials. Every material can be browsed, drawn
the way its engine draws it (animated, in real time), checked the way its
engine loads it, and edited either as text or as a node graph.

Status: **Partial**. The module has automated tests on generated fixtures
only and has not been proven against real game packages.

## Shape Of The Module

| Layer | Files | Role |
|---|---|---|
| Model | `src/core/material_model.*` | One definition type for all five engines: directives with their exact source spans, stages, expressions, image programs, deforms, sky and fog parameters, classic frames, switches, WAL flags, diagnostics and the reason an engine would drop the material |
| Scripts | `src/core/material_script.*` | Quake III and Doom 3 lexers and parsers, the Doom 3 expression grammar, text edits that keep comments and layout, templates, implicit materials |
| Classic | `src/core/material_classic.*` | Doom composites, flats, vanilla and Boom animation and switch tables, SWANTBLS and ANIMDEFS text; Quake texture names; Quake II WAL headers and `.wal_json` |
| Evaluation | `src/core/material_eval.*` | Quake III wave tables and noise, Doom 3 expressions and tables, classic frame timing, Quake light styles |
| Images | `src/core/material_images.*` | Engine lookup order, decoding, image programs, cube maps, Doom composition, palettes and colormaps, a shared byte-bounded cache and a shared per-package index |
| Video | `src/core/material_video.*` | RoQ videos for `videoMap`, decoded frame by frame on demand |
| Renderer | `src/core/material_render*.cpp`, `src/core/shaders/material_*` | Each engine's drawing rules on six preview shapes (wall, floor, cube, sphere, cylinder, room): per-vertex and per-stage values on the CPU, every pixel in GLSL shaders on the studio's 3D renderer (OpenGL or Vulkan; see [3D rendering](ARCHITECTURE.md#project-core)) |
| Library | `src/core/material_library.*` | Scans a package over its base packages: scripts, implicit shaders, classic textures; the Quake III dialect; which duplicate the game uses |
| Usage | `src/core/material_usage.*` | Which maps in a package use which materials |
| Lookup | `src/core/material_lookup.*` | The material a level face's texture name finds, for the Levels page |
| Graph | `src/core/material_graph.*` | Node graphs built from definitions, and graph edits turned into text edits |
| CLI | `src/cli/materials.*` | `material list`, `inspect`, `validate`, `render`, `graph`, `edit`, `templates`, `new`, `doom-tables`, `wal`, `usage` |
| GUI | `src/app/material_*.cpp` | The Materials workbench on the **Materials** page (the `shell.mode.shaders` mode) |

The GUI and the CLI call the same functions; nothing the page shows is worked
out a second way.

## One Text, Two Editors

The text is the single source of a material. The node graph is built from the
parsed definition, and a graph edit (set a property, add or remove a node,
move a stage or tcMod, wrap or unwrap a Doom 3 expression, expand a Doom 3
shorthand) is translated into text edits by `applyMaterialGraphEdit`. The text
is parsed again and the graph rebuilt from it, so the two cannot disagree.
Node ids are paths (`stage/0/tcmod/1`, `stage/0/color/red/e/a`) and survive
rebuilds, so the selection and the positions the user dragged nodes to stay.

Text edits patch only the directive spans they change: comments, indentation
and the rest of the script are kept byte for byte. On the page a graph edit
reaches the editor as one replacement of the changed range, so Undo and Redo
in the text cover graph edits too. Whole definitions are edited the same way:
renaming (the material node's **Name**, or `rename`), a duplicate placed after
the original with its comments and layout (`duplicate`), and removal; a rename
or copy onto a name the script already defines is refused.

Classic engines have no material scripts, so their editable text is the form
their tools already use:

| Engine | Text | Saved as |
|---|---|---|
| Doom with ANIMDEFS | Hexen/ZDoom `ANIMDEFS` | The `ANIMDEFS` lump or file |
| Doom otherwise | Boom SWANTBLS (`DEFSWANI.DAT` columns: `speed last first`, `episode texture1 texture2`) | Boom `ANIMATED` and `SWITCHES` lumps |
| Quake II | ericw-tools `.wal_json` | The WAL's own header, rewritten without touching its pixels |
| Quake | None: the texture name sets animation, liquids and skies | Read only |

## Engine Rules

The rules below were checked against the GPL source releases listed in
[Credits](CREDITS.md#materials-shaders-and-textures-2026-10-08).

- **Quake III** follows ioquake3 unless a dialect says otherwise (below). An
  unknown keyword, noise outside `rgbGen`,
  more than four tcMods or a missing image drops the whole shader, and the
  engine draws its default shader instead; the preview does the same and says
  why. Default `rgbGen` is `identityLighting` for opaque and `GL_ONE`/
  `GL_SRC_ALPHA` source blends, depth writes follow the blend unless
  `depthWrite` is given, and sort defaults follow `polygonOffset`, fog, sky
  and blending. Waves use the engine's 1024-entry tables, noise its seeded
  table, and every tcMod, deform, tcGen and colour generator its formula.
  Overbright lighting halves stages and doubles the display. Within one file
  the first definition wins; across files the alphabetically last script
  wins (ioquake3 reads scripts in reverse order).
- **Doom 3** parses expressions right-associatively at every level, as
  `idMaterial` does, with tables appending their first value and the `%`
  operator truncating. An unknown keyword defaults the material. Interactions
  pair bump, diffuse and specular stages as the engine flushes them, light
  colour is twice the light stage, and shading is either the ARB2 interaction
  with the specular table or the BFG `pow(N.H, 10)` form. The first decl of a
  name wins.
- **Doom** composes wall textures from patches, keeps flats as indices, and
  shades through `COLORMAP` by sector light, distance and fake contrast.
  Vanilla wraps rows at 128 (tutti-frutti on short textures); Boom wrapping
  is an option. Animations come from ANIMDEFS, then Boom `ANIMATED`, then the
  vanilla table; switches from Boom `SWITCHES`, then the vanilla list.
- **Quake** gathers `+0`..`+9` and `+a`..`+j` frames at 0.2 s each, warps
  liquids, draws two-layer skies and fullbright colours, and lights through
  styles at 10 Hz, with GLQuake, modern-port and software renderers.
- **Quake II** follows the WAL next-frame chain at 2 Hz, flowing and warping
  surfaces, `trans33`/`trans66`, the `intensity` setting and env sky boxes.

## Quake III Dialects

Quake III's derivatives read the same script format but each loads its own
keywords, and any keyword a game does not know drops the shader. The library
reads every Quake III script of a package as one game, the **dialect**:

| Dialect | Adds | Which script wins a name |
|---|---|---|
| Quake III Arena (ioquake3) | Nothing | The alphabetically last |
| Quake III Arena 1.32 | Nothing | The alphabetically first (its shader text hash) |
| Return to Castle Wolfenstein (iortcw) | Fog, sun, light grid and compression keywords; `nomipmap`; stage `map16`, `map32`, `mapcomp`, `mapnocomp`, `animmapcomp`, `animmapnocomp`, `fog` | The alphabetically last |
| Wolfenstein: Enemy Territory | Wolfenstein's, plus `distanceCull`, `implicitMap`, `implicitMask`, `implicitBlend`, the `lightmap` stage and `tcMod swap` | The alphabetically last |
| Jedi Knight II and Jedi Academy (OpenJK) | `material`, `sun`, `surfacelight`, `lightColor`, `deform`, `noglfog`, `noTC`, `hitLocation`, `hitMaterial`; stage `clampanimMap`, `oneshotanimMap`, `glow`, `surfaceSprites` and its `ss` options | The alphabetically last |

**Automatic** (the default) picks the dialect from the scripts themselves:
Enemy Territory's own keywords first, then whichever of Jedi Knight's and
Wolfenstein's appear more often, else Quake III Arena. A keyword from another
game is reported with the game it belongs to. The preview draws what a 32-bit
display without texture compression would (`map32`, `mapnocomp`), plays
`oneshotanimMap` once and holds the last frame, applies `tcMod swap`, and
names what it leaves out: surface-sprite stages and the glow (bloom) pass.
Keywords that act on the map rather than the surface (fog, sun, light grid,
distance culling, hit materials) are read and not drawn.

## Base Packages

A mod or PWAD borrows from the game it runs on. The library can read a
package over **base packages**, lowest first (`MaterialLayeredReader`): every
entry of every layer is listed, the package's last, and a path a higher layer
also has is hidden, as the game's search path hides it. So images and Doom 3
tables come from the base packages, a PWAD's textures, flats, patches and
lumps merge with its IWAD's (Doom finds the last lump of a name), and base
materials join the library marked **base**. A package's definition of a base
name follows the dialect's script order. On the page the base packages are
the selected game installation's, when it is of the package's engine family;
base materials show read only, and **Override in Package** copies one into
`scripts/zz_vibestudio_overrides.shader` (`00_` where the first definition wins:
Quake III 1.32 and Doom 3) or, for a Quake II WAL, copies the file. The CLI
takes `--base`, and lists or checks base materials only with
`--include-base`.

## Videos

`videoMap` stages play RoQ files (`video/` is added to a bare name and `.roq`
to one without an extension). `MaterialVideo` reads the chunk index when the
image set is resolved and decodes frames when they are drawn: codebooks of
2x2 and 4x4 YCbCr cells, 16 x 16 macroblocks of four 8 x 8 blocks coded as
kept, moved (with the frame's mean motion), one 4x4 cell, or split into
quarters, and Doom 3's JPEG frames. Snapshots every few frames bound the cost
of going back. Quake III loops shader videos; Doom 3 holds the last frame
unless the stage says `loop`. A file that is not RoQ (a Quake II `.cin`)
keeps the moving stand-in, with the reason in **Images**.

## Doom 3 Programs

Doom 3 stages may run ARB vertex and fragment programs, which are game data.
The stock heat haze programs (`heatHaze`, `heatHazeWithMask`,
`heatHazeWithMaskAndVertex`) are approximated from their documented inputs:
the frame drawn so far is read at each pixel moved by `fragmentMap 1`'s
normal map, scrolled by `vertexParm 0` and scaled by `vertexParm 1`, by the
`fragmentMap 2` mask and by the vertex colour. The stock `bumpyEnvironment`
program (glass and polished metal) is approximated as the `fragmentMap 0` cube
map seen in the view direction reflected about the surface normal, bent by
the `fragmentMap 1` normal map. `vertexParm` and `fragmentMap` values are kept
on the stage (with whether a map is a cube map), and fragment-map images and
cube maps join the material's images. Other programs draw the stage's image
plainly, with a note.

## Maps That Use A Material

`scanMaterialUsage` reads every map in a package for the materials it names:
`.map` sources of every idTech editor (faces after their plane and texture
matrix groups, `patchDef2/3` names), Quake, Half-Life and BSP2 BSPs (faces to
texinfo to miptex), Quake II BSPs (texinfo), Quake III, Wolfenstein and Jedi
Knight BSPs (surfaces to shaders), Doom WAD maps (SIDEDEFS and SECTORS), UDMF
`TEXTMAP` and maps in nested WADs. Names match with or without Quake III's
`textures/` folder. The page's **Maps** tab and `material usage` show the
result; Enter on a `.map` or Doom map opens it in Levels with the material's
faces selected (a map inside an archive is copied out first), and compiled
maps are shown on the Packages page. `MaterialLookup` gives the Levels page the
other direction: the definition a face's name finds (an implicit shader when
none does), the image editors show for it, and a face-on frame for animated
textures.

## The Workbench

The page lists every material of the open package (or loose script) as
swatches, filtered by the studio query language over the same properties the
CLI's `--where` reads. Choosing one shows:

- a live preview, playing by itself when the material animates (unless
  reduced motion is on), with play, restart, speed, a time scrubber, the
  preview shape, and the engine's own view options (overbright, lightmap
  brightness and surface type for Quake III; sector light, distance, fake
  contrast, Boom wrapping and switch state for Doom; renderer, light style and
  alternate frames for Quake; intensity for Quake II; interaction shading,
  light orbit, specular, ambient light, light colour and the developer
  `_default` for Doom 3). Dragging orbits, Shift+drag moves the light, the
  wheel zooms; the arrow keys, plus, minus, Home, Space, comma and full stop
  do the same from the keyboard. Frames resize to keep playback near 30
  frames a second;
- the **Nodes** tab: the graph and the selected node's properties, with Add
  Node by category, Remove (Delete), Move Earlier and Later (Alt+Up and
  Alt+Down), operator wrapping for Doom 3 expressions, and Enter to edit;
- the **Text** tab: the script with highlighting and the parser's problems
  underlined;
- **Images**: every image the material reads, where the engine found it, and
  what is missing;
- **Problems**: what the engine would warn about or reject, with the line;
- **Details**: the summary and the other materials sharing its images.

Saving writes a loose script in place, or stages the change into the open
package through the package staging model, so it joins the package's review,
undo and save; **Save All** does so for every text with unsaved changes.
**New Material** starts from a template (`material templates` lists them) in
the current script, or in a new `scripts/` or `materials/` script.

The page also has:

- **Library Options**: sort (as the game reads them, by name, engine,
  script, problems first, most used in maps), swatch size, base game
  materials, shadowed definitions, and the Quake III game (also under
  **Engine View** > **Game**);
- a context menu on materials and the **More** menu: Save All, Save Script
  As, Rename, Duplicate, Delete, Override in Package, Copy Name, Copy Preview
  Image, Export Preview Image, Export Animation Sheet, Open Script in Code
  (scripts that are files), Show in Packages, Find Uses in Maps and Preview
  Settings;
- **Preview Settings**: tiling, field of view, orthographic, background,
  checkerboard, Quake III entity colour and light grid, Doom extra light,
  Quake lightmap level, the sky for Quake II and Doom sky surfaces, and Doom 3
  light radius, angle and orbit time, sound level, shader parms 0-11 and
  globals 0-7;
- **Problems** for the material or the **Whole Package**; a **Maps** tab;
  **Images** with Show in Textures, Edit Image in Texture Editor (when the
  studio offers it), Show in Packages and Copy Path;
- find and replace in the text (Ctrl+F, Ctrl+H) and completion (Ctrl+Space)
  of the engine's and game's keywords and the package's images.

The view (list or grid, swatch size, order, base materials, dialect, tab,
splitter sizes and preview choices) is kept between sessions.

Scans, image decoding and frames run off the GUI thread; a newer request
replaces an older one, and nothing scans until the page is shown.

## Accessibility And Localisation

The graph is exposed to screen readers as a list of nodes, each with its
title, what feeds it and what it feeds; selection changes are announced.
Every control has a name and a description, ports differ by shape as well as
colour, swatch badges use shapes (a triangle for animated materials, an
exclamation for dropped ones), and node text scales with the text size
preference through the graph's zoom. Right to left, the graph's flow and the
arrow keys mirror. All text is translatable.

## Limits And Integration Gaps

- Renders are approximations on preview shapes: no real lightmaps or
  bump-mapped geometry from a map, no Doom 3 GLSL or ARB fragment programs
  beyond the interaction, and no RoQ/CIN video (a moving placeholder stands
  in).
- Previews, swatches and `material render` need a working 3D renderer. Where
  neither OpenGL nor Vulkan starts, the preview says why (and points to
  **Settings** > **Appearance and Language** > **3D Rendering**), swatches stay
  blank and `material render` exits 5. The shaders match the former CPU
  renderer to within two levels apart from isolated pixels at texel and
  triangle edges, and OpenGL and Vulkan match each other.
- Quake III dialects: keywords that act on the map, not the surface (fog,
  sun, light grid, distance culling, surface materials), surface sprites and
  the glow pass are read and named, not drawn.
- Doom 3 ARB programs other than the stock heat haze family and
  `bumpyEnvironment` draw the stage's image plainly; those two are
  approximations of the programs' effect.
- Quake II `.cin` cinematics are not decoded; they are not shader videos.
- Base packages on the page are the selected game installation's; a package
  of a different engine family gets none.
- The Levels 3D view draws each face's display image; it can ask
  `MaterialLookup` for animated frames, which the Levels page does not do yet.
