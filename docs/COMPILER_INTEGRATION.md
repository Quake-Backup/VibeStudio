# Compiler Integration

SlopStudio imports level compiler and node-builder sources as Git submodules.
The first integration step is orchestration: build or locate compiler
executables, run them through a structured wrapper, capture diagnostics, and
feed outputs back into the project/package graph.

## Prepared Quake-family Workspaces

`build prepare` / **Prepare Build Workspace** serialize the current map and copy
the complete package reader, including draft overrides, into a new
`game/id1`, `game/baseq2` or `game/baseq3` directory. `build-inputs.json` records relative paths, byte counts
and SHA-256 hashes. The service defaults to 4 GiB, caps requests at 1 TiB and
100,000 asset files, and refuses existing/protected destinations, unsafe names,
links, collisions and root PAK/PK3/DPK mounts. Dry preparation reads/hashes without
writing a workspace. GUI work is asynchronous; cancellation discards private
copies before the publication boundary. Blocking filesystem calls and map
serialization remain cooperative-cancellation latency limits.
Windows retries a transient final directory-rename failure with five bounded
delays totalling 310 ms, rechecking cancellation, links and destination absence.
Other platforms retain a single Qt directory rename. Fixture tests hold a
Windows directory handle and verify release, destination-collision and
cancellation behavior without touching installed content.

Quake and Quake II use `quake-full`, `quake-fast` or `quake-bsp-only`.
The target is recorded in schema-2 input manifests (existing Quake III schema-1
manifests remain supported). SlopMap2 stages receive `-nodefaultpaths`,
`-path <assets>` and a captured-map-specific `-logfile`. QBSP receives the maps
folder as `-wadpath`; Quake II adds `-q2bsp`. Extra arguments are restricted to
supported scalar options with checked argument counts; bare positional numbers
and missing values are rejected. External definitions, conversion, target/path
overrides and response files cannot bypass capture. The bounded shared texture service
reads WAD2 from package bytes and builds a deterministic compiler WAD; the private
Quake map references that WAD. WAL decoding and animation traversal reuse the
image/dependency services. See the Level Editor limits and format boundaries.
Artifact validation recognizes that SlopMap2 VIS/LIGHT preserve Quake II/Qbism
formats without QBSP target flags, while retaining Quake III mismatch warnings
and rejecting unknown IBSP/QBSP versions. Planning warnings about not-yet-produced
BSP/PRT inputs may still be retained after a successful pipeline; they remain
available in package review's bounded **Details** view.

For Quake III, `build run-prepared` verifies those inputs and runs `quake3-full` or
`quake3-bsp-only` through the normal runner. Each stage receives `-game quake3`,
`-fs_basepath <workspace>/game`, `-fs_homepath <workspace>/home`, `-fs_game baseq3`
and `-fs_basegame baseq3`. User filesystem/game overrides and `-lightmapdir`,
`-tempname`, `-rename` output redirects are rejected. Stage manifests live in
`<workspace>/manifests`; runtime outputs stay in the captured asset tree.
A post-run verification failure prevents successful output registration in the
studio. The inventory is a reproducibility check, not an authentication signature
or a lock against external processes. Compiler output and manifest files remain reviewable on
failure. See [Level Editor](LEVEL_EDITOR.md#prepared-builds-with-current-assets)
for supported output companions and remaining layout/deployment limitations.

`build-outputs.json` records a run UUID, pipeline, captured-input fingerprint,
state and bounded output inventory. Standard BSP, generated map shaders and
numbered external TGA lightmaps are runtime outputs; compiler companions such
as portal/leak files stay diagnostic. A full BSP run retains previous outputs
under `history/<new-run-id>/` outside search paths before execution. There is no
automatic history pruning. Incremental runs that disable BSP require a previous
verified successful record. A workspace lock excludes simultaneous studio build
and publication writes; failed/cancelled/changed outputs cannot publish.

`build artifacts` / **Publish Prepared Build** verify current inputs and outputs.
`build publish-prepared` uses the existing deterministic PAK/PK3 writer, per-chunk
input identity checks and atomic backup publication. The GUI binds its review
to the receipt hash; CLI can do so with `--expected-output-sha256`. Captured assets
and runtime outputs publish together, diagnostics stay in the workspace, and
`--include-source` optionally includes the map and generated Quake texture WAD. **Deploy Prepared Build** and
`build deploy-plan` / `build deploy-prepared` reuse that publisher for complete
installation PAKs/PK3s, binding both the output receipt and existing destination hash.
Read-only permission is per operation. Build and Launch reviews deployment after
successful compilation; optional launch follows verified publication and uses
the matching Quake-family profile with explicit base/game paths and windowed
mode (Quake III also pins home lookup). Quake/Quake II use the shared numbered
PAK planner and map-slot receipts; CLI `--pak-slot` and
`--expected-deployment-sha256` expose the same review as the GUI. The existing
single-map deploy path remains for ordinary saved-file builds.
Classic IBSP v46 validation accepts its standard
17-lump header independently of the extended Quake Live v47 header.

The optional generated-assets proofs run without a game or input control:

For the Quake/Quake II proof, add
`--recorder builddir/src/level_classic_deployment_smoke_test` to
`level_build_engines_compiler_workflow.py` (use `.exe` on Windows) to verify
numbered installation PAK deployment and launch ordering with a recorder.

```sh
python src/tests/level_build_engines_compiler_workflow.py --binary builddir/src/slopstudio --qbsp /path/to/slopmap2-bsp --vis /path/to/slopmap2-vis --light /path/to/slopmap2-light --output-root .agents/tmp/quake-build-proof
python src/tests/level_build_workspace_compiler_workflow.py --binary builddir/src/slopstudio --compiler /path/to/slopmap3 --output-root .agents/tmp/prepared-build-proof
python src/tests/level_build_artifacts_compiler_workflow.py --binary builddir/src/slopstudio --compiler /path/to/slopmap3 --output-root .agents/tmp/build-output-proof
python src/tests/level_build_deployment_compiler_workflow.py --binary builddir/src/slopstudio --compiler /path/to/slopmap3 --recorder builddir/src/level_build_deployment_smoke_test --output-root .agents/tmp/build-deployment-proof
```

The dated results below predate the move to SlopMap2 and SlopMap3: they ran
stock ericw-tools and NetRadiant Custom q3map2. The first two commands above
have since passed with SlopMap2 and SlopMap3 (see [Status](#status)); the
others have not been rerun yet.

The artifacts proof reuses the generated model/draft fixture, adds lightmapped
materials, and checks external lightmaps plus generated shaders against BSP
references and PK3 payloads. It verifies overwrite backups, altered-output and
stale-review rejection, then a clean rebuild back to internal lightmaps without
shipping stale outputs. Windows q3map2 evidence from 2026-10-05 is retained in
`.agents/tmp/level-build-artifacts/compiler-proof/` (15 steps plus the 9-step
baseline fixture); native game rendering and other platforms remain unverified.

The Quake-family proof passed 25 Windows steps on 2026-10-05 using installed
ericw-tools 2.0.0-alpha8. It independently verifies embedded Quake miptex pixels,
Quake II texture names, published `.lit`/`.lux`, MD2/PCX and sound bytes, PAK
determinism/backups and stale-input rejection. Final evidence is in
`.agents/tmp/level-build-engines/compiler proof final/`. The same round reran
all 24 q3map2 artifact-proof steps. The installed `qbsp -help` separately reports
`number is too big` partway through option formatting; compile execution succeeds.

## Imported Sources

Numeric rotation verifies texture alignment through both the source service and
the compiler output. On 2026-10-04 an original synthetic room with a brush
rotated 31.75° passed the installed q3map2 BSP/VIS/LIGHT pipeline. Required Valve
220 conversion covered all 42 classic faces. All 24 checked BSP vertices retained
their original UV mapping within `3.55e-7` texture repeats, allowing equivalent
whole-repeat offsets. Dependency export and the resulting level PK3 passed
payload validation. Commands, compiler logs, source/assets and the report are
under `.agents/tmp/level-rotation/compiler/`. No game was launched. This is one
compiler acceptance fixture; other compilers and native platforms still need
verification before general compatibility claims.

| Tool | Local path | Main role |
|---|---|---|
| SlopMap2 | `external/compilers/slopmap2` | SlopStudio's Quake/idTech2 compilers, derived from ericw-tools: `slopmap2-bsp`, `slopmap2-vis`, `slopmap2-light`, the `slopmap2-bspinfo`, `slopmap2-bsputil` and `slopmap2-maputil` utilities, and the `slopmap2-hub` GUI. |
| SlopMap3 | `external/compilers/slopmap3` (compiler source in `tools/quake3/q3map2`) | SlopStudio's Quake III/idTech3 compiler, continuing q3map2 from NetRadiant Custom: BSP compile, VIS, light, conversion and packaging through `slopmap3`, plus the `slopmap3-workbench` GUI. |
| ZDBSP | `external/compilers/zdbsp` | Doom-family node building, including GL and extended node formats. |
| ZokumBSP | `external/compilers/zokumbsp` | Doom-family node, blockmap, and reject building with vanilla-focused output. |

Initialize imports:
```sh
git submodule update --init --recursive
```

## SlopStudio Compilers

SlopStudio's Quake, Quake II and Quake III compilers are SlopMap2 and
SlopMap3, not stock ericw-tools and NetRadiant Custom q3map2. Both are forks developed as part of
the SlopStudio project, so compiler fixes, diagnostics and features can land
where the studio needs them instead of only being worked around in the wrapper.
They are the explicit, documented forks that `AGENTS.md` asks for when
SlopStudio changes a compiler.

| Compiler | Fork | Branch | Pinned revision | Derived from | Licence |
|---|---|---|---|---|---|
| SlopMap2 | [themuffinator/VibeyMapTools](https://github.com/themuffinator/VibeyMapTools) | `main` | `4495049a9e4c1f6deadae3a76b8256614840af35` | [ericw-tools](https://github.com/ericwa/ericw-tools) | GPL-3.0 (`COPYING`) |
| SlopMap3 | [themuffinator/q3mapx](https://github.com/themuffinator/q3mapx) | `main` | `897524439cb58d2b736bc96b16c231d22dc5ddce` | q3map2 from [NetRadiant Custom](https://github.com/Garux/netradiant-custom), revision `8216133` | GPL-3.0-or-later; imported q3map2 files keep their GPL-2.0-or-later notices |

The two repositories still carry their earlier names, VibeyMapTools and q3mapx.
`.gitmodules` uses relative URLs (`../VibeyMapTools.git`, `../q3mapx.git`) that
resolve beside the SlopStudio remote you cloned from, and GitHub redirects the
old URLs when a repository is renamed.

The compilers were first renamed VibeMap2 and VibeMap3, after the studio's
former name, VibeStudio. SlopStudio now calls them SlopMap2 and SlopMap3, but
the forks themselves have not taken the new names yet: their `vibemap2-rebrand`
and `vibemap3-rebrand` branches build `vibemap2-*` and `vibemap3` executables
that print VibeMap2 and VibeMap3 banners, and the pinned revisions are the
forks' published `main` commits from before that, which build the VibeyMapTools
(`vmt-*`) and q3mapx executables. Discovery and the version probe accept all
three sets of names. The pins move once the forks publish their renames, so a
clone never points at a commit GitHub does not have.

### Executables And Build Layouts

| Compiler | Executables | Build output | Version line |
|---|---|---|---|
| SlopMap2 | `slopmap2-bsp`, `slopmap2-vis`, `slopmap2-light`, `slopmap2-bspinfo`, `slopmap2-bsputil`, `slopmap2-maputil`, `slopmap2-hub` once the fork is renamed; `vibemap2-*` and `vmt-*` before that (the build and lighting preview hub that succeeds ericw-tools' lightpreview) | `build/src/<tool directory>/`, inside `Release/` for multi-configuration generators; sources are in `src/qbsp`, `src/vis`, `src/light`, `src/bspinfo`, `src/bsputil`, `src/common`, `src/include` and `src/hub`. An install copies every executable flat into the prefix, and the release archives (`slopmap2-windows-<version>.zip`, `slopmap2-linux-<version>.tar.gz`, `slopmap2-macos-<version>.tar.gz`) hold every tool flat at the archive root. | Each tool's first line is `---- slopmap2-<tool> / SlopMap2 <version> ----`, for example `---- slopmap2-bsp / SlopMap2 <version> ----`. `-help` exits 0; `--version` is not supported (it exits 1 after the banner), so the registry probe runs the tool with no arguments. |
| SlopMap3 | `slopmap3` (command line) and `slopmap3-workbench` (GUI) once the fork is renamed; `vibemap3` or `q3mapx` before that | `build/<preset>/bin` for the `release`, `cli`, `cpu-only` and `debug` CMake presets; an install writes `<prefix>/bin`. | `slopmap3 --version` prints `SlopMap3 <version> (NRC <revision>)`; `-help` prints the same line after the inherited q3map2 version line. |

### Discovery

The registry looks for SlopMap2 and SlopMap3 under their current names and
their earlier names:
- SlopMap2: `slopmap2-<tool>`, `vibemap2-<tool>` or `vmt-<tool>` in
  `external/compilers/slopmap2/build/src/<tool directory>/`, its `Release/`
  subfolder and `external/compilers/slopmap2/install/`, and on PATH.
- SlopMap3: `slopmap3`, `vibemap3` or `q3mapx` in `external/compilers/slopmap3/build/release/bin`,
  `build/cli/bin`, `build/cpu-only/bin` and `install/bin`, and on PATH.

Stock `qbsp`, `vis`, `light` and `q3map2` executables are no longer found
automatically. To use one, choose **Locate…** on the Build page's
**Toolchain** tab or run `compiler set-path`, for example
`compiler set-path slopmap2-bsp --executable /opt/ericw-tools/bin/qbsp`. The
version probe still recognises the ericw-tools, q3map2, VibeyMapTools, q3mapx,
VibeMap2 and VibeMap3 banners for executables configured this way.

### Renamed Ids

Tool, profile and integration ids changed with the move. Old tool and profile
ids are refused with a message that names the new one; pipeline ids (`quake-full`,
`quake-fast`, `quake-bsp-only`, `quake3-full`, `quake3-bsp-only`) and stage ids
(`qbsp`, `vis`, `light`, `bsp`) are unchanged. The `vibemap2-*` and `vibemap3*`
ids from before SlopStudio's own rename are not refused: every id lookup maps
them to the `slopmap2-*` and `slopmap3*` ids silently.

| Old id | New id |
|---|---|
| `ericw-qbsp`, `ericw-vis`, `ericw-light` | `slopmap2-bsp`, `slopmap2-vis`, `slopmap2-light` |
| `ericw-bspinfo`, `ericw-bsputil` | `slopmap2-bspinfo`, `slopmap2-bsputil` |
| `ericw-bsputil-check`, `ericw-bsputil-extract-entities`, `ericw-bsputil-extract-textures` | `slopmap2-bsputil-check`, `slopmap2-bsputil-extract-entities`, `slopmap2-bsputil-extract-textures` |
| `ericw-lightpreview` | `slopmap2-hub` |
| `q3map2` (tool) | `slopmap3` |
| `q3map2-probe`, `q3map2-bsp`, `q3map2-vis`, `q3map2-light`, `q3map2-convert`, `q3map2-pk3` | `slopmap3-probe`, `slopmap3-bsp`, `slopmap3-vis`, `slopmap3-light`, `slopmap3-convert`, `slopmap3-pk3` |
| `ericw-tools`, `q3map2-nrc` (integrations) | `slopmap2`, `slopmap3` |

Game installations default to the `slopmap2` compiler label for Quake and
Quake II and `slopmap3` for Quake III; installations saved with the old
`ericw-tools` and `q3map2` labels migrate automatically.

### Status

**Partial.** Profiles, discovery, pipelines and prepared builds target
SlopMap2 and SlopMap3. On 2026-10-08 two generated-assets proofs passed on
Windows with builds of the rename commits (SlopMap2 `ec52db30`, SlopMap3
`e2a00b3`): the Quake/Quake II prepared-build proof, 43 steps through
`slopmap2-bsp`, `slopmap2-vis` and `slopmap2-light` including the deployment
recorder, and the Quake III prepared-workspace proof, 9 steps through SlopMap3's
BSP, VIS and LIGHT stages into a verified PK3. Their evidence is in
`.agents/tmp/slopmap2-engines-proof/` and `.agents/tmp/slopmap3-workspace-proof/`.
The other dated compiler proofs in this document and in
[Level Editor](LEVEL_EDITOR.md) ran stock ericw-tools 2.0.0-alpha8 and
NetRadiant Custom q3map2, and the [MD3 instance appearance contract](#md3-instance-appearance-contract)
was verified against NetRadiant Custom q3map2; those still need rerunning with
SlopMap2 and SlopMap3, as do other platforms. On 2026-10-09 the known-issue
catalogue and Quake map preflight were reconciled against the pinned SlopMap2
3.1.0 audit and changelog: 15 resolved/inapplicable reports were retired,
3 became informational guidance, and the other 107 were retained with reasons.
The [reconciliation matrix](plans/ericw-tools-remaining-bugs-resolution.md)
accounts for every original issue ID. This source review does not replace
native compiler or visual acceptance; SlopStudio remains pre-alpha and largely
untested in real projects.

## Integration Model
1. Discover compiler availability from bundled builds, user paths, source-port toolchains, and project-local overrides.
2. Normalize compiler profiles by engine family, map format, source file, target game, output package, and quality preset.
3. Generate a command manifest before every run.
4. Execute compilers in a task sandbox with captured stdout/stderr, exit code, duration, environment, and file outputs.
5. Parse diagnostics into clickable editor markers where possible.
6. Register produced BSP/WAD/node/lightmap/assets with the package manager.

Current implementation:
- `src/core/compiler_registry.*` defines descriptors for SlopMap2
  `slopmap2-bsp`, `slopmap2-vis`, `slopmap2-light`, `slopmap2-bspinfo`,
  `slopmap2-bsputil` and `slopmap2-hub`, SlopMap3 `slopmap3`, ZDBSP, and
  ZokumBSP.
- Discovery checks imported source directories, known build-output locations
  (see [Discovery](#discovery)), optional extra search paths, user-configured
  executable overrides, project-local executable overrides, and PATH. The
  registry records static capability flags for SlopMap2, Doom node builders,
  and SlopMap3, and can run short version/help probes when listing tools.
  `slopmap2-hub` is discovered presence-only and is never probed, because it is
  a GUI.
- GUI inspector details and CLI `--compiler-registry` expose source/executable
  availability without running external tools.
- `src/core/compiler_profiles.*` defines fifteen wrapper profiles across the
  four toolchains (see [Wrapper Profiles](#wrapper-profiles)). The planner
  produces reviewable command plans with program, arguments, working directory,
  expected output path, further required outputs, optional sibling outputs,
  warnings, and readiness.
- Compiler command manifests are schema-versioned JSON documents (currently
  `CompilerCommandManifest::kSchemaVersion` 4) generated from command plans and
  runs. They include command line, working directory, environment subset,
  inputs, expected and registered outputs, an `expectedOutputKnown` flag for
  stages whose destination cannot be predicted, optional output paths, file
  hashes, duration, exit code, stdout/stderr, parsed diagnostics, known-issue
  warnings, preflight warnings, combined warnings, errors, and structured
  task-log entries. They can be saved and loaded through the shared core
  service.
- `src/core/compiler_known_issues.*` defines the high-value known issue
  catalog for the SlopMap2 profiles used by compiler plans. Its entries come
  from the ericw-tools issue tracker and keep those upstream issue numbers.
  Resolved/inapplicable reports are retired against the SlopMap2 3.1.0 audit
  (see [Status](#status)); retained reports are not proof of a reproduced fork defect.
  Warnings are scoped by profile/tool, include the issue IDs and suggested
  local actions, and are also kept in the manifest `knownIssueWarnings` field.
  After a run the catalogue is matched against the run's diagnostic lines only,
  never the whole output: the tools print catalogue words on every healthy run
  (`clipnodes` and `marksurfaces` in qbsp's lump table, `skip faces` and
  `built-in palette` from light). A match is context for a warning or error the
  compiler already printed, so it is recorded as a note in `knownIssueNotes`
  and never changes how the run ended.
- `src/core/quake_map_preflight.*` performs conservative Quake `.map`
  preflight checks for SlopMap2 profiles. It reports path privacy, long
  values, escape sequences, external-map/prefab hazards, light-group conflicts,
  region risks, brush-entity origin keys, non-integer brush coordinates, Phong
  risks and `_sunlight2` sampling warnings. Fixed light-style, Half-Life
  light-range, forced-luxel-scale and region culling/areaportal reports no
  longer produce preflight warnings. Compiler plans and manifests keep these
  separately in
  `preflightWarnings` while preserving the combined warning list.
- `src/core/compiler_runner.*` executes compiler profiles through `QProcess`,
  streams stdout/stderr line by line while the process runs, parses diagnostics
  incrementally, links them to file paths and line numbers where present,
  supports a time limit (zero or less means none) and cancellation callbacks,
  closes the tool's input so a prompt cannot hang a run, surfaces plan/preflight warnings
  before launch, refuses missing working directories, applies isolated `TMP`,
  `TEMP`, and `TMPDIR` paths to compiler processes, detects leaks, validates
  produced artifacts, saves manifests, and registers produced outputs.
- `src/core/build_pipeline.*` chains those single-profile wrappers into
  multi-stage builds and turns a finished build into a reviewable game launch
  command line. See [Chained Build Pipelines](#chained-build-pipelines).
- CLI `compiler profiles`, `compiler plan`, `compiler manifest`,
  `compiler run`, `compiler rerun`, `compiler copy-command`,
  `compiler set-path`, and `compiler clear-path` expose the same services used
  by the GUI. `compiler run` can save a manifest and register outputs into a
  project manifest with `--register-output`, stream task-log entries with
  `--watch`, and add machine-readable task-state JSON with `--task-state`.
  CLI `build list`, `build plan`, `build run`, `launch plan`, `launch run`, and
  `bsp inspect` cover pipelines, launching, and compiled-artifact inspection.
- The Qt Widgets shell shows a pipeline graphic, per-profile readiness, run and
  cancellation actions, activity-center summaries/logs, and copy actions for a
  CLI equivalent and JSON manifest. The Build surface adds a pipeline chooser,
  a stage list, and Run / Cancel / Copy Commands actions; pipelines run on a
  worker thread and report stage start/finish and every log line back into the
  activity center. The pipeline input follows the map open in Levels, and a
  finished run lists its parsed diagnostics in a **Problems** tab: a diagnostic
  whose line (a bare `WARNING: <line>:` included, attributed to the stage's
  `.map` input) falls inside a brush, patch, or entity of the open map selects
  that object, and one in another text file opens the Code editor at the line.
  The **Toolchain** tab lists every registry tool with its resolved executable
  and where the path came from; **Locate…** and **Use Automatic** write and
  remove the same user override as `compiler set-path` and `compiler
  clear-path`, and a project manifest override still wins while its project is
  open. Launch Game copies the built map into the game folder first when the
  engine loads maps only from there (`planGameMapDeploy()` and
  `deployGameMap()` in `build_pipeline.*`), after asking once whether the
  installation may be written to, and **Build and Launch** (F5) chains the build
  and that launch.
- `scripts/validate_credits.py` and CLI `credits validate` compare imported
  compiler pins across `src/core/studio_manifest.cpp`, `.gitmodules`,
  `README.md`, [`docs/CREDITS.md`](CREDITS.md), and the checked-out submodule
  revisions.

## Wrapper Profiles

ZDBSP and ZokumBSP WAD outputs now pass shared node validation before output
registration. Missing or malformed node records fail the run even if the tool
exits zero. ZDBSP `-m`/`--map` selectors and ZokumBSP positional map lists limit
validation to those maps; otherwise all groups are inspected. Extended and
compressed native/GL records are supported, including GL data in SSECTORS.
UDMF, DeePBSP and separate GL caches warn that validation is unavailable.
Validation is cancellable and uses the same service as Map Health and Doom
launch plans. See [node readiness](LEVEL_EDITOR.md#doom-node-readiness).

A profile is the wrapper layer's unit of work: one tool, one stage, one command
shape. `CompilerProfileDescriptor` in `src/core/compiler_profiles.h` carries the
stage token, the input extensions, how the tool accepts an output path, where it
writes by default, which sibling files it produces or needs, and its named
argument presets.

| Profile id | Tool | Stage | Input | Output behavior |
|---|---|---|---|---|
| `slopmap2-bsp` | SlopMap2 `slopmap2-bsp` | `qbsp` | `.map` | Trailing positional destination; defaults to `<input base>.bsp` |
| `slopmap2-vis` | SlopMap2 `slopmap2-vis` | `vis` | `.bsp` | Rewrites its input in place; requires a sibling `.prt` |
| `slopmap2-light` | SlopMap2 `slopmap2-light` | `light` | `.bsp` | Rewrites its input in place; `-lit` adds a required `<base>.lit` |
| `slopmap2-bspinfo` | SlopMap2 `slopmap2-bspinfo` | `inspect` | `.bsp` | Writes `<base>.bsp.json` beside the BSP |
| `slopmap2-bsputil-check` | SlopMap2 `slopmap2-bsputil` | `inspect` | `.bsp` | `--check`; console output only, no artifact |
| `slopmap2-bsputil-extract-entities` | SlopMap2 `slopmap2-bsputil` | `extract` | `.bsp` | `--extract-entities`; writes a sibling `.ent` |
| `slopmap2-bsputil-extract-textures` | SlopMap2 `slopmap2-bsputil` | `extract` | `.bsp` | `--extract-textures`; writes a sibling `.wad` |
| `zdbsp-nodes` | ZDBSP | `nodes` | `.wad` | `-o <path>` before the input; without it ZDBSP writes `tmp.wad` into the working directory |
| `zokumbsp-nodes` | ZokumBSP | `nodes` | `.wad` | `-o <path>` after the input; without it the input is rewritten in place |
| `slopmap3-probe` | SlopMap3 | `probe` | none | `-help`; console output only, no artifact |
| `slopmap3-bsp` | SlopMap3 | `bsp` | `.map` | No stage token (BSP is the fall-through stage); default arguments are `-meta`; writes `<input base>.bsp` with `.prt`, `.srf`, and `.lin` as optional siblings |
| `slopmap3-vis` | SlopMap3 | `vis` | `.bsp` | `-vis`, in place; requires a sibling `.prt` |
| `slopmap3-light` | SlopMap3 | `light` | `.bsp` | `-light`, in place |
| `slopmap3-convert` | SlopMap3 | `convert` | `.bsp` or `.map` | `-convert`; destination depends on `-format`, so it is treated as unknown |
| `slopmap3-pk3` | SlopMap3 | `package` | `.bsp` | `-pk3`; writes into the engine path, so the destination is treated as unknown |

### SlopMap3 Stage Dispatch
SlopMap3 keeps q3map2's dispatch: it parses its general options first, then
dispatches on the *front* of what is left. `tools/quake3/q3map2/main.cpp` uses
`args.takeFront("-vis")`, `takeFront("-light")`, `takeFront("-pk3")`,
`takeFront("-convert")` and so on, and falls through to `BSPMain` when nothing
matches. A stage token pushed behind user-supplied extras is therefore not a
stage token at all - SlopMap3 silently runs a BSP compile instead.

The planner handles this with `CompilerProfileDescriptor::leadingStageArgument`,
which `buildCompilerCommandPlan()` always emits as argument 0, before the
profile's default arguments and before any caller extras. `slopmap3-bsp` carries
no leading token on purpose, because `BSPMain` is the fall-through case. The
same mechanism carries `bsputil`'s `--check`, `--extract-entities`, and
`--extract-textures` sub-commands.

### Output Paths
`CompilerOutputArgumentStyle` records how a tool accepts a destination, and
`CompilerDefaultOutputMode` records where it writes when SlopStudio does not
supply one:
- `Positional` - the path is a trailing argument. `slopmap2-bsp` accepts
  `sourcefile.map [destfile.bsp]`, as ericw-tools `qbsp` did.
- `Flag` - the path follows a flag. Both Doom node builders take `-o`, but the
  ordering differs: ZDBSP parses options before the input, so SlopStudio emits
  `-o <output> <input>`, while ZokumBSP reads `-o` after the input file and its
  level list, so SlopStudio emits `<input> -o <output>`. This is what
  `outputArgumentAfterInput` selects.
- `None` - the tool has no destination argument and rewrites its input
  (`slopmap2-vis`/`slopmap2-light`, SlopMap3 `-vis`/`-light`). Requesting an output path
  for such a profile produces a plan warning rather than a silently ignored
  argument.
- Defaults: `DerivedFromInput`, `InPlace`, `WorkingDirectoryFile` (ZDBSP's
  `tmp.wad`, which also raises a plan warning), `Unknown` (SlopMap3 `-convert` and
  `-pk3`, which clears `expectedOutputKnown` so artifact validation skips the
  file), and `NoArtifact` (`bsputil --check`, `slopmap3 -help`).

A derived output that collapses onto the input path is rejected and downgraded
to "unknown", so validation can never pass by inspecting an untouched input.

### Named Argument Presets
`CompilerArgumentPreset` gives each profile a small catalog of reviewed,
translatable switches instead of free-form strings: an id, a display name, a
description, the literal arguments, and - for switches that take a value - a
`requiresValue` flag with a placeholder the caller fills in.
- `slopmap2-bsp`: `bsp2`, `hlbsp`, `q2bsp`, `qbism`, `hexen2`, `notex`,
  `leaktest`, and `wadpath` (takes a directory).
- `slopmap2-vis`: `level4`, `fast`.
- `slopmap2-light`: `extra4`, `bounce`, `lit`, `soft`.
- SlopMap3 (shared by the BSP, vis, light, convert, and pk3 profiles): `meta`,
  `fast`, `fs-basepath` and `fs-game` (take a directory and a mod name),
  `threads` (takes a count), and `verbose`.

Presets are resolved through `compilerArgumentPresetsForProfile()` and
`compilerArgumentPresetForId()` and are covered by
`src/tests/compiler_profiles_smoke_test.cpp`. They are a core-layer catalog
today: neither the CLI nor the shell has a preset picker yet, so callers still
pass the resolved arguments as extras.

## Diagnostics And Leak Detection

### Live Output Streaming
`runCompilerCommand()` polls the process on a 100 ms cadence, drains both
channels, and splits them into complete lines. Each line becomes a task-log
entry as it arrives - `stderr` lines are prefixed `[stderr]` - so `--watch`, the
activity center, and the pipeline log all show progress while the compiler is
still running rather than after it exits. Any trailing partial line is flushed
once the process finishes. Cancellation and the timeout are checked on the same
loop and kill the process. A timeout of zero or less (`CompilerRunRequest::timeoutMs`,
`BuildPipelineRequest::stageTimeoutMs`) means no limit; runs started from the
window use that, because they can be cancelled and a full vis or light pass can
take hours.

SlopMap2 redraws its progress with a carriage return and no line feed
(`[ 42%]  est: 00:00:03\r`, `src/common/log.cc`), so a message printed
mid-phase arrives behind every snapshot drawn before it. A line is logged and
parsed as a terminal would show it: only the text after its last carriage
return. The captured stdout and stderr keep the bytes as they came.

### Diagnostic Parsing
`CompilerDiagnosticParser` consumes one line at a time and keeps per-channel
state, which is what makes the two-line error form work:
- **Fatal-error banners.** SlopMap2 (`src/common/log.cc`) and SlopMap3
  (`tools/quake3/common/inout.cpp`), like their upstreams, print a `*** ERROR ***`-style banner
  and put the actual message on the *next* line. A line matching the banner is
  held, and the following line is emitted as a single error diagnostic whose
  `rawLine` contains both. A banner with nothing after it is still flushed as an
  error when the run ends.
- **SlopMap2 source locations.** SlopMap2, like ericw-tools, reports positions as
  `<source>[line N]` (from the `parser_source_location` formatter in
  `src/include/common/parser.hh`), not as `path:N`. That form is matched first, and
  the file name is only taken when it carries a known content extension.
- **Bare line numbers.** Many qbsp warnings are `WARNING: <line>: message` with
  no file name; the line number is still captured.
- **Generic fallback.** Otherwise a `path[:line[:column]]` pattern is tried,
  restricted to a fixed extension list so ordinary prose is not mistaken for a
  path. The extension has to end the name, so `C:/Users/john.clark/maps/e1m1.map:120`,
  `mathlib.cpp:44` and `textures/base.wall/metal.tga` are read whole instead of
  stopping at `.c` or `.wal`.
- **Status lines are not diagnostics.** `0 errors`, `no warnings`, and
  `Error count: 0` are recognised as summaries and ignored, so a clean compile
  does not report itself as failing.

Diagnostics carry their originating channel (`stdout` or `stderr`), and are
folded into the manifest's `errors` or `warnings` list by level.

### Leak Detection
A leaked Quake compile still exits 0, so the exit code cannot be trusted.
`detectLeak()` runs for qbsp-stage profiles and reports a leak when either
signal appears:
- a `.pts` leak point file exists among the run's optional outputs (`slopmap2-bsp` writes
  `<bsp>.pts` and `<bsp>.leak.prt` from `src/qbsp/outside.cc`; both, plus
  `<bsp>.prt`, are declared as related outputs of the `slopmap2-bsp` profile), or
- the captured output contains `Reached occupant "<classname>" at (<x y z>)`,
  which gives the entity and position that escaped into the void.

The resulting warning names the entity, the position, and the leak point file to
load in the editor. Independently, `inspectCompiledMapArtifacts()` in
`src/core/bsp_inspect.*` scans the output folder for `.pts`, `.lin`, and
`.leak*` files beside a BSP, parses their coordinate triples into a leak line
with bounds, and warns that visibility and lighting results cannot be trusted.
CLI `bsp inspect` exposes that report. The Build page lists a leak first among
its problems, and a leaking build of the map open in Levels draws the point
file over it as a trail (`MapViewport::setLeakTrail()`); `map render --leak`
draws the same trail into the headless SVG.

### The `.prt` Requirement For Visibility
Both `slopmap2-vis` and `slopmap3-vis` declare `.prt` in
`requiredCompanionInputExtensions`, because `vis.cc` and `vis.cpp` load
`<bsp base>.prt` written by the preceding BSP stage. When that file is missing,
the plan warns before anything is launched and names the classic cause: the
BSP stage found a leak, so the portal file was never kept and visibility cannot run.

## Chained Build Pipelines
`src/core/build_pipeline.*` composes profiles into the loop a mapper actually
runs. Every stage still goes through the same runner, so logs, diagnostics,
hashes, and manifests are identical to a single-profile run.

| Pipeline id | Engine | Stages (default state) |
|---|---|---|
| `quake-full` | idTech2 | `slopmap2-bsp` -> `slopmap2-vis` -> `slopmap2-light` |
| `quake-fast` | idTech2 | `slopmap2-bsp` -> `slopmap2-vis` (optional, off by default) -> `slopmap2-light` |
| `quake-bsp-only` | idTech2 | `slopmap2-bsp` |
| `quake3-full` | idTech3 | `slopmap3-bsp` -> `slopmap3-vis` -> `slopmap3-light` |
| `quake3-bsp-only` | idTech3 | `slopmap3-bsp` |
| `doom-zdbsp` | idTech1 | `zdbsp-nodes` |
| `doom-zokumbsp` | idTech1 | `zokumbsp-nodes` |

Behavior:
- Each stage names the earlier stage whose expected output feeds it, so the BSP
  a stage writes is the BSP the next stage reads.
- The pipeline's explicit output path belongs to the last surviving stage that
  can actually be told where to write; later in-place stages operate on that
  artifact. If no stage accepts a destination, the requested path is ignored
  with a warning rather than silently dropped.
- Stages can be skipped per run (`--disable-stage`), or are skipped because they
  are optional and off by default, or because their profile is not registered in
  this build. Skips are reported with a reason.
- `planBuildPipeline()` resolves every stage, input, and output without running
  anything; `runBuildPipeline()` reuses that resolution.
- With `stopOnFailure` (true by default, and not yet switchable from the CLI), a
  failed stage marks the remaining stages as skipped with the reason recorded;
  cancellation does the same.
- On a dry run, a chained stage whose input does not exist yet - because the
  stage that would have produced it did not actually run - has that
  missing-input error downgraded to a warning, so planning a full chain does not
  report its later stages as broken.
- With a manifest directory, each stage writes
  `<pipeline id>.<stage id>.json` into it.
- Per-stage extra arguments are passed as `--stage-args <stageId>=<args>`.

Project pipelines: a project manifest's `buildPipelines` array keeps pipelines
of the project's own (`projectBuildPipelines()`, `setProjectBuildPipelines()`),
edited in **Build Pipelines…** and with `build pipeline-set` and
`build pipeline-remove`. Each is a name, variables and an ordered list of
compiler profile stages, each with an id, a typed argument line, an on/off
switch and an input (`map` or `previous`):

```json
"buildPipelines": [{
  "id": "fast-light", "name": "Fast light", "variables": {"QUALITY": "-extra"},
  "stages": [
    {"id": "qbsp", "name": "QBSP", "profile": "slopmap2-bsp", "arguments": "", "enabled": true, "input": "map"},
    {"id": "vis", "name": "VIS", "profile": "slopmap2-vis", "arguments": "", "enabled": false, "input": "previous"},
    {"id": "light", "name": "LIGHT", "profile": "slopmap2-light", "arguments": "${QUALITY} -threads ${CPU_COUNT}", "enabled": true, "input": "previous"}
  ]
}]
```

The pipeline's engine, input and output types come from its first stage, and
every stage must be a compiler of that engine. Argument lines are split as a
shell would and grouped from switch to switch; `${MAP}`, `${MAP_DIR}`,
`${MAP_NAME}`, `${OUTPUT}`, `${PROJECT_DIR}`, `${GAME_DIR}`, `${MOD}`,
`${CPU_COUNT}` and the pipeline's own variables are filled in per word, a group
with an empty variable is left out with a warning, and an unknown variable
fails the plan so a run starts nothing. `BuildPipelineRequest::projectPipelines`
puts them before the built-in ones and `BuildPipelineRequest::variables` sets
values for one run. The key is optional within manifest schema 2, so an older
reader keeps it as an unknown key.

Program trust (`src/core/build_program_trust.*`): a project folder or a saved
run can come from someone else, and both can name a program (a project's
`compilerToolOverrides` and `compilerSearchPaths`, discovery from the project
root, or a manifest's `program`). Before **Run Pipeline**, **Run Profile**,
**Re-run Last Compile**, `build run` and `compiler rerun` start anything,
`reviewBuildPrograms()` checks every program against `knownCompilerPrograms()`,
discovered from the user's own configuration only (StudioSettings compiler
paths, search paths typed on the command line, the studio's compiler folders
from the current folder, and PATH) and matched by the engine family of the
stage's or the saved run's profile. A discovered compiler inside the project
(or map folder) does not count. Anything else needs the user's trust: the GUI
shows each program's full command line and folder with **Cancel** as the
default, and the CLI exits 4 with the same text and JSON `programTrust` unless
`--trust-programs` is given. Trust is recorded per user in StudioSettings
(`trustedBuildPrograms()`: the project or map folder, the program path and its
SHA-256), never in the project, so a changed program is asked about again.

`gameLaunchProfiles()` then turns a finished build into a launch command line
from argument templates using `{map}`, `{mod}`, `{basedir}`, and `{bsp}`, with
profiles for Quake, Quake II, Quake III, and Doom source ports plus a `custom`
profile that adds nothing of its own. `buildGameLaunchPlan()` only produces a
reviewable plan; `startGameLaunch()` refuses to run one that is not marked
runnable. `GameLaunchRequest::userProfiles` carries the user's own profiles
(`StudioSettings::userGameLaunchProfiles()`, edited in **Launch Profiles…** and
with `launch profile-set`), looked up before the built-in ones: their typed
argument line is split as a shell would and grouped from each switch to the
next (`gameLaunchArgumentGroups()`), and a group whose token has no value is
dropped, silently for `{mod}`. `extraArguments` come last; the Build page and
`launch plan` fill them from the installation's saved extra arguments
(`StudioSettings::launchExtraArguments()`).

Repeating builds: Levels' **Run Profile** and the Toolchain's **Run Profile**
keep each run's command manifest in `.slopstudio/compiler-runs`
(`compilerRunManifestDirectory()`, `newCompilerRunManifestPath()`).
`findNewestCompilerRunManifest()` finds the newest run of a map there, which
**Re-run Last Compile** and `compiler rerun --last` replay through
`rerunCompilerCommandManifest()`, saving the replay beside it. **Run Last
Build** instead re-plans what the project ran last
(`StudioSettings::lastBuildRun()`: a pipeline, with or without its launch, or a
Levels profile) against the open map.

## CLI Examples

```sh
slopstudio --cli compiler set-path slopmap2-bsp --executable C:\tools\slopmap2\slopmap2-bsp.exe
slopstudio --cli compiler plan slopmap2-bsp --input maps/start.map --workspace-root E:\Projects\QuakeMod
slopstudio --cli compiler run slopmap2-bsp --input maps/start.map --workspace-root E:\Projects\QuakeMod --manifest build/qbsp-run.json --register-output --watch
slopstudio --cli compiler rerun build/qbsp-run.json --manifest build/qbsp-rerun.json
slopstudio --cli compiler rerun --last maps/start.map --workspace-root E:\Projects\QuakeMod
slopstudio --cli compiler copy-command build/qbsp-run.json
slopstudio --cli compiler plan slopmap3-light --input maps/q3dm1.bsp --workspace-root E:\Projects\Q3Mod
slopstudio --cli build list --json
slopstudio --cli build plan quake-full --input maps/start.map --json
slopstudio --cli build run quake-full --input maps/start.map --manifest build/manifests --stage-args light=-extra4 --watch
slopstudio --cli build run quake-fast --input maps/start.map --disable-stage light
slopstudio --cli bsp inspect build/start.bsp --json
slopstudio --cli launch plan --map start --json
slopstudio --cli launch run --bsp maps/start.bsp --deploy --allow-test-maps
slopstudio --cli launch profile-set user-dev --name "Developer" --arguments "+set fs_game {mod} +set developer 1 +devmap {map}"
slopstudio --cli launch extra-args --arguments "+set developer 1"
```

## License Boundary
External compilers are kept as submodules and treated as separate tools until a
specific source-level merge is reviewed. This holds for SlopStudio's own
SlopMap2 and SlopMap3 too: they are separate executables in their own
repositories, never linked into SlopStudio. The imported tools use GPL-2.0-era
licensing (ZDBSP, ZokumBSP, and the q3map2 files SlopMap3 keeps with their
GPL-2.0-or-later notices), GPL-3.0 (SlopMap2, and SlopMap3's own code), mixed
GPL/LGPL/BSD file licensing inherited from NetRadiant Custom, or
dependency-specific terms.

Rules:
- Keep upstream license files in place, including the ericw-tools and
  NetRadiant Custom notices inside SlopMap2 and SlopMap3.
- Credit SlopMap2, SlopMap3 and their upstreams (ericw-tools, NetRadiant
  Custom) in `README.md` and `docs/CREDITS.md`.
- Prefer process execution over static linking for the first integration.
- Document every SlopStudio fork's URL, branch, revision, and reason here, as
  [SlopStudio Compilers](#slopstudio-compilers) does for SlopMap2 and SlopMap3.
- If compiler code is copied or modified in-tree, preserve headers and add nearby comments for derived code.
- Release licence bundles carry the compiler licences under
  `licenses/external/compilers/SlopMap2/COPYING` and
  `licenses/external/compilers/SlopMap3/COPYING`.

## Planned Build Strategy
- Keep SlopStudio's main build independent from compiler builds by default.
- Add optional Meson features only after each tool's native build requirements are documented.
- Prefer a `tools/compiler-build` helper that can produce platform-specific tool bundles in CI.
- Store built tools in release artifacts with their corresponding source revision and license bundle.

## Engine Mapping
- idTech1 Doom-family maps: WAD map lumps, UDMF/TextMap variants, and the
  ZDBSP and ZokumBSP node/blockmap/reject profiles. Both node builders take
  their destination behind `-o`, on opposite sides of the input path, and both
  have a single-stage pipeline (`doom-zdbsp`, `doom-zokumbsp`).
- idTech2 Quake-family maps: `.map` to BSP through SlopMap2 `slopmap2-bsp`,
  `slopmap2-vis`, and `slopmap2-light`, chained by the `quake-full`,
  `quake-fast`, and `quake-bsp-only` pipelines, with `slopmap2-bspinfo` and
  `slopmap2-bsputil` available as inspection and extraction profiles.
- idTech3 Quake III-family maps: `.map` to BSP through SlopMap3, SlopStudio's
  continuation of q3map2 from NetRadiant Custom, including shader-aware light
  and packaging flows. The BSP, `-vis`, `-light`, `-convert`, and `-pk3` stages each
  have their own profile, and `quake3-full` / `quake3-bsp-only` chain them.

## Diagnostics Contract
Compiler wrappers should emit:
- Start/end timestamps.
- Working directory.
- Full command line with redacted secrets if any are ever introduced.
- Source files and package mounts.
- Output files and hashes.
- Parsed warnings/errors with source locations where available.
- Re-run recipe.

## SlopMap2 Known-Issue Mitigation Model
The catalogue keeps ericw-tools issue numbers for traceability, but its current
baseline is SlopMap2 3.1.0 at `4495049a9e4c1f6deadae3a76b8256614840af35`.
The [2026-10-09 reconciliation](plans/ericw-tools-remaining-bugs-resolution.md)
records evidence and a disposition for all 125 original IDs. It retires fixed
reports, including Hub workspace isolation (#463), embedded light settings
(#167/#483), style preservation (#173/#475), bounce debug (#400/#416),
saturation (#401), region culling/areaportals (#422/#444), GoldSrc sun/spot
support (#437), texture lookup (#451), Half-Life light ranges (#470), forced
luxel scale (#485), and the inapplicable generic-binary warning (#335).

Platform constraints (#480/#482) and partially resolved lightgrid optimisation
(#425) are informational help-only notes. The sampling guidance for #484 and
the design decisions for #456/#464 remain explicit. Other retained reports
have insufficient issue-specific fix evidence; they are conservative risks
or authoring/integration guidance, not confirmed SlopMap2 defects. General
compiler hardening does not establish every historical map-specific case fixed.

Coverage tests require each original ID to be retained or explicitly retired,
with no duplicates or overlap. Plan and preflight tests check that fixed cases
stay quiet and unresolved inputs still warn. Actual compiler diagnostics and
artifact validation remain independent of catalogue matching. GUI, CLI,
manifests, prepared builds and package review share those services.

The catalogue is source-baseline-aware, not dynamically version-gated. A
user-selected older/custom executable does not automatically restore historical
warnings. Native compiler/version acceptance remains a separate task.

Wrapper and preflight mitigations SlopStudio owns in the current implementation:
- Run compiler processes with isolated temporary directories and register only
  expected outputs, mitigating temp-file and overwrite risks in the wrapper
  layer. `slopmap2-hub` (the successor of ericw-tools' lightpreview) is
  discoverable as an optional helper, but SlopStudio does not launch it; its
  native launch and OpenGL/Qt behavior stay with SlopMap2 until SlopStudio adds
  a smoke-tested preview workflow.
- Generate command manifests with resolved executable path, command line,
  inputs, outputs, hashes, warnings, and diagnostics. SlopMap2 3.1.0 also offers
  opt-in `-embedsettings` for a path-sanitised effective lighting manifest in
  `LIGHTING_SETTINGS`; SlopStudio does not enable it automatically. Wrapper
  manifests remain useful for the whole pipeline and non-BSP-writing modes.
  Registry version/help probes remain discovery data rather than manifest fields.
- Sanitize or warn about absolute WAD paths, long entity values, escape
  sequences, dotted filenames, and hardcoded asset roots before invoking the
  compiler, covering the wrapper side of #87, #201, #230, #245, #288, #293,
  #450.
- Validate light entities for mismatched `START_OFF` flags, risky Phong/bmodel
  settings and unsupported sun/surface-light combinations, covering the
  product-facing side of #122, #310, #351, #377 and #405. Authored switchable
  styles are preserved as `oldstyle` in SlopMap2; the generated runtime style
  is not guaranteed to equal the authored value.
- Detect risky region and prefab compile setups, including multiple region
  brushes, rotating origin brushes in region builds, external-map
  missing classnames, grouped external maps, and map-relative path ambiguity,
  covering #193, #194, #199, #207, #231, #327, #333, #390 and #417 as
  diagnostics or workflow constraints.
- Catalog helper-tool risks for `bspinfo` and `bsputil`, expose first-class
  helper descriptors, and smoke-test the core registry path so packaging,
  argument-parsing and dependency constraints can become clear setup
  diagnostics. Runnable wrapper profiles now exist for `bspinfo` and for
  `bsputil --check`, `--extract-entities`, and `--extract-textures`. The
  sub-command is emitted as argument 0, ahead of any caller extras, because
  `bsputil` parses its options before the single positional BSP path; that keeps
  SlopStudio's own command shape stable. The historical #435 argument-parsing
  report remains unverified because the fork audit does not explicitly resolve
  that operation; it is not presented as a reproduced SlopMap2 failure.
  Registry discovery itself is still presence and help/version output only, and
  operation-level `bspinfo`/`bsputil` probes remain without smoke-test coverage.

Planned or diagnostic-only mitigations that are not compiler fixes:
- Post-compile BSP/BSPX validation now checks missing outputs, wrong BSP
  family, truncated/corrupt headers, lump bounds, selected face-reference
  risks, conversion-output mismatches, and missing profile-requested metadata.
  It should continue to expand for deeper semantic checks. These checks may
  block promotion or packaging, but they do not repair compiler output.
- Visual regression fixtures should track lighting, shadow, VIS, and debug
  output changes by SlopMap2 version, but wrapper checks cannot guarantee
  visual parity.

Compiler-side items the wrapper should not claim to resolve:
- Compiler output correctness regressions such as lighting artifacts, BSPX
  lump generation bugs, VIS behavior changes, corrupt BSP output, and geometry
  compile bugs. SlopStudio can flag known affected versions and maintain
  regression fixtures, but fixes belong in SlopMap2.
- Remaining compiler feature requests such as `func_viscluster`,
  `func_detail_null`, embedded lightmaps, custom hull sizes,
  conditional entities, model shadow casting, translucent lighting, and new
  image-format support. SlopStudio can expose options once SlopMap2 releases
  and documents them.
- Native compiler build, packaging, logging, or launcher changes. These belong
  in the SlopMap2 repository, recorded under
  [SlopStudio Compilers](#slopstudio-compilers).

## MD3 Instance Appearance Contract

Levels and dependency review follow the pinned NRC q3map2 MD3 material contract
through `core/level_model_appearance`: implicit importer default skins, derived
entity skin filenames, omitted surfaces and ordered suffix remaps. The contract
and the evidence below come from NetRadiant Custom q3map2, before SlopStudio
moved to SlopMap3; they have not been rechecked with SlopMap3 yet. See
[Placed Model Appearances](LEVEL_MODEL_APPEARANCE.md) for supported input grammar
and the studio's stricter failures instead of compiler fallback/truncation.

The bundled Assimp `MD3Loader.cpp` at NRC `68ecbed` validates `configFrameID`
but leaves `pcVertices` at frame zero. A real local NRC `449778b` compiler with
its installed Assimp runtime reproduced that mismatch. Nonzero frame requests
therefore fail appearance/dependency review. A separate Assembly pose bake was
verified against BSP vertex positions; original animated sources remain intact.
The optional `level_model_appearance_compiler_workflow.py` exercises ten synthetic
cases. Compiler executable/runtime identities and exact commands are retained
under `.agents/tmp/modeller-rc/evidence/level-model-appearance/`. Other compiler
variants, patched frame support and native macOS/Linux acceptance remain open.
