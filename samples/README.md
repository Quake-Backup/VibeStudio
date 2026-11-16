# SlopStudio Samples

The `*-minimal` sample projects are intentionally tiny, license-clean workspaces
for CI and manual smoke testing. They contain placeholder text assets only.
No sample includes commercial game data, SDK assets or proprietary content.

Each minimal sample includes:
- `.slopstudio/project.json` for the project manifest service.
- `maps/` with a tiny placeholder map or build input.
- `packages/` with folder-package content for read-only package browsing.
- `out/` and `.slopstudio/cache/` folders so health checks can exercise output
  and temporary paths without creating files.

Run the smoke checks with a built SlopStudio CLI:

```sh
python scripts/validate_samples.py --binary builddir/src/slopstudio
```

[Hingeworks](projects/quake2-hingeworks/README.md) is a larger Quake II rerelease
(Q2RE) deathmatch map authoring experiment, currently an **unreleased Alpha 5 development build** (Alpha 4 is the latest approved release). It keeps editable map sources,
the design reference, authoring script and release records in a persistent
SlopStudio project. Its textures, sounds and music require a separately owned
Quake II rerelease installation. It is not part of the installation-free minimal smoke
checks; human multiplayer balance testing remains outstanding.

[Panic Protocol](projects/quake2-panic-protocol/README.md) is a three-level
Quake II rerelease single-player campaign with co-op support, currently
**Alpha 2**. It follows a mostly linear mission to shut down a Strogg datacenter,
with optional objectives, secrets and revisitable levels. Its project includes
the reviewed design PDF, editable floor plans, map sources, build tools and
native visual comparisons. Alpha 2 refines the architecture, machinery,
materials, lighting and landmarks while preserving the campaign route, and
replaces thin sign text with solid lettering.
Stock assets require a separately owned installation;
see its validation record for runtime and multiplayer testing limits.

Standalone model designs are in [`models/`](models/):
[`pillar.model.json`](models/pillar.model.json) exercises the original schema,
and [`angled-panel.model.json`](models/angled-panel.model.json) demonstrates
schema-2 3D rotations, UV tiling, mirroring, and rotation. Open these in
**Models > Design Prop** or build them with `model build`. The material paths
are placeholders; see [Model Design](../docs/MODEL_DESIGN.md) for the workflow.
