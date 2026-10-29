# Model Outputs and Receipts

Status: **Partial**. Exports and staged models from the Mesh Editor, `model build`
and `model lod` leave receipts; status checks, refresh of exports and detail
levels, and checking a player package's embedded receipt work on synthetic
fixtures. Bakes, collision fragments, `animation.cfg` and player packages made by
the studio do not record receipts yet, and nothing here has been tried on a real
project. VibeStudio is pre-alpha.

An editable source (`.mesh.json`, `.assembly.json`) reaches the game as derived
files: an MD3 or MDL export, a staged package entry, a set of detail levels, a
player package. When the source changes, those files go stale, and shipping a
stale MD3 beside a newer source is easy to miss. VibeStudio keeps a receipt for
each committed output so it can tell which are current and refresh them with
the settings they were made with.

## What a receipt holds

`core/model_outputs.h` defines `ModelOutputRecord`:

- `kind`: `export`, `stage`, `lod-set`, or another service's own kind
  (`pose-bake`, `animation-bake`, `collision-map`, `q3-animation`,
  `player-package`);
- the source and its SHA-256, and any other inputs (parts, skins) with theirs;
- the recipe: the settings that made it (`format`, `frame`, `clip`,
  `md5FrameRate`, `strict`, `target`, and for detail levels `levels` and `ratio`);
- every output, a file or a package entry, with its SHA-256;
- the studio version and the time.

An export made from unsaved edits records no source hash: it cannot be judged
against the saved file, and its status is **Unknown** until the source is saved
and the output refreshed.

## Where receipts are kept

Receipts live in the project manifest, beside the delivery targets
([Model Delivery Targets](MODEL_TARGETS.md)), under the top-level key
`modelOutputs`. Paths inside the project are relative to its root, so the
receipts move with the project:

```json
"modelOutputs": [
  {"kind": "export",
   "source": {"path": "models/crate.mesh.json", "sha256": "…"},
   "recipe": {"format": "md3", "frame": 0, "clip": -1, "md5FrameRate": 0, "strict": false, "target": "ioquake3"},
   "outputs": [{"path": "build/models/crate.md3", "sha256": "…"}],
   "tool": "0.9.0", "deliveredUtc": "2026-10-29T10:00:00Z"}
]
```

A staged output names its package and entry instead of a path. A newer receipt
for the same first output replaces the older one; at most 256 are kept and the
oldest go first. Game folders and packages never get receipts written into them,
except the receipt a Quake III player package already embeds
(`vibestudio/player-bundle.json`). Without an open project no receipt is kept.

`recordModelOutput(projectRoot, record)` is the shared hook: every guarded
export calls it after its write commits, so a refused or cancelled export leaves
no receipt. Other services (bakes, player packages, weapon sets) add receipts
the same way, with their own `kind` and inputs.

## Status

`modelOutputStatus` checks the outputs first, then the inputs:

| Status | Meaning |
| --- | --- |
| **Current** | Every input and output still has its recorded bytes. |
| **Source changed** | The source or another input changed since: refresh the output. |
| **Changed elsewhere** | An output was rewritten by someone else; it is never refreshed over. |
| **Missing** | An output or input is gone. |
| **Unknown** | Exported from unsaved edits, or a file or package cannot be read. |

**Source changed** and **Missing** are stale. Status chips always say the
state in words; the icon only repeats it.

## Refresh

`refreshModelOutput` re-runs an `export` or `lod-set` receipt from the saved
source with its recorded settings. Every output must still have its recorded
bytes, or be missing; one changed elsewhere is left alone and the refresh says
so. All outputs are prepared and their destinations reviewed before any is
written, through the same guarded writer as exports. A dry run writes nothing,
and cancelling leaves every output as it was. Staged entries are refreshed by
staging the source again.

## Player package receipts

`readModelPackageReceipt` reads the receipt a Quake III player package embeds:
each packaged file with its SHA-256 (a file that no longer matches is
**Changed elsewhere**), and, given the `.assembly.json` that made it, whether each
part's source still has the SHA-256 the package was built from
(**Source changed** when not).

## In the studio

- The Mesh Editor's **Export** page lists **Outputs**: what this source has
  delivered, each with its status. **Check Outputs** compares them again on a
  worker; **Refresh Stale** exports the stale ones from the saved source.
  Exports and **Stage in Package** add receipts when a project is open; a staged
  entry reads **Staged; save the package** until the package is saved.
- The **Package path** starts as the game path the source was imported from
  (for example `progs/player.mdl`) when that is a path a package can hold.
- A release of the project reports each stale output as a `stale-model` problem
  (advisory, like `stale-map`).
- The Models page lists the project's editable sources, and marks one saved
  after a receipt was delivered as **(outputs out of date)**: a file-time hint
  (`modelSourceSavedAfterOutputs`), while **Check Outputs** compares content.
  Choosing **Mesh Editor** on a game model finds its source from the receipts
  first.

## Command line

```sh
vibestudio --cli model outputs ./models/crate.mesh.json --json
vibestudio --cli model outputs --project ./mymod --refresh stale --dry-run --json
vibestudio --cli model outputs --project ./mymod --refresh ./build/models/crate.md3
vibestudio --cli model outputs --project ./mymod --forget ./build/models/crate.md3
vibestudio --cli model outputs --receipt ./build/crate.pk3 --assembly ./models/crate.assembly.json --json
```

`model outputs` lists the receipts of the project (`--project`, or the project
holding the source) with their status and exits 4 when any is stale or changed
elsewhere, like `asset index status`. `--refresh OUTPUT|stale` refreshes one
receipt or every stale one and records the new receipts; `--dry-run` writes
nothing. `--forget OUTPUT` drops a receipt. `--receipt PACKAGE` checks a player
package's embedded receipt, with `--assembly` its parts' sources. `model build`
and `model lod` record receipts when the source is in a project.

## Limits

- Bakes, collision fragments, `animation.cfg` exports and player packages made
  by the studio do not record receipts yet; the embedded player receipt can be
  checked, and `recordModelOutput` is the hook those services call.
- The Mesh Editor's detail-level export and an MD3 staged with its stored detail
  levels record no receipt yet; `model lod` records the whole set.
- The Models page does not yet badge game models whose source is newer.
- Without an open project (or `--project`), nothing is recorded.
