# Model Collision Authoring

The Mesh Editor's **Collision** tab authors static and animated oriented boxes and
[convex hulls](#convex-hulls) separately from render geometry. Use several boxes to approximate a prop, inspect them in
the same camera view, then export a collision `.map` or place the brushes in the
open level. Collision is retained by mesh source saves, selection-aware undo and
local recovery. Core authoring works offline without an AI provider.

## Authoring

**Add Box** uses Name, Centre, Size and Rotation. Rotations use degrees around
model X, then Y, then Z, matching the geometry transform convention. **Apply Box**
updates the selected box, including its name. **Duplicate Box** retains its
geometry; **Delete Box** removes it. The GUI chooses a free numeric suffix when
adding/duplicating with an existing name. The CLI requires a unique name.

**Fit New Box** encloses selected vertices, edges or faces of the active surface.
With no selected mesh components it encloses every surface. Choose All frames
or Current frame; either produces one static axis-aligned box. Dimensions thinner
than one unit expand symmetrically to one unit. Fitting does not recompute when
mesh positions, frames, materials or topology change.

**Animate Box** copies a static box into every stored mesh frame. **Fit New
Animated Box** fits an independent axis-aligned box in each frame, using the same
component selection as static fitting. The track has one centre, size and
rotation per frame; later mesh edits do not refit it. **Make Static from Current
Frame** keeps that stored pose and removes the track. One undo restores it.

For animated boxes, **Edit poses** chooses Current frame or All frames and stays
synchronized with Geometry's transform scope. Apply Box sets the displayed
absolute values on that scope. Animate and animated fit select Current frame
initially. Names identify the whole track; duplicating a box copies every pose.
Frame duplication/deletion remaps collision poses; Copy Pose includes them;
Insert In-betweens generates them alongside mesh and tag poses. Centres and sizes
interpolate linearly; rotation uses the shortest quaternion arc shared with
attachment tags. Endpoints remain exact. Generated poses must satisfy the same
corner bounds as authored ones; failure rolls back the whole edit.

Playback uses the mesh's clip, native MDL timing and interpolation mode. Overlays
share the completed raster's pose and camera. Inspector/table values identify
stored poses; transient blends never enter the document. Pause, stepping and
authoring return to an exact stored frame. Reduced motion suppresses playback.
Frame Model includes the currently displayed boxes.

Choose **Collision** in the component selector, then select a box edge or a
keyboard-accessible table row. The inspector's box list selects the same volume.
Selection is exclusive; the profile's toggle modifier deselects the current box.
Selected boxes have solid,
thicker edges; other boxes have dashed edges. Both draw through the model, so
the overlay is an inspection view rather than a depth test. **Show collision
boxes** toggles it outside Collision selection mode; that mode keeps boxes
visible. Frame Model includes visible boxes. Clipping and resizing
use the same completed camera snapshot as the model. The viewport's accessible
description identifies the selected box.

The existing transform tools and Geometry numeric fields act on the selected
box. Move and rotation follow Geometry's **Transform axes**: World, Selection
(the selected box's rotation), or Custom XYZ orientation. **Scale uses the box's local axes**,
labelled Local X/Y/Z, so nonuniform scaling retains a rectangular box. The centre
handle scales uniformly. Rotation composes with the existing orientation, in
X/Y/Z order in the chosen basis, and may normalize the displayed Euler angles. All operations use
the chosen selection-centre, origin or custom pivot. Local scaling about an
external pivot also scales the centre's offset in the box's basis.

Translation, angle and scale snapping share Geometry's settings. Numeric scale
also accepts negative factors to mirror around the pivot; drag factors remain
positive. The viewport previews a candidate and commits one undo step on release.
Escape, changing selection/tool, or releasing an invalid transform cancels it.
Transforms that exceed the bounds or shrink a dimension below one unit are
rejected. Static boxes ignore the GUI frame scope; the CLI refuses a single-frame
edit until a box is animated. All-frame transforms resolve one fixed pivot and
move/rotate basis from the displayed reference pose, while scaling retains each
pose's own local axes.

At most 64 boxes and 1,024 poses per animated box are allowed, matching the mesh
frame limit. Names are unique, 1–128 characters, without
surrounding whitespace or control characters. Centres and every rotated corner
must fit within ±32768 model units; each size is at least one unit. Rotation
components must be finite and within ±36000 degrees. Invalid edits and
cancellation leave the complete document and selection unchanged.

Collision poses are independent of attachment tags and render-mesh transforms.
They are source authoring data; no target-game dynamic hitbox or physics format
is emitted. Rigid bodies, skeletal collision and automatic convex decomposition
remain outside this workflow. Selecting a box prevents accidental mesh
or UV transforms until mesh components are selected again.

### Convex hulls

**Fit New Hull** fits a convex hull to the selected vertices, edges or faces of
the active surface, or to every surface when none are selected, over the poses
**Fit poses** chooses (all frames together, or the current frame). Round,
sloped and tapered props fit far better than with boxes. **Hull budget** sets
how many faces, edges and vertices the hull may have:

| Budget | Limit | Source |
| --- | --- | --- |
| Map brush | 128 faces | The level editor's brush limit; Quake, Quake II and Quake III clip brushes. |
| Doom 3 trace model | 16 faces, 32 edges, 32 vertices | `MAX_TRACEMODEL_POLYS`, `MAX_TRACEMODEL_EDGES` and `MAX_TRACEMODEL_VERTS` in Doom 3's [`TraceModel.h`](https://github.com/id-Software/DOOM-3/blob/master/neo/idlib/geometry/TraceModel.h). |

**Maximum hull faces** can lower the face limit further, down to 4. When the
exact hull fits the budget it is used as it is. Otherwise it is simplified
conservatively: starting from the bounding box, the exact hull's own faces are
added one at a time, each time the one that removes the most volume while the
result stays within budget. Every plane touches the source points, so the hull
only grows and every vertex stays inside it. The fit reports the volume of the
exact hull beside the fitted one (CLI `hull.volume`). Coplanar, collinear or
fewer than four points are refused.

A hull is a collision volume like a box: it has a name, a frame (centre, size
and rotation) and shares the 64-volume limit and the name rules. Its points
are stored inside that frame, so **Apply Box**, the transform tools, **Animate
Box**, **Make Static from Current Frame**, duplicate and delete all work on it;
**Scale** stretches it along its local axes. The shape stays the same in every
pose; an animated hull moves, turns and scales with its poses.

**Fit New Animated Hull** fits a hull separately in every stored frame instead,
each within the budget, so a prop that bends or stretches keeps a tight hull in
every frame. Each pose stores its own shape beside its centre, size and
rotation. Shapes cannot be blended, so between stored frames the nearer frame's
shape holds while the frame itself is interpolated; in-between frames take the
nearer stored frame's shape too. The pose transform tools keep a pose's shape.
Doom 3 exports write one surface per volume and a surface keeps one topology,
so they use the exported frame's shape. **Animate Box** on a static hull still
copies one shape into every pose. The inspector
lists its vertex, edge and face counts and whether it fits the Doom 3 trace
model budget. The overlay draws its edges with the box styles. Map export and
placement turn each hull into one convex brush through the level editor's
convex-envelope service. Hulls of at most 256 points and 128 faces are accepted.

## Target And Level Handoff

Map origin translates every box by the same offset. One closed convex brush is
generated per box through the level editor's existing hull service. Every placed
corner must remain within ±32768 world units. Choose the target explicitly;
it is not inferred from an MDL/MD2/MD3 filename.

**Place for Selected Model Instance** places the boxes where a placed prop
actually is. Select the model entity (one with a `model` key, such as
`misc_model`) in the open level, then choose it. Every box corner goes through
the instance's whole transform, the same one level preview draws the prop
with: its `origin`, then `angles` (pitch, yaw, roll: yaw about Z, then pitch
about Y, then roll about X) or `angle` (yaw), then `modelscale_vec` or
`modelscale`. Rotation and non-uniform scale are exact, because the image of a
box under that transform is still a convex solid with eight corners. **Map
origin** is not used. Mirrored scales are accepted; a zero scale is refused.
Compilers and games may read pitch differently from level preview; check a
pitched instance in the target game.

| Target | Output and requirement |
| --- | --- |
| Quake | `clip` texture on all faces; the compiler generates expanded collision hulls. No draw hull or point-trace collision is added. |
| Quake II | `clip` texture and `CONTENTS_PLAYERCLIP` (`65536`) on every face; blocks player movement, not monsters or projectiles. |
| Quake III | Explicit project shader path, such as `common/clip`, below `textures/` with that prefix omitted. The compiler must find a definition with `surfaceparm playerclip` and `nodraw`. Numeric map flags cannot supply this behavior. |

Quake III shader availability and behavior are not verified by this export.
Keep its shader in the project's compiler assets, inspect level dependencies,
compile with the intended game profile, and test collision in the target engine.
No commercial shader or texture is bundled. The material field accepts up to
54 ASCII path characters and rejects traversal, comments and map syntax.
Quake and Quake II use fixed clip conventions and reject a material override.

**Export Collision Map…** saves a standalone worldspawn brush fragment through
the shared guarded model writer. Merge it into the intended level or use
**Place Collision in Level**. A fragment is not a sealed, playable level.
Placement needs no writable package. It prepares on a worker, honors level
scene locks and insertion destinations, and publishes only if the map, selection
and shared context still match. All boxes form one level undo step. Save the
level before using the existing compiler/package/launch workflow. Doom/Hexen
WAD maps are not a brush-collision destination.

GUI handoff pauses playback and samples the current stored frame. CLI/service
handoff requires an explicit frame when any box is animated; absent or invalid
frames are rejected. Static boxes accompany the sampled tracks unchanged.
Export notes identify the frame and its name. The map receives static brushes,
with no animation or live link to the model.

### Recorded placement and refresh

Placement puts the brushes of one handoff into one `func_group` entity that
records where they came from. Its keys are:

| Key | Value |
| --- | --- |
| `_vs_collision_source` | The mesh source, relative to the map's folder when possible. Omitted for an unsaved model. |
| `_vs_collision_revision` | SHA-256 of the collision volumes as the source stores them. |
| `_vs_collision_frame` | The sampled frame, or `-1` for static boxes. |
| `_vs_collision_target`, `_vs_collision_material` | The target and, for Quake III, the clip shader. |
| `_vs_collision_transform` | The placement transform: 12 numbers, row by row, translation last. |
| `_vs_collision_model` | The instance's `model` key, for instance placement. |

ericw-tools and VibeMap2, Quake II `qbsp3`, and q3map2 and VibeMap3 merge
`func_group` brushes into the world, as they do for TrenchBroom's groups, so
the record changes nothing in the compiled map. A compiler without `func_group`
support, such as the original 1996 Quake `qbsp`, would keep it as a separate
brush entity; compile Quake maps with VibeMap2 or ericw-tools.
Underscore keys are ignored by the games. Only the revision tracks collision:
a render-only mesh edit leaves placed collision current.

**Refresh Placed Collision…** lists the collision placed from this model in the
open level, each **Current** or **Stale** (the boxes changed since placement),
with its frame and brush count; collision placed from an unsaved model shows as
**No recorded source**. Choose one to replace its brushes with the current boxes,
using the recorded target, material, frame and transform. The old `func_group`
and its brushes are removed and the new one is placed in the same scene group
or layer, all as one level undo step. A locked group or layer refuses the
refresh and leaves the map unchanged. Collision placed from another model is
not offered; open that model to refresh it. A record whose keys were edited
into an invalid state is reported and never refreshed.

These brushes are static level geometry. Moving or rescaling the prop in the
level does not move them: refresh them afterwards. On the command line,
`refresh --instance entity:N` follows that instance's current transform. The
level editor can further edit inserted brushes using its normal component,
transform, scene, undo and save services; a later refresh replaces such edits.
**Export Collision Map…** still writes a plain worldspawn fragment, with no
record.

The level shows the state of its records without the model open: each record is
checked against the mesh source it names, read from disk (an unchanged file is
read once). The Levels **Checklist** on the **Map** tab warns when sets are out of
date with their model, or when their model cannot be found or their keys are
damaged, and says so when every set matches. `map inspect` adds
`collisionRecords` (each record's `entity`, `status` of `current`, `stale`,
`source-missing` or `invalid`, `source`, `frame`, `target` and `brushes`) when the
map holds any, and a summary line in text output. Refreshing still happens from
the Mesh Editor or `model collision --operation refresh`. Placing collision
together with **Stage and Place** in one step is not yet available.

### Doom 3 collision

Doom 3 has no clip brushes for props: it builds a model's collision from its
render surfaces. When any surface's material has the `collision` keyword, such
as `textures/common/collision`, only those surfaces collide (Doom 3's
`CollisionModel_load.cpp`, `LoadRenderModel`). **Export ASE with
Collision…** writes the current frame as an ASE model with one closed surface
per box or hull, named `collision_<volume>`, with triangles wound outwards and
**Doom 3 collision material** as its material. The render surfaces are written
exactly as a plain ASE export writes them. Define the material with the
`collision` keyword in the game's material scripts. When a package is open, the
export reads that package's Doom 3 material scripts and warns, in the status
line, when none defines the material or its definition lacks the keyword; the
file is still written, since the game's own materials may define it. A render
surface already named `collision_<volume>`, more than 32 surfaces in all, or an
unsafe material path is refused.

**Export Doom 3 Clip Model…** writes the selected box or hull alone as an ASE
for an entity's `clipmodel` key: Doom 3 builds a trace model from its single
convex volume. The volume must fit the trace model budget of 16 faces, 32 edges
and 32 vertices (fit a hull with the **Doom 3 trace model** budget); a larger
one is refused. For an axis-aligned static box the review also gives the
equivalent `mins` and `maxs` keys.

**Export Doom 3 .cm File…** writes every box and hull as a Doom 3 collision
model file. When Doom 3 loads a static model's collision
(`idCollisionModelManagerLocal::LoadModel`), it first reads the model's path with
the extension changed to `.cm`, without comparing its CRC, and converts the
model's surfaces only when there is no such file. Give the static model's game
path, ending `.ase`, `.lwo` or `.ma` (the formats Doom 3 converts), such as
`models/props/crate.ase`: it names the collision model, and the file belongs at
`models/props/crate.cm` in the package. The file follows the reader in
`CollisionModel_files.cpp`: `CM "1.00"`, a CRC of 0, and one `collisionModel`
with its vertices, edges (edge 0 is the reserved dummy; every edge has two users
and none is internal), a single leaf node and one polygon per volume face,
wound counter-clockwise seen from outside, with its plane, bounds and the
**Doom 3 collision material**, whose definition gives the contents. Animated
volumes are written at the current frame. Keep the `.cm` file in step with the
model; Doom 3 does not regenerate it. Map collision files, which dmap writes
for a whole map with the map's CRC, are not written.

None of these exports runs the game; in-game collision and physics are not
verified.

```sh
vibestudio --cli model collision models/crate.mesh.json --operation export-ase --target doom3 --output models/crate.ase --json
vibestudio --cli model collision models/crate.mesh.json --operation export-clipmodel --target doom3 --box body --output models/crate_clip.ase --json
vibestudio --cli model collision models/crate.mesh.json --operation export-cm --target doom3 --model-path models/props/crate.ase --output models/props/crate.cm --json
```

MDL, MD2, MD3 and OBJ exports do not embed these boxes and report the omission.
Model package staging does not stage collision automatically. Linked assembly
baking reports omitted input collision by default, or carries every part's
volumes into the bake with **Include part collision in bakes** (see
[Part collision in bakes](MODEL_ASSEMBLY.md#part-collision-in-bakes)).

## Source And Recovery

Sources with static collision use `vibestudio.mesh` version 5, or version 6 when
clip FPS is saved. The required version-5
`collisionBoxes` array contains objects with exactly `name`, `centre`, `size`
and `rotation`; all three vectors have three numeric components. Version 5
optionally retains existing version-4 `mdl` metadata. Sources without boxes
use version 6 when clip FPS is saved, otherwise version 4 for native MDL data or version 3. Versions 1–4
remain readable but reject a collision field rather than silently dropping it.

A hull adds a `hull` array of 4–256 points, each three numbers within ±0.5, to
either object shape (static or animated). Releases from before hulls refuse a
source that holds one rather than dropping its shape; the source version number
is unchanged.

Animated collision uses version **7**, retaining optional saved FPS and MDL data.
Its required `collisionBoxes` array accepts the static object above or an animated
object containing exactly `name` and `poses`. `poses` is a nonempty array with
exactly one object per mesh frame, each containing exactly `centre`, `size` and
`rotation`. A pose of a hull may add its own `hull` array, in the form above,
for a shape per frame; pose zero's shape is the volume's own `hull`, and a box
refuses a pose shape. Releases from before per-frame shapes refuse such a pose.
Track objects do not repeat those vectors outside the array. Older
versions reject tracks. Mixed sources round-trip without flattening static or
animated data. In memory, scalar box fields mirror pose zero; validation enforces
that invariant. Use `setModelCollisionFrames` when replacing a track. History
memory estimates include every collision pose.

Collision metadata contributes to revision fingerprints and history estimates.
Recovery retains the chosen collision box, requires a valid exclusive component
selection, and restores an unsaved draft. Source checksums and guarded writes
retain their existing behavior; export does not mark a mesh source saved.

## CLI

`model collision <source.mesh.json>` lists boxes with text or `--json` output.
`--input` can replace the positional source. Unknown, repeated and irrelevant
options are refused. Standard `--settings-file`, `--locale` and `--catalog-root`
arguments remain available. Text inspection lists each name, centre, size and
rotation. Authoring mutations require an output ending `.mesh.json`;
map operations require `.map`. `--dry-run` prepares and validates the full
candidate without writing. Existing derivatives require `--overwrite`; edits to
the loaded source use its guarded source fingerprint.

| `--operation` | Options |
| --- | --- |
| `inspect` | Read-only default. Optional `--frame N` adds `sampledBoxes` and `frame` to JSON. Text shows that pose (zero by default) and track lengths. Complete tracks remain in `collisionBoxes`. |
| `add` | Required `--name`; optional `--centre X,Y,Z`, `--size X,Y,Z`, `--rotation X,Y,Z`. Defaults: centre/rotation zero, size 16. |
| `fit` | Required `--name`; optional `--frame all\|N`, `--surface N`, `--vertices all\|N,…`, `--faces all\|N,…`, `--edges A-B,…`. Indices are zero-based; edges have ascending endpoints. A surface without components is refused; use `--vertices all`. |
| `fit-animated` | Required `--name`; same component selectors as `fit`, always samples every frame. No `--frame` option. |
| `export-ase` | Required `--target doom3` and an `.ase` `--output`; optional `--material` (default `textures/common/collision`), `--frame N` (required for animated volumes) and `--package PATH`, a folder or archive whose Doom 3 material scripts are checked for the material's `collision` keyword. Writes the render surfaces plus one collision surface per volume. JSON reports `collisionSurfaces`, the material, notes and, with `--package`, `materialCheck.state` (`defined`, `undefined` or `no-collision-keyword`) and `materialCheck.source`. |
| `export-cm` | Required `--target doom3`, `--model-path` (the static model's game path, ending `.ase`, `.lwo` or `.ma`) and a `.cm` `--output`; optional `--material` and `--frame N`. JSON `collisionModel` gives `modelPath`, `gamePath` (where Doom 3 looks for the file) and its polygon, edge and vertex counts. |
| `export-clipmodel` | Required `--target doom3`, `--box NAME` and an `.ase` `--output`; optional `--material` and `--frame N`. Writes that volume alone and refuses one over the trace model budget. JSON `clipModel` gives its faces, edges and vertices, and `mins`/`maxs` for an axis-aligned static box. |
| `fit-hull` | Required `--name`; the selectors and `--frame all\|N` of `fit`; `--profile quake\|quake2\|quake3\|doom3-trace` (default `quake3`) and optional `--max-faces K` (4 to the profile's limit). JSON adds `hull` with its vertex, edge and face counts and volume. The other operations accept hulls as they accept boxes. |
| `fit-animated-hull` | As `fit-hull`, without `--frame`: fits a separate shape in every stored frame. JSON `hull.frames` gives each frame's vertex and face counts. |
| `animate` | Required static `--box`; copies it into every frame. |
| `freeze` | Required animated `--box` and `--frame N`; keeps that pose as a static box. |
| `update` | Required `--box existing-name`; optional new `--name`, centre, size and rotation. Omitted fields retain their per-pose values, including under all-frame scope. Animated value updates require explicit `--frame all\|N`; rename alone needs no scope. |
| `transform` | Required `--box`; optional deltas `--offset X,Y,Z`, `--rotate X,Y,Z`, intrinsic local factors `--scale X,Y,Z`; `--transform-space world\|selection\|custom` (default world), custom `--axis-rotation X,Y,Z`; `--pivot-mode selection\|origin\|custom` (default selection), custom `--pivot X,Y,Z`; `--snap-grid`, `--snap-angle`, `--snap-scale`. A pivot supplied alone implies custom. See animated scope rules below. |
| `duplicate` | Required `--box existing-name --name new-name`. |
| `delete` | Required `--box existing-name`. |
| `export-map` | Required `--target quake\|quake2\|quake3`; Quake III also requires `--material`. Optional `--origin X,Y,Z`. |
| `place` | Same target options, plus `--map existing.map`; writes a separate map output through shared placement/serialization. The original map is protected even with `--overwrite`. `--instance entity:N` places through that model entity's transform instead of `--origin`. The brushes form one recorded `func_group`. |
| `placed` | Required `--map`; read-only. Lists every recorded `func_group` with `group` (`entity:N`), `status` (`current`, `stale`, `other-source` or `invalid`), source, revision, frame, target, material, model and brush count. Optional `--target` only hints how to read the map. |
| `refresh` | Required `--map` and `--output`. Replaces every stale record placed from this mesh, or the one named by `--group entity:N`, as one undo step each; `--instance entity:N` follows that instance's current transform. Fails when nothing matches. Protects the input map. |

Animated `transform` requires explicit `--frame all|N`. Selection axes may use
`--axes-frame N`; all-frame CLI selection pivots use pose zero, while a custom
pivot can choose another point. Animated `export-map` and `place` require
`--frame N` and return that frame in JSON. Static sources retain earlier defaults.

```sh
vibestudio --cli model collision models/prop.mesh.json --operation fit-animated --name body --output models/animated-collision.mesh.json
vibestudio --cli model collision models/animated-collision.mesh.json --operation transform --box body --frame 2 --offset 0,0,4 --output models/animated-collision.mesh.json
vibestudio --cli model collision models/animated-collision.mesh.json --operation export-map --frame 2 --target quake2 --output maps/pose-clip.map --json
```

```sh
vibestudio --cli model collision models/prop.mesh.json --operation fit --name body --frame all --output models/prop-collision.mesh.json --dry-run --json
vibestudio --cli model collision models/prop.mesh.json --operation fit --name body --output models/prop-collision.mesh.json
vibestudio --cli model collision models/prop-collision.mesh.json --operation transform --box body --offset 0,0,8 --rotate 0,0,15 --scale 1.25,1,1 --output models/prop-collision.mesh.json
vibestudio --cli model collision models/prop-collision.mesh.json --operation export-map --target quake2 --origin 128,0,0 --output maps/prop-clip.map --json
vibestudio --cli model collision models/prop-collision.mesh.json --operation place --target quake3 --material common/clip --map maps/source.map --output maps/with-prop-clip.map --json
vibestudio --cli model collision models/prop-collision.mesh.json --operation place --target quake3 --material common/clip --map maps/source.map --instance entity:12 --output maps/with-prop-clip.map --json
vibestudio --cli model collision models/prop-collision.mesh.json --operation placed --map maps/with-prop-clip.map --json
vibestudio --cli model collision models/prop-collision.mesh.json --operation refresh --map maps/with-prop-clip.map --output maps/refreshed.map --json
```

Exit codes are 0 success, 2 invalid/irrelevant arguments and 4 failed source,
validation, placement or file publication. Map results include brush count,
material, target, output path, write state and target notes. Original-engine
collision acceptance and cross-platform release checks remain on the
[modeller release gate](MODELLER_RELEASE.md).

## Compiler Verification

The optional `src/tests/model_collision_compiler_workflow.py` proof creates two
rotated volumes, transforms one through local scaling and world rotation/movement,
and creates a sealed room and original texture/palette/shader assets. It
runs authoring, export and placement through the real CLI, then invokes the
supplied Quake and Quake III compilers: VibeMap2 `vibemap2-bsp` through
`--qbsp` and VibeMap3 `vibemap3` through `--q3map2`. Stock ericw-tools `qbsp`
and q3map2, which the recorded runs used, are accepted too. It independently
reads BSP29 hulls and IBSP38/46 brush planes, leaf references, contents and
draw surfaces. Quake hulls 1/2 contain the boxes while draw hull 0 remains
empty at those positions;
Quake II/III contain player-clip brushes with no visible clip faces.

```sh
python src/tests/model_collision_compiler_workflow.py --binary <vibestudio> --qbsp <vibemap2-bsp> --q3map2 <vibemap3> --output-root .agents/tmp/modeller-collision-compiler
```

Use a new output directory inside `.agents/tmp`. The proof records binary and
BSP hashes and per-step diagnostics, protects the mesh and input room, and
refuses leaks or compiler warnings. It does not launch a game or establish
engine movement, shader availability in another project, or release-package
acceptance. Compiler executables remain optional external tools.

The separate [model engine workflow](MODEL_ENGINE_ACCEPTANCE.md) repeats this
compiler proof and exercises generated BSP29/IBSP38/IBSP46 data through FTE's
dedicated server. It tests clip point/player/large hull behavior, clear movement,
floor contact and `walkmove`, with an intentionally unclipped room as a negative
control. Its QuakeC game and server findings do not establish the original
Quake II/III game runtimes or graphical client acceptance.
