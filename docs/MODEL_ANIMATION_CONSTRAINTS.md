# Clip constraints and layers

**Status: Partial.** Persistent constraints and animation layers on skeletal
clips are available in the Mesh Editor and the command line, evaluated by one
shared clip evaluation stack. VibeStudio is pre-alpha and largely untested in
real projects; none of this has been checked against production rigs.

## How the stack works

Without a stack a clip is its dense samples. Once a clip has a constraint or a
layer, it keeps its **base** samples (the motion before the stack) and its
frames become their evaluation:

    frames = constraints(layers(base))

Layers apply first, in order, then constraints, in order, each over every
sample. The editable source keeps the base, the layers and the constraints
(mesh version 15), and validation checks that the stored frames equal the
stack's evaluation, as stored curves are checked against samples, so every
exporter (md5anim, IQM, the baked MDL, MD2 and MD3 frames) writes the
evaluated motion.

Edits that change motion work on the base and evaluate again:
[animation curves](MODEL_ANIMATION_CURVES.md) (stored curves describe the
base), key edits, range transfers, [inverse kinematics](MODEL_IK.md) and
[pose paste](MODEL_RIG.md#copy-paste-and-mirror-poses). So editing a base key
moves a hand that an IK constraint holds, and the hand stays on its target.
Direct pose edits (transform, copy, interpolate), resizing the clip, moving
bind joints and adding, reparenting or removing joints are refused until the
clip's stack is baked; renaming a joint renames constraint and layer
references.

## Constraints

| Kind | Effect on each sample in its range |
| --- | --- |
| Inverse kinematics | FABRIK from the chain root (or two bones up) to the joint, reaching the fixed point or the target joint's pose applied to the point as an offset, steered by the pole; unreachable targets are clamped. |
| Aim | Turns the joint so its chosen axis (±X, ±Y, ±Z) points at the target, optionally twisted so the next axis leans towards the pole direction. |
| Child of | The joint follows the target joint, keeping the offset it had at the range's first sample. |
| Copy transform | The joint takes the target joint's model-space pose. |
| Foot lock | Inverse kinematics holding the joint where it is at the range's first sample. |

Each constraint has a sample range (the last sample can follow the clip's end),
a weight from 0 to 1 that blends the moved joints' parent-local poses from the
unconstrained to the constrained pose, and can be disabled. Optional **weight
keys** (sample:value pairs from 0 to 1, linear between them, such as `0:0,
12:1`) scale that weight over the clip, so a constraint can blend in and out;
where they reach zero the sample is left as it was. Inverse kinematics and foot
lock constraints can **keep joints within their limits**: every chain joint is
clamped to its [joint limits](MODEL_IK.md#limit-joint-rotation) (Euler or
swing-twist) after each solver pass, as a chain solve with **Apply joint
limits** does. The constrained
joint's descendants follow it. A constraint that would follow a joint it moves
itself (the joint, its descendants, or its IK chain) is refused: that is a
dependency cycle.

## Layers

A layer keys parent-local channels of some joints with
[curves](MODEL_ANIMATION_CURVES.md). An additive layer adds its translation and
Euler degrees and multiplies its scale; an override layer replaces the keyed
joints' channels. Its weight blends it in (weight 0 leaves the base), scaled
sample by sample by optional weight keys from 0 to 1, keyed and interpolated
like any other curve.

## Use it

Open **Skeleton** > **Constraints and Layers…**, choose the clip, describe a
constraint (**Kind**, **Joint**, **Chain root**, **Target joint** or a
**Point**, **Pole**, **Aim axis**, **First sample**, **Last sample**,
**Weight**, **Weight keys**, **Keep joints within their limits**) and choose
**Add Constraint**. Select a listed constraint to load it, then **Update
Selected** or **Remove Selected**.

Layers are keyed in the same dialog. Select a listed layer to load it as the
draft, or start a new one: choose a **Keyed joint** and a **Layer channel**
(the nine joint channels, or **Layer weight**), then set and remove keys with
**Key**, **Set Key** and **Remove Key** or in the curve graph, which works as
in **Animation Curves** (drag a key, double-click to add one, Delete to remove
it). A joint's first key starts it neutral: zero offsets and unit scale on an
additive layer, or the base pose's own values at every sample on an override
layer, so keying one channel changes nothing else. **Add Layer** adds the draft
with its **Layer name**, mode, weight and enabled state; **Update Layer**
applies it to the selected layer; **Remove Layer** removes it. **Bake Stack**
keeps the evaluated samples and drops the stack (and the curves of joints the
stack changed). Every button that changes the clip is one undo step; keys stay
a draft until then.

```sh
vibestudio --cli model constraints models/imp.mesh.json --operation add --clip 0 --kind foot-lock --joint lfoot --first 4 --last 12 --output models/planted.mesh.json --json
vibestudio --cli model constraints models/imp.mesh.json --operation add --clip 2 --kind aim --joint head --target prop --axis x --pole 0,0,1 --output models/look.mesh.json
vibestudio --cli model constraints models/look.mesh.json --operation add-layer --clip 2 --layer breathe.json --mode additive --weight 0.5 --name breathe --output models/breathing.mesh.json
vibestudio --cli model constraints models/breathing.mesh.json --operation bake --clip 2 --output models/baked.mesh.json
```

`list` (the default) reports each clip's constraints and layers. `add` needs
`--clip`, `--kind ik|aim|child-of|copy-transform|foot-lock` and `--joint`, with
optional `--root-joint`, `--target`, `--point x,y,z`, `--pole x,y,z`,
`--no-pole`, `--axis x|y|z|-x|-y|-z`, `--first`, `--last N|end`, `--weight`,
`--weight-keys "0:0,12:1"` (`-` clears them), `--limits yes|no` (inverse
kinematics and foot lock only), `--name` and `--disabled`; `update --index N`
changes only the given settings; `remove --index N`; `add-layer --layer
file.json` (curve groups as `model curves` writes them) with `--mode
additive|override`, `--weight`, `--weight-keys`, `--name`; `update-layer` and
`remove-layer --index N`; `bake`. Edits need `--output .mesh.json`, with
`--dry-run` and `--overwrite`. Listings report each entry's `weightKeys` as
text and each constraint's `limits`.

## Limits

- Layers blend rotations through X-then-Y-then-Z Euler offsets, so a layer
  cannot key a joint with [quaternion rotation keys](MODEL_ANIMATION_CURVES.md#use-quaternion-rotation-keys).
- Override layers start from the base pose, not from the layers below them.
- Constraint weight keys are typed as sample:value pairs (linear); layer
  weight keys are keyed in the graph and can use any curve interpolation.
- Stacked clips refuse direct pose edits and hierarchy changes until baked.
