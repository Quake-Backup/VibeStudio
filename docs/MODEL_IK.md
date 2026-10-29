# Place hands and feet with inverse kinematics

**Available:** two-bone inverse kinematics (IK) over retained skeletal clip
samples, with a model-space target and bend pole, and
[long-chain IK](#solve-longer-chains) (FABRIK or CCD) with
[joint limits](#limit-joint-rotation), targets that follow a joint, per-sample
targets and targets placed at the 3D cursor. VibeStudio remains pre-alpha
and largely untested in real projects. This does not establish the
[production gate](MODELLER_RELEASE.md).

## Review a pose

Open **Mesh Editor → Skeleton → Inverse Kinematics**. Choose a clip
and an **End joint**. With the **Two bones (exact)** solver its parent and
grandparent form the lower and upper bone;
the resolved chain appears beneath the controls. Each bone must have a nonzero
length in every affected sample. The grandparent's origin stays fixed.

1. Choose **First sample** and **Last sample**, inclusive and zero-based within
   the clip. A range can keep a foot planted while the root moves.
2. Set **Target X/Y/Z** to the desired end position in model units.
3. Set **Pole X/Y/Z** to a point on the desired elbow or knee side. The pole
   chooses the bend plane; it is not an extra joint. Keep it off the line from
   the root to the target whenever the solution bends.
4. Choose **Review**, compare **Reviewed result** and **Original model** in
   **Preview**, and inspect every affected sample in **Results**.
5. **Apply** commits one undoable edit. Changing a setting requires another
   review. **Cancel** discards the candidate and cancels an active worker.

**Use First Sample Pose** fills the target from the current end position and
the pole from the current bend. A straight chain uses a perpendicular model
axis; adjust it to match the intended elbow or knee direction. **Target at 3D
Cursor** and **Pole at 3D Cursor** take the editor's 3D cursor, so a target can
be placed by putting the cursor on a point in the viewport first. The pole
stays constant across the selected range. The target does too unless
**Target source** is **Follow a joint**: each sample then aims at that joint's
pose applied to the target fields, read as an offset in its space, so a hand
can stay on a moving prop. The followed joint must not move with the chain.
The GUI and the CLI both solve
any clip with poses, including clips outside the
[working set](MODEL_SKELETAL_WORKING_SET.md); the preview skins those one sample
at a time.

## Solve longer chains

Choose **Chain (FABRIK)** or **Chain (CCD)** as the **Solver** and a **Root
joint**: any ancestor of the end joint two or more bones up (spines, necks,
tails, tentacles, three-segment limbs; up to 256 joints). The root stays in
place and every bone keeps its length.

- FABRIK (forward and backward reaching, after Aristidou and Lasenby) moves the
  joint origins towards the target and back towards the root, then, with
  **Steer the bend with the pole**, turns each interior joint about the line
  through its neighbours towards the pole, so the bend is deterministic. A
  straight starting chain leans slightly towards the pole first.
- CCD (cyclic coordinate descent) turns each joint from the end back to the
  root so the end points at the target. It ignores the pole.

The solved joint origins become rigid swings of the existing affine bases, as
the two-bone solve does, so scale and shear stay. **Iterations** caps the
passes per sample (1–1000, default 32); a sample stops as soon as the end is
within the tolerance: ten parts per million of the chain's length, at least
0.0001 units, or `--tolerance`. A target beyond the chain's reach, or one the
solver does not reach within the iterations, is refused unless **Nearest
reachable** accepts the closest pose. Results report each sample's target too.

## Limit joint rotation

Open **Edit Skeleton**, choose **Set joint limits**, a joint, **Limit rotation**
and the minimum and maximum angles about X, Y and Z. The angles are the joint's
parent-local rotation relative to its bind pose, applied X then Y then Z (the
animation curves' convention), from −180 to 180 degrees. **No limits** clears
them. Limits are saved in the editable source (mesh version 15) and do not
change existing poses.

With **Apply joint limits**, chain solves clamp every joint they turn after each
pass: the joint's relative rotation is decomposed into those angles, clamped and
rebuilt, and its subtree follows. Every turned joint therefore ends inside its
limits; a target that cannot be reached within them is refused unless
**Nearest reachable** is chosen. The end joint keeps its orientation (or follows
the last bone) and is not clamped. The two-bone solve does not apply limits.

`model rig --operation limits --joint knee.L --min -150,0,0 --max 0,0,0`
sets limits from the command line (`--clear` removes them), and `model rig`
listings report each joint's limits.

## Choose reach and orientation policies

**Require target** refuses the entire operation if any sample is unreachable.
**Nearest reachable** explicitly clamps the end to the chain's reach. Neither
policy stretches bones. **Results** reports the remaining target distance and
whether the target was reached, including unchanged but unreachable poses.
For bone lengths `a` and `b`, reach is between `abs(a-b)` and `a+b`.

**Keep end orientation** preserves the hand or foot's original model-space
basis. **Follow lower bone** rotates it with the lower bone. Children of the
end follow this choice. Other branches follow their nearest chain ancestor.
The solver applies rigid swings to the existing affine bases, retaining scale
and shear rather than reconstructing poses from decomposed rotations.

At a fully folded equal-length chain, the pole selects the middle joint's
direction from the root. At an unreachable target exactly on the root, clamping
uses the previous root-to-end direction, or the upper bone direction if folded.
Opposite directions use a deterministic half-turn axis. The two-bone solve has
no joint-angle limits, stretch, twist controls or persistent IK constraints.

The positional tolerance is `max(0.00001, (a+b)*0.000002)` model units. Coordinates
must be finite and within ±1,000,000. If float storage cannot preserve bone
lengths within that tolerance, the edit fails. Reapplying an already solved
constraint within tolerance leaves the pose and undo history unchanged.

## Preserve the authoring workflow

IK writes ordinary dense skeletal samples. The bind pose and weights stay
unchanged. The shared skeletal rebuild updates skinned geometry and linked
attachments only where their poses change. Independently edited affected
geometry, normals or attachment poses refuse the edit; unaffected independent
edits survive. Frame names/origins, custom animation ranges, manual tags,
collision tracks, material bindings and UVs remain intact. A changed clip's
cached bounds are invalidated. Save, undo, redo and recovery use the normal
document services and existing editable source format.

If an affected joint has [stored curves](MODEL_ANIMATION_CURVES.md), a solve
that would invalidate those curves is refused. Bake the affected joint curves
explicitly before using IK; sampled motion remains and undo restores the keys.

Export through the existing MD5/IQM or baked MDL/MD2/MD3 path and its format
checks. Refit collision explicitly after posing, refresh derivatives used by
level placement and packaging, and review game animation scripts and playback.
There is no automatic package publication, map compilation or engine launch.
Native round-trip fixtures do not prove acceptance in the original engines.

## Automate the solve

```sh
vibestudio --cli model ik ./models/player.mesh.json --clip 0 --first 0 --last 8 --end-joint foot.L --target 0,8,0 --pole 0,4,8 --output ./models/planted.mesh.json --dry-run --json
```

Input is positional or supplied with `--input`. All of `--clip`, `--first`,
`--last`, `--end-joint`, one target and `--output .mesh.json` are
required, and the two-bone solve needs `--pole`. Joint names match exactly.
The target is `--target x,y,z`, `--target-joint NAME` with optional
`--target-offset x,y,z`, or `--targets file.json` holding one `[x, y, z]` per
sample from first to last. `--root-joint NAME` with `--solver fabrik`
(the default for chains) or `--solver ccd` solves a chain; chain options are
`--iterations`, `--tolerance` and `--limits`, and `--pole` is optional. Optional
flags are `--nearest-reachable`,
`--follow-end`, `--dry-run`, `--overwrite` and normal CLI output/localisation
options. Repeated, unknown or malformed options are refused.

```sh
vibestudio --cli model ik ./models/monster.mesh.json --clip 2 --first 0 --last 30 --root-joint spine1 --end-joint head --solver fabrik --limits --target-joint prop --target-offset 0,0,8 --output ./models/look.mesh.json --json
```

JSON includes per-sample end position, target, target distance, tolerance and
`reached`, the solver, root and followed joints, `limits` and `withinLimits`,
plus `dryRun` and `written`. A clamped result is successful but can have
`reached: false`; automation should inspect it. Dry-run validates the source,
solve and destination without writing. Existing output requires `--overwrite`.
The CLI and GUI apply the same `SolveTwoBoneIk` or `SolveChainIk` document edit.

## Remaining animation work

[Persistent IK, aim, follow and foot-lock constraints](MODEL_ANIMATION_CONSTRAINTS.md)
keep a solve live as the clip changes. Dedicated target and pole drag
handles in the viewport remain planned (the 3D cursor places them today); joint
limits apply to chain solves only, as Euler bounds rather than swing-twist
cones. Physical
keyboard/screen-reader acceptance, real-project animation quality, maximum-size
performance, engine playback and cross-platform release checks remain open.
The numeric controls, native results table and full detail field provide
keyboard-accessible equivalents for the current workflow. No new dependency,
cloud service or AI connector is required.
