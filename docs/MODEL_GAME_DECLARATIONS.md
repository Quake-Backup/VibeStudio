# Animation events and game declarations

**Status: Partial.** Animation events and five generated game-side
declarations are available in the Mesh Editor and the command line. The
generated text follows the published formats, but none of it has been loaded
in the original engines yet. VibeStudio is pre-alpha and largely untested in
real projects.

A model file is only half of what a game needs: Doom 3 and Quake 4 find their
animations through a `model` declaration, Quake and Quake II game code names
frames with macros, and GZDoom maps sprite frames to model frames in MODELDEF
and replaces sprites with voxels in VOXELDEF.
VibeStudio generates these from the editable source, so a change of frames or
clips cannot silently leave game code behind.

## Animation events

An event is a game-side frame command on one frame of a clip: a footstep, a
sound, a melee or fire trigger. Each has a frame (numbered from zero within
the clip), a command and arguments, such as `sound_body snd_footstep` or
`melee melee_impLeftClaw`. Commands are one word of ASCII letters, digits,
underscores, dots or hyphens; arguments are printable words without spaces,
quotes, braces or comment marks.

Skeletal models keep events on their skeletal clips; models without joints keep
them on their animations. Open **Skeleton** > **Animation Events…**, choose the
clip, then **Add Event** (it starts at the frame the editor shows) and type the
command line. **Apply** replaces the clip's events as one undo step
(`EditAnimationEvents`). The editable source keeps events (mesh version 15).

Events stay on their frames through edits:

| Edit | What happens to events |
| --- | --- |
| Duplicate frame, insert in-betweens, delete a frame before them | They move with their frame. |
| Delete a frame that carries an event | Refused; move or remove the event first. |
| Resize a skeletal clip below an event, change an animation's range so an event falls outside | Refused, naming the event. |
| Duplicate a skeletal clip | The copy has the same events. |
| Other frame edits that move an animation with events | Refused until its events are removed. |
| Add joints to a model whose animations have events | Refused: move the events to skeletal clips. |

Opening a Doom 3 mesh keeps the frame commands of the `.def` declarations that
name it as events of the clips they declare (one-based frame numbers become
zero-based). A command the studio cannot keep is listed as a warning.

```sh
vibestudio --cli model events models/imp.mesh.json --json
vibestudio --cli model events models/imp.mesh.json --operation add --clip 2 --frame 5 --command "sound_body snd_footstep" --output models/imp-steps.mesh.json
```

`model events` lists every clip's events (`--clip N` or `--animation N` narrows
it) and edits them with `--operation add` (`--frame`, `--command "command
args"`), `remove` (`--index`), `move` (`--index`, `--frame`) or `replace`
(`--events file.json`, a JSON array of `{frame, command, args}`), with
`--output .mesh.json`, `--dry-run` and `--overwrite`.

## Generate declarations

Open **Skeleton** > **Game Declarations…** and choose the **Declaration**. The
preview updates as you type; **Copy** puts it on the clipboard and **Save As…**
writes it. A model the declaration cannot describe (a frame name game code
cannot use, a path outside the game folder) is explained instead.

| Declaration | What is written |
| --- | --- |
| Doom 3 / Quake 4 model declaration | `model NAME { mesh PATH  anim CLIP PATH { frame N command } ... }`: the md5mesh path, one `anim` per skeletal clip at `<anim folder>/<clip>.md5anim`, each clip's events as one-based `frame` lines, and optional `inherit`, `skin` and `offset ( x y z )`. |
| QuakeC frame macros | `$cd`, `$base`, `$skin` and every frame name in order with `$frame`, grouped and commented by animation, with events as comments. Frame names must be unique QuakeC names (letters, digits, underscores). |
| Quake II frame header | `#define FRAME_name index` for every frame, grouped by animation, and `#define MODEL_SCALE`, as id's `m_*.h` monster headers. |
| GZDoom MODELDEF | A `Model ACTOR { Path, Model 0, Skin 0, Scale, Offset, AngleOffset, FrameIndex ... }` block. Each animation takes sprite names of three characters plus `0`-`9`/`A`-`Z`, 29 frames (`A`-`Z`, `[`, `\`, `]`) per name; the comments list which sprite frames each animation uses, for the actor's states. |
| GZDoom VOXELDEF | One `SPRITE = "voxels/name.kvx"` entry per replaced sprite (four characters for every frame, five for one frame), with a `{ Scale, AngleOffset, Spin or PlacedSpin and DroppedSpin, OverridePalette }` block when any option is set. The scale keeps a decimal point, as GZDoom reads it as a floating-point number; GZDoom adds its own 90 degrees to the angle offset. Voxels are drawn unanimated, so an animated model is refused. VibeStudio reads KVX but does not write it: the entry names the `.kvx` file the package ships, such as the one the Mesh Editor opened. |

**Export Model Set…** (Doom 3) writes the md5mesh, every clip as an md5anim and
`def/NAME.def` under a chosen game folder, at the paths the declaration names,
in a worker with progress; it checks every destination first and does not
overwrite an existing set. Reopening that md5mesh in the studio finds every
clip under its declared name with its events.

**Stage in Package** (Doom 3, shown while a folder, ZIP, PK3 or PK4 package is
open in the studio) builds the same set on the worker and stages it into the
active package through the package staging service as one package operation,
so one undo in the package removes it all and nothing is staged when any file
is refused. Files already at those paths are refused unless **Replace files
already in the package** is on. Save the package to write them; the package
view and activity log show the staged files.

```sh
vibestudio --cli model declarations models/soldier.mesh.json --target quakec --cd progs/soldier --output progs/soldier_frames.qc
vibestudio --cli model declarations models/soldier.mesh.json --target quake2 --cd models/monsters/soldier
vibestudio --cli model declarations models/imp.mesh.json --target doom3 --name monster_imp --mesh-path models/md5/monsters/imp/imp.md5mesh --set ./out/base --json
vibestudio --cli model declarations models/imp.mesh.json --target gzdoom --actor DoomImp --path models/imp --model imp.md3 --sprite IMP
vibestudio --cli model declarations voxels/ceye.kvx --target gzdoom-voxeldef --sprites CEYE --angle-offset 90 --output VOXELDEF.ceye
```

Without `--output` (or with `-`) the text goes to standard output and to the
JSON `text` field. `--set DIR` (Doom 3 only) writes the whole model set;
`--dry-run` lists the planned files, every destination is checked before any is
written, and existing files need `--overwrite`. Options belong to their target
and are refused for another: Doom 3 `--name`, `--mesh-path`, `--anim-dir`,
`--skin`, `--inherit`, `--offset x,y,z`; QuakeC `--cd`, `--base`, `--skin`;
Quake II `--cd`, `--prefix`, `--scale`; GZDoom `--actor`, `--path`, `--model`,
`--skin`, `--scale x,y,z`, `--offset x,y,z`, `--angle-offset`, `--sprite ABC`;
VOXELDEF (`--target gzdoom-voxeldef`) `--sprites ABCD,ABCDE`, `--path
voxels/NAME.kvx` (the default uses the source's name), `--scale`,
`--angle-offset`, `--spin` or `--placed-spin` and `--dropped-spin`, and
`--override-palette`. For VOXELDEF the source can be the `.kvx` file itself as
well as an editable source. Staging into a package is a studio action; the
command line writes files.

## Limits

- Generated text has not been compiled or loaded by FTEQCC, the Quake II game
  DLL, Doom 3 or GZDoom yet; the tests parse the Doom 3 declaration with the
  studio's own `.def` reader and the MODELDEF with an independent tokenizer,
  and compare VOXELDEF with golden text written from the ZDoom wiki and
  GZDoom's parser.
- Events have no timeline markers in the viewport yet, and the QuakeC and
  Quake II outputs carry them only as comments.
- Anim flags (`ai_no_turn`, `prevent_idle_override`, `random_cycle_start`)
  and `channel` blocks of existing declarations are not kept; multi-model
  MODELDEF blocks are not generated, and VOXELDEF does not write the KVX file
  itself.
