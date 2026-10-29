# Vertex animation frames

**Available** — frame-range editing, frame naming, clip resampling and loop
closing, a frame timeline with onion skinning, tags that follow the mesh,
range sculpting, and frame import and export for vertex-animated models
(Quake MDL, Quake II MD2, Quake III MD3 and editable sources). They use the
shared Mesh Editor document and the `model frames` command. VibeStudio is
pre-alpha: these tools are tested on synthetic fixtures only, not on real game
projects, and the [release checklist](MODELLER_RELEASE.md) is still open.

All of them are on the Mesh Editor's **Animation** page, below
**Animation clips**, except **Follow Selection**, which is in **Attachment
tags**, and the timeline, which sits beside the frame list in the transport.
Every operation is one undo step, runs on the document worker, and refuses
without changing anything when it cannot complete. A disabled button's tooltip
says why it cannot run.

## Move, copy, delete and reverse frames

Under **Frame range**, set **Range first frame** and **Range last frame**
(zero-based, as in the transport; Shift-drag on the timeline sets both), then:

- **Move Frames** moves the range so its first frame lands at **Move to
  frame**: the index it has after the move.
- **Duplicate Range** inserts a copy of the range right after it.
- **Delete Range** removes the range. A model always keeps one frame.
- **Reverse Range** reverses the range in place.
- **Add Ping-Pong Return** keeps the range and inserts the frames between its
  ends in reverse after it (`1 2 3 4` becomes `1 2 3 4 3 2`), so a looping clip
  returns to its first pose. It needs at least three frames.

Everything stored per frame follows the frames:

- surface positions and normals, frame names and origins;
- attachment tags and animated collision poses;
- Quake MDL native frame groups and their timing: frames that stay next to
  frames of their own timed group stay in it with their own hold times, and a
  group split by a move becomes two groups;
- clip ranges, by these rules:
  - **Move**: a clip entirely inside the moved range moves with it. A clip
    partly inside keeps its other frames and takes the whole moved range when
    the range lands next to or inside what is left of it. A clip the range
    lands strictly inside grows by the range.
  - **Duplicate** and **ping-pong**: a clip holding the range's last frame
    grows by the inserted frames; later clips move.
  - **Delete**: clips shrink, and a clip left with no frames is removed.
  - **Reverse**: clip ranges stay as they are.

On skeletal models, frames that follow the skeleton (the bind frame and frames
baked from a skeletal clip) stay in their clips while any surface is skinned:
an operation that would move, copy, remove or reorder them is refused and
names the skeletal tool to use instead. Independent frames edit freely and
the clip frames keep their mapping. Once every surface is unbound, the frames
are ordinary vertex animation; a skeletal clip whose frames no longer play in
order becomes independent frames. Frame edits keep every vertex's influences
unchanged ([skin weights through mesh edits](MODEL_SKIN_PROVENANCE.md)).

The frame and frame-vertex limits (1,024 frames, 1,048,576 frame vertices) are
checked before anything changes.

## Name frames so clips survive native files

MDL, MD2 and MD3 have no clip table. Readers, including VibeStudio's own,
rebuild clips from frame names: consecutive frames sharing a stem with a
trailing number (`stand1`, `stand2` ...) form one clip named after the stem.
Quake and Quake II game code also address frames by those names and numbers.

The **Native clip check** line under **Frame range** says how many clips come
back from the frame names, and which do not. A clip does not come back when
its frames are named differently, when neighbouring frames share its stem, or
when its name is not a stem a frame name can hold. Saved clip FPS never comes
back from these formats; set timing in the game's own animation files.

**Name Frames From Clips** renames the frames of the clip chosen under
**Animation clips**, or of every clip when **All frames** is chosen:

- Each frame becomes `<stem><number>`, numbered from **First frame number**
  (default 1, as in `stand1`). The stem is the clip name's ASCII letters,
  digits, `_` and `-`, with other characters turned into `_`.
- Names fit the 15 printable ASCII characters these formats hold. A long stem
  is shortened once for the whole clip, sized for its highest number, so every
  frame keeps the same stem.
- A stem ending in a digit gets a `_` before the number (`attack2_1`), so the
  reader does not take the digit for part of the frame number.
- With **Rename clips to their frame stem** on (the default), a clip whose name
  is not a usable stem is renamed to the stem, so the name comes back too.

The operation is refused when two chosen clips overlap (a frame has one name),
when two clips would share a stem, when a renamed clip would take another
clip's name, when frames next to a clip already use its stem, or when it would
break a clip that comes back today.

Other operations keep names consistent too:

- **Insert In-between Frames** with an empty **Frame-name prefix** names the
  new frames after the stem the two endpoint frames share (`walk_001`), so they
  stay in the clip; without a shared stem they are named `blend_001` and so on.
- Resampling and frame import with a new clip name number their frames after
  the clip.
- Frames baked from a skeletal clip use one stem for the whole clip, sized for
  its last frame number, and a clip name ending in a digit gets a `_` before
  the number. Earlier builds shortened the stem as numbers gained digits,
  which split long clips in native files.

MD2, MD3 and MDL exports still list clips that will not come back in their
report, as an omission a strict export refuses.

## Resample a clip and close its loop

Animation is often made at 24 or 30 FPS, while Quake and Quake II game code
step model frames at 10 Hz and MD5 needs a whole frame rate. Under **Resample
and loop**, with a clip chosen under **Animation clips**:

1. Set **New FPS**, or set it to **Use frame count** and give **New frame
   count**. **Current FPS** is the clip's saved rate unless you give another.
2. Choose **Sampling**: **Blend** blends positions and origins linearly,
   normals by normalised blending, attachment tags and collision orientations
   along the shortest arc; **Nearest frame** copies the nearer stored frame.
3. Turn on **Looping clip** for a cycle: the step from the last frame back to
   the first is sampled too, and the new frames divide the whole loop evenly.
4. Choose **Resample Clip**.

The clip's frames are replaced and renamed after the clip (its frames' shared
stem, numbered from the first frame's number). The clip's FPS becomes the new
rate, or, for a frame count, the rate that keeps its duration. A clip with the
same range or holding the whole clip follows it; later clips move; a clip that
partly overlaps it is refused. A Quake MDL timed group that is exactly the
clip keeps its total time, split evenly over the new frames; single native
frames stay single.

**Close Clip Loop** removes the jump from a clip's last pose to its first, as
**Close loop by** says: **Replacing the last frame** with the first pose,
**Appending the first pose** after the last frame, or **Cross-fading** the last
**Cross-fade frames** frames toward the first pose (the k-th of N moves
k / (N + 1) of the way).

Skeletal clips have their own **Skeletal clip** list and buttons. **Resample
Skeletal Clip** samples joints in parent space (rotation slerp, translation
and scale blending) and re-bakes the clip's frames. Joints with animation
curves follow their curves: keys are rescaled to the new length and must each
land on a whole frame, so a looping clip or a count that would put a key
between frames is refused unless **Bake curves** samples the motion and
removes the curves. **Close Skeletal Loop** makes the same choices; on a clip
with curves it keys each channel the same way (the first value at the last
key, an appended key, or blended keys over the cross-faded poses; rotations
close to the nearest equivalent angle), so the curves keep authority. **Bake
curves** closes the loop on sampled poses and removes the curves instead.

The same two operations are in **Skeleton → Edit Skeletal Animation** as
**Resample clip** and **Close clip loop**, with **New FPS** or **New pose
count**, **Sampling**, **Looping clip**, **Close loop by**, **Cross-fade
poses** and **Bake curves**.

## See neighbouring frames: the timeline and onion skin

The **Frame timeline** beside the frame list shows frame numbers, each clip as
a bar (overlapping clips stack in up to three lanes), Quake MDL timed groups
as brackets, the frame range as a dashed band, and the current frame as a
heavy line. It is a slider: screen readers hear its value and a description
of the frame, the clips holding it and the range. Time runs left to right in
every layout direction.

- Click or drag to choose a frame; Shift-click or Shift-drag selects the
  range the range tools use.
- **Left** and **Right** step one frame, **Page Up** and **Page Down** ten,
  **Home** and **End** go to the ends; with **Shift** they extend the range.

**Onion skin** draws neighbouring frames as wireframes over the 3D view:
**Frames before** (dashed) and **Frames after** (dotted), every **Onion step**
frames, fainter with distance. In high-visibility themes the ghosts use the
text colour at full strength; the line style tells earlier from later without
colour. Every pane of the four views draws the ghosts through its own camera.
Ghosts are hidden while the animation plays, and the checkbox's description
lists the frames shown. Very large models draw as many ghosts as fit a fixed
amount of work per redraw.

**Toggle Onion Skin** is a registered command: find it with **Search Commands**
(F3), or bind a key to it in the controls of any editor profile. It has no key
by default.

## Make a tag follow the mesh

Quake III players and weapons made as vertex animation need `tag_weapon`,
`tag_flash`, `tag_torso` and `tag_head` to follow the hand, muzzle or neck in
every frame. In **Attachment tags**:

1. Select the vertices, edges or faces the tag should follow (three or more
   points not in a line for rigid or surface following).
2. Type the tag's name in **Name**. A new name adds the tag at the
   selection's centre with model-aligned axes.
3. Choose **Follow by**: **Rigid fit** (the selection's best-fit rotation and
   movement), **Centroid** (its centre only, axes kept) or **Surface** (the
   selected faces' normal and first edge).
4. Choose the **Reference frame** whose tag pose sets the tag's offset from the
   selection, and optionally **Only the frame range**.
5. Choose **Follow Selection**.

The rigid fit uses Horn's closed-form unit-quaternion solution, so the
rotation is always proper and a reflected tag basis keeps its handedness. The
**Tag fit result** line reports the largest RMS residual and its frame: a large
residual means the selection does not move rigidly there. A frame where the
selection collapses onto a line refuses the whole edit. The tags written are
ordinary per-frame MD3 tags, not a live constraint.

With **Remember the components** on (the default), the editable source keeps
an anchor: the tag, its surface, the components, the mode and the frames.
After moving, sculpting, resampling or importing frames, choose **Refit
Anchored Tags** to fit every anchored tag again in one undo step. The
**Anchored tags** line lists each anchor; an anchor whose surface gained or
lost vertices or faces, was renamed or removed, or whose frames no longer
exist cannot be refitted, and the line and the disabled button say why:
select the components and choose **Follow Selection** again. Renaming a tag
keeps its anchor and deleting it removes the anchor. Sources with anchors are
saved as editable source version 19.

Assemblies read the fitted tags like any other: a part attached at the tag
follows it frame by frame (checked with a synthetic assembly; not yet in a
game).

## Sculpt over a range of frames

Under **Range sculpt**, select vertices, edges or faces and set the frame range
and **Falloff over the range** (**Constant**; **Ramp in**, growing toward the
last frame, k / n; **Ramp out**, fading after the first; **Smooth**, a bump
strongest in the middle, sin²(πk / (n + 1))):

- **Transform Selection Over Range** applies the **Geometry** page's move,
  rotate and scale values to the selection in every frame of the range,
  scaled by the falloff. The pivot is resolved once, in the displayed frame,
  so the motion stays coherent. Mirroring is refused.
- **Blend Selection From Frame** moves the selection toward **Blend from
  frame**'s pose by **Blend amount** times the falloff.
- **Add Pose Difference** adds the selection's movement from **Base frame** to
  **Pose frame**, times **Difference weight** (-10 to 10) and the falloff, like
  a shape key.

Unselected vertices, frames outside the range, tags and collision stay exact.

The same range applies to the **Geometry** page and the 3D view's modal move,
rotate and scale when **Geometry → Over the frame range** is on (beside
**Apply to**): the transform covers the frame range with the Range sculpt
falloff, using the chosen axes and pivot, resolved once in the displayed
frame. The live preview shows the full amount on the displayed frame; the
committed edit scales each frame by its falloff weight. Proportional editing
and mirror are refused with the scope on; tags and collision boxes keep
**Apply to**.
Normals are rebuilt around the moved vertices only; a whole-surface transform
or blend carries its normals instead.

## Import frames from other files

Most vertex animation is made in another modeller and brought in as one mesh
per frame. Under **Import and export frames**:

1. Choose **Match vertices by**: **Face corners** (the default: the same faces
   in the same order, however the exporter split vertices), **Exact vertex
   order**, or **Nearest position and UV** (each vertex takes the source
   vertex with its UV at the same place as the displayed frame, within 0.001
   units).
2. Set **Insert after frame** (**End** appends), or **Replace frames** to
   overwrite frames from there.
3. Optionally name a **New clip name** and **New clip FPS**; the frames are then
   numbered after the clip.
4. For OBJ files, set **OBJ up axis** (**Y up** for Blender's default export)
   and **OBJ scale**.
5. Choose **Import Frames…** and pick an OBJ sequence (one frame per file, in
   natural file-name order: `walk_2.obj` before `walk_10.obj`) or an MD2, MD3,
   MDL or editable source of the same mesh (every frame it holds).

Each source surface is matched by name, else by index, and must have the
target's triangles. Tags are taken from the source by name; other tags and
collision poses copy the frame before the insertion. A clip the new frames land
strictly inside grows. The first mismatching surface, triangle or vertex is
named and nothing changes. The model's own source cannot be a frame source.

**Export Range as OBJ…** writes each frame of the frame range as its own OBJ
through the guarded one-frame export. `#` in the name marks the zero-padded
frame number (`walk_###.obj`); without one, `_###` is added. Nothing is written
when a file already exists.

### OBJ import settings

OBJ is the generic interchange format, so imports accept typical Blender
exports. Opening an OBJ with **Open or Import Model** asks for these settings
in **OBJ Import Settings**, starting from the last ones chosen in the session;
`model import` and frame import take them as options:

- **Up axis**: Y up and -Z forward converts to Z up (a rotation, so faces keep
  their winding); Z up keeps coordinates.
- **Scale** multiplies positions after the conversion.
- **Material libraries** (`mtllib`): read from beside a loose OBJ, or from the
  package entry beside an OBJ in a package (the package browser and package
  previews). A material with a diffuse map (`map_Kd`) binds that image,
  relative to the OBJ's package folder, or by file name for absolute paths;
  other materials keep their names. Colours, specular and other shading statements have no idTech
  equivalent and are noted. Without the file, or with **ignore**, material
  names are kept; **refuse** stops the import.
- **Loose data**: line (`l`) and point (`p`) records and vertices no face uses
  are dropped with a note, or refused on request.

The import report lists every conversion and drop, and the Mesh Editor's
status line repeats it after opening.

## Command line

`model frames` runs every operation through the shared document service.
Without `--operation` it lists each clip, whether it comes back from frame
names and what the names give instead.

```sh
vibestudio --cli model frames ./models/soldier.mesh.json --json
vibestudio --cli model frames ./models/soldier.mesh.json --operation move --first 10 --last 14 --to 0 --output ./models/moved.mesh.json --json
vibestudio --cli model frames ./models/soldier.mesh.json --operation name-from-clips --clip 2 --start 0 --keep-clip-names --output ./models/named.mesh.json
vibestudio --cli model frames ./models/soldier.mesh.json --operation resample --clip 0 --fps 10 --output ./models/10fps.mesh.json --json
vibestudio --cli model frames ./models/soldier.mesh.json --operation follow-tag --tag tag_weapon --surface 0 --faces 12,13 --output ./models/tagged.mesh.json --json
vibestudio --cli model frames ./models/soldier.mesh.json --operation import --source ./obj/run_01.obj --source ./obj/run_02.obj --clip-name run --obj-up y --output ./models/run.mesh.json --json
vibestudio --cli model frames ./models/soldier.mesh.json --operation export-obj --first 0 --last 9 --output ./obj/walk_###.obj
```

| Operation | Options |
| --- | --- |
| `move`, `duplicate`, `delete`, `reverse`, `ping-pong` | `--first`, `--last`; `move` also `--to` |
| `name-from-clips` | `--clip N`, `--start N` (0-1000, default 1), `--keep-clip-names` |
| `resample`, `resample-skeletal` | `--clip N`, `--fps F` or `--frames N`, `--source-fps F`, `--method linear\|nearest`, `--cyclic`; skeletal also `--bake-curves` |
| `close-loop`, `close-skeletal-loop` | `--clip N`, `--mode replace\|append\|crossfade`, `--blend-frames N`; skeletal also `--bake-curves` |
| `follow-tag` | `--tag NAME`, `--surface S`, `--faces`/`--vertices`/`--edges`, `--reference-frame F`, `--mode rigid\|centroid\|surface`, optional `--first --last`, `--no-anchor` |
| `refit-tags` | optional `--tag NAME` (default every anchored tag) |
| `transform-range` | `--first`, `--last`, components, `--falloff constant\|ramp-in\|ramp-out\|smooth`, `--offset`, `--rotate`, `--scale`, `--pivot-mode origin\|selection\|custom`, `--pivot`, `--pivot-frame` |
| `blend-pose` | `--first`, `--last`, components, `--falloff`, `--source-frame F`, `--amount 0..1` |
| `relative-pose` | `--first`, `--last`, components, `--falloff`, `--pose-frame A`, `--base-frame B`, `--weight -10..10` |
| `import` | `--source FILE` (repeatable), `--match corners\|exact\|nearest`, `--tolerance`, `--reference-frame`, `--insert-after N` or `--replace-from N`, `--clip-name`, `--clip-fps`, `--obj-up z\|y`, `--obj-scale` |
| `export-obj` | `--first`, `--last` (default every frame), `--output pattern.obj` |

Edits need `--output` ending in `.mesh.json`, different from the input.
`--dry-run` validates without writing; `--overwrite` replaces an existing
output. JSON output lists `frames`, `frameNames`, and `clips` with
`survivesNativeNames`, `rateLost`, `rebuiltName`, `rebuiltFirst` and
`rebuiltLast`, plus `nativeClipSummary`; `follow-tag` adds `tagFollow` with
per-frame `residuals`; `follow-tag` and `refit-tags` list `tagAnchors` with any
`problem`; `import` adds `importedFrames`, skeletal operations add
`skeletalClip`, and `export-obj` lists `files`. Usage errors return 2,
unreadable sources and outputs 1, and refused edits 4 without writing.

`model import` takes the OBJ settings as `--obj-up z|y`, `--obj-scale N`,
`--obj-materials resolve|ignore|refuse` and `--obj-loose drop|refuse`, and its
JSON lists `notes`.

## Limits

- Frame operations work on whole frames across every surface; there is no
  per-surface frame order.
- Frames that follow a skinned skeleton cannot be moved, copied, reversed,
  resampled or replaced with these tools; use the skeletal clip tools.
- Clip names longer than a frame name holds come back shortened, as the stem.
- Tag following is baked, not a constraint: refit anchored tags after
  geometry edits. Anchors do not survive topology edits on their surface, and
  an assembly child on a fitted tag has not been checked in a game.
- The range scope's live preview shows the full transform on the displayed
  frame rather than its falloff weight; proportional editing and mirror do not
  combine with it.
- Frame import matches whole surfaces; it cannot import a subset of surfaces
  or remap differing topology.
- There are no animation events or markers on the timeline yet.
