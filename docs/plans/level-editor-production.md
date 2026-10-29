# Level editor production gates

Objective: a complete, production-grade level editor for idTech1, idTech2
and idTech3, including the applicable authoring functionality of TrenchBroom
and NetRadiant Custom. Passing an individual test does not complete this goal.
VibeStudio remains pre-alpha and largely untested in real projects.

Latest verified increment: [Parity wave 5](#2026-10-29-parity-wave-5).

## Reference baseline

Audit started 2026-10-18 against the
[TrenchBroom 2026.2 manual](https://trenchbroom.github.io/manual/latest/)
and [NetRadiant Custom](https://github.com/Garux/netradiant-custom), including
the local reference at `449778b3840f8f2062daab15c87cb012f2ac945c`.
The inventory below groups workflows; completing it requires a command-level
audit of both editors and recorded evidence for each applicable command.
The [generated command catalogue](level-editor-commands.md) now enumerates the
central registrations and in-tree plugin providers at TB `v2026.2`
(`4c756be1a54a28d921d18c77716a5f170bdc31d0`) and the NRC revision above.
It contains 596 entries, and every entry now has a bounded review: 502 Partial,
57 Planned and 37 Not applicable, with no Available entries. The additional
gesture, panel and workflow inventory layers still require review.
New upstream releases need a separate delta review. Behaviour references and
any borrowed implementation belong in the repository credits.

## Authoring parity

| Area | Existing foundation | Required work and acceptance |
| --- | --- | --- |
| Clipper | Shared movable two/three-point plane, NRC dominant-view/parallel/outer-face camera-plane policy, fixed-anchor Shift/Alt point dragging with relative grid and Ctrl surface picking, camera face placement/matching, numeric XYZ, checked wire preview, atomic front/back/both, whole-side removal, quick clipping, persistent options, optional one-point/no-change wrap/merge and standalone brush/component wrapping with a material-aware source/result preview | Wider scene snapping, native first-point drag parity, textured cut previews, native gestures and dense-map performance |
| Components | Solved topology, shared 2D/3D vertex/edge/face handles, checked multi-object move/rotate/scale/shear with numeric and direct controls, shared pivot/grid/UV lock, edge/face splitting, direct grid-point and numeric insertion/removal, exact vertex quick snapping, collapse and labelled movement traces | Complete native gesture audit, broader selection continuity and collision/degeneracy tests, native profile acceptance and dense-map performance |
| Selection | Object/face selection, spatial queries, scene locks and document-owned temporary Hide/Show All/Isolate with profile history and structural identity tracking; shared component replace/add/subtract/toggle, geometry clumping, depth cycling, profile lasso/wheel policy, All/None/Invert, complete/touching conversion and topology Grow/Shrink/Connected | Full container visibility inheritance, component hiding, filter history, broader visible-picking and native 2D/3D/profile acceptance |
| Brush modelling | Primitive shapes, transforms, CSG merge/intersect/carve/hollow, plane and split extrusion with face texture lock; exact component corner UV lock; straight/arc/S-bend sweep with shared viewport cap controls, numeric preview, grid-aware scaling, taper and affine repetition | Main-camera textured sweep previews, complete sweep/extrusion profile gestures, bevel and arbitrary convex creation; broader precision, topology-changing UV policies and compiler acceptance |
| Groups and layers | Persistent nested scene nodes, visibility, locks, linked copies with object correspondence and protected entity properties, closed-group picking, linked-family extraction, nested editing contexts and hierarchy-preserving duplicate/arrays across GUI/CLI | Nested linked instances, wider transform/conflict behaviour, reliable cross-editor metadata interchange and native profile acceptance |
| Prefabs | Selection export, preview, insertion/replacement, namespaced targets, package assets and version 2 nested group transfer | Linked prefab updates, browse/search workflows, missing dependency review and consistent project/package handoffs |
| Materials | Face/patch selection, alignment, paint/sample and surface clipboard | Direct UV manipulation, projection/lock semantics during topology changes, shader visibility and native profile acceptance |
| Patches | Primitives, control points, rows/columns, caps, stitch, thickness and shared 3D component transforms | Complete patch transform/UV workflows, multi-seam constraints and compiler validation |
| Entities | Definitions, properties, links and static models | Command-level creation/connection/selection parity, smart property editors, group transforms and asset preview consistency |
| Navigation | Four views, layouts, profiles, bookmarks, filters, regions and atomic object repeat with captured world transforms, TB keys and CLI recipes; profile-aware clipboard placement in plans, perspective and orbit views | Complete manipulator/pivot workflows, additional repeat applicability reviews, responsive large-map editing, native keyboard and pointer acceptance |
| Production loop | Save/recovery, undo, validation, compiler and package services | Representative authored maps through edit/save/reopen/build/package/test, external-change and failed-write recovery, repeatable diagnostics |

## Completion gates

- [ ] Record every relevant TrenchBroom and NRC command, its VibeStudio
  equivalent, status (Available, Partial or Planned), test and any limitation.
- [ ] Close all authoring gaps above through shared services with atomic undo,
  locked-object protection, deterministic persistence and meaningful diagnostics.
- [ ] Provide GUI and documented CLI routes for new authoring operations.
- [ ] Preserve materials, flags, entities, scene membership, prefab links and
  asset paths through topology changes; verify compiler and package handoffs.
- [ ] Measure representative complex maps and fix editing stalls; long work
  must expose progress, cancellation and stale-result rejection.
- [ ] Verify scaling, high-visibility themes, RTL, translation expansion,
  accessible state/focus and keyboard-only alternatives for new controls.
- [ ] Run native Windows, macOS and Linux checks and record real-project
  acceptance. Direct widget/service tests do not establish physical input or
  screen-reader acceptance. Input control requires the user's scoped permission.
- [ ] Update manual, generated offline guide, CLI help, credits and changelog
  with each delivered increment; keep release claims consistent with evidence.

## Requested work (2026-10-29)

Queued at the user's request after the current parity waves. All three are
Planned.

- [ ] **Filters tab** (`filters-outliner-tab`): a Blender Outliner-style tree
  on the Levels sidebars for selecting and filtering the map: layers, groups
  and linked-group families (instances under one node), object kinds, entity
  classes, tool textures (clip, hint, skip, caulk and the rest) and filter
  tags, with visible and selectable toggle columns and counts. It becomes the
  one home for kind, class and tag filters; the View tab's Filters section
  moves into it, and the Outliner stays the structural editor, sharing the
  same visibility and lock state. One documented precedence covers regions,
  hidden layers and groups, filters and temporary Hide/Isolate, and rows say
  why something is hidden.
- [ ] **Fixable issues and brush cleanup** (`map-inspector-issue-fixes`,
  after `bobtoolz-brush-utilities`): Health issues gain kinds, affected
  objects and quick fixes (per row, per kind and for the selection, with a
  preview and one undo step each). It adds detection for duplicate or
  redundant planes, slivers, micro and duplicate brushes, off-grid vertices,
  broken texture axes, out-of-bounds and mixed-contents brushes, and empty
  entities and groups, after TrenchBroom's validators. Cleanup services are
  shared by the GUI and a `map repair` CLI; ignored issues persist per map.
- [ ] **Camera-view editing** (`camera-direct-editing`, after the object
  gizmos, drag gestures, Escape cascade and size overlays): audit
  TrenchBroom's 3D workflow, then close its gaps so a map can be built
  entirely in the camera: move on the hit plane with Alt for vertical,
  duplicate-drag, face resize with live sizes, surface-grid drawing and
  snapping, clip points on surfaces, vertex/edge/face handles, entity and
  prefab drops onto surfaces, and texture apply and copy gestures. These are
  TrenchBroom defaults in its profile and available to every profile through
  data-driven controls.

## Component drag behaviour audit

This is a bounded audit of TB `v2026.2` component movement, reviewed against the
[credited controllers](../CREDITS.md#component-drag-constraints). It does not
complete the command-level inventory of either TB or NRC. NRC's manipulation
and texture conventions still need their own detailed evidence.

| Reference behaviour | VibeStudio route | Status / remaining acceptance |
| --- | --- | --- |
| Perspective horizontal movement; live Alt vertical | Shared `LevelComponentDrag`, TB camera profile | Available; core and owned camera checks pass |
| Orthographic coordinate plane; Alt ignored | Same controller, all three plans | Available; three-plane checks pass |
| Relative movement grid; vertex-only Ctrl/Cmd absolute grid | Shared movement and insertion, independently configurable per view | Available; off-grid and hidden-coordinate checks pass on Windows; macOS acceptance open |
| Live Shift chooses the dominant accepted movement axis | Key press/release routing and validated-preview anchors | Available; ties, refused previews and vertical transitions tested |
| Shift insertion can also use Ctrl/Alt; initial Shift remains free | Existing insertion controller shares movement policy | Available; combined plan chords, existing camera insertion and pure tracker checks pass |
| Ctrl click toggles; Ctrl vertex drag moves | Deferred handle press and shared preview transaction | Available; selection and exact undo checked in owned views |
| Drag an unselected geometric clump; preserve other picks only if the whole clump is selected | Existing topology keys feed native drag selection | Partial; multiselection checked, broader overlap/native acceptance remains |
| Ctrl edge/face click selects; movement stays relative | Component-mode initial chord policy | Available; semantic chord and plan-mode checks pass |
| Alt+Shift quick-snap click to another vertex | One geometric brush-vertex source, exact target, configurable click, keyboard target selector and shared CLI transform | Available; owned plan/camera, merge, undo and compiler checks pass; physical-input acceptance remains open |
| Movement trace with labelled axis segments | Shared X/Y/Z distance renderer for movement and insertion, constrained-axis emphasis and pending dashes | Available; owned 100%/200% high-contrast renders and bounded-label checks pass; native-platform acceptance remains open |
| Native 2D/3D click clumping and all modifier combinations | Existing projected-stack selection plus world-geometry drag clumps | Partial; differences and physical-input acceptance remain |
| Complete native rendered previews and topology/UV semantics | Shared hull transaction, current material/UV policies | Partial; textured previews and topology-changing UV work remain |

## Implementation record

### Topology insertion and removal — Available, 2026-10-18

Solved brush vertices can be inserted and removed through the shared convex
hull service; removing an edge or face removes its incident vertices. Flat/open
results and unrepresentable new points leave the draft unchanged. The component
dialog has numeric insertion, removal and selection-aware undo. The Levels
Geometry menu and Tools tab remove picked components across brushes atomically;
Delete Selection acts on components while their mode is active. CLI
`map insert-vertices` and `map remove-components` use the same geometry and
guarded document persistence. Stored texture parameters survive; deformation
texture lock remains required work. Interactive edge/face splitting is recorded below.

Evidence from Windows, clang-cl 20.1.7 / Qt 6.10.1, warnings as errors:

- `level-brush-smoke`: four face dialects, inserted/removed topology, all box
  edges, valid/invalid face removal, source comments/flags/mapping, exact undo,
  explicit collapse, multi-brush atomicity, scene locks and strict CLI options.
- `brush-editor-ui-smoke`: dialog and shell command routing, selection history,
  protected parent objects, 100% high-contrast light and 200% high-contrast dark
  with RTL and expanded labels. Owned widget renders inspected; no input injection.
- `level-brush-tools-smoke`, `level-scene-locks-smoke`, `level-prefab-smoke` and
  `compiler-known-issues-smoke`: related service regressions passed.
- `src/tests/level_topology_compiler_workflow.py`: 27 CLI/compiler/package steps
  passed with VibeMap3 0.4.0. Classic, Valve 220 and brushDef maps each retain
  eight authored corners, ten faces and six inherited materials after insertion
  and removal; all corners appear in the compiled BSP. Dependency PK3s validate
  and contain byte-identical map, BSP and texture payloads. No game was launched.
- Documentation site, offline guide generation, credits, changelog and Qt
  translation extraction checks passed. Native macOS/Linux, physical input,
  screen readers and real-project acceptance remain outstanding.

Evidence lives under `.agents/tmp/level-production-tools/`, including
`compiler/verified.json`, command logs and `ui/` owned renders. During validation,
unused compiler-diagnostic helpers and an unused surface-selection accumulator
were removed to restore the existing warnings-as-errors build.

### Camera component selection and movement — Available, 2026-10-18

The camera uses the same native brush vertex/edge/face IDs and patch control
points as the plan views. Visible/X-ray selection, toggle and rectangle picks,
select-all, XYZ/view-plane movement and grid snapping share selection across
panes. A bounded worker validates a coalesced private draft through the normal
component service. The wire preview reports pending/invalid state; release
waits for the current result and commits that draft once. Document, selection,
navigation, grid, focus and tool changes cancel stale gestures. Topology
removal also accepts camera picks. Object/handle limits are enforced without
splitting an object's handle list and reported visibly.

`level-camera-components-smoke` covers the actual shell and semantic viewport
APIs: depth visibility, X-ray, band/toggle, XYZ snapping, stale/invalid responses,
pending release/cancellation, shared multi-brush edits, native patch points,
camera-only layouts, tool handoffs, locks, save-during-drag cancellation,
exact undo/redo/save/reload, and four editor profile
definitions at 100% and 200% with high contrast, expanded translations and RTL.
Evidence and owned widget captures are under `.agents/tmp/level-camera-components/`.
The seven focused suites passed with clang-cl 20.1.7 / Qt 6.10.1 and warnings
as errors: camera components, camera resize (widget and shell), camera brush
construction, brush component dialog/shell, model transform axes and brush core.
The offline guide, 22-page documentation site, credits, changelog and translation
extraction checks also passed.
No physical input or game launch is used. Native OS/screen-reader and large real
project acceptance remain open, as do textured deformation previews, component
rotation/scale and direct splitting/insertion gestures.

## Clipper increment — 2026-10-18

The following is a command-level audit for clipping only. The whole-editor
inventory and production completion gates remain open.

| Reference behaviour or command | VibeStudio equivalent | Status and evidence |
| --- | --- | --- |
| TB two/three-point clip plane; movable points | Shared Clip Tool in plan and camera views | Available; `level-clipper-ui-smoke` checks points, grid, projection changes, cancel and shared state |
| TB surface placement and face-plane matching | Camera placement and double-click face matching | Partial; semantic paths pass under four profiles; physical input remains unverified |
| TB remove last clip point | Backspace / Delete within the tool | Available; shared removal tested; numeric XYZ is the keyboard alternative |
| TB keep front/back/both; NRC `ClipperClip`, `ClipperSplit`, `ClipperFlip` | Tab, Enter, Shift+Enter, Ctrl+Enter; numeric Keep; `map clip --keep` | Available; checked candidate commit, pending apply/cancel, whole-side removal and undo tested |
| NRC quick clip | Quick Clip command; NRC Ctrl+left-click/drag plan gesture, remappable | Available through semantic tests; native gesture acceptance remains open |
| NRC Caulk Clipper Cuts | Inherit/current/custom material policy; editable caulk name; CLI `--cut-texture` | Available; original-face preservation and explicit cap flags tested in six syntax variants; compiler/package proof in three dialects |
| NRC Reset Flipped State / Reset Points on Split | Independent persistent side and point resets | Available; retained plane rebased after accepted edits, refused/cancelled drafts preserved, undo invalidates old points |
| NRC 2 Points in 2D Views | Optional two-point plan cycling; three points in camera/numeric editing | Available; replacing/cancelling a complete plane tested |
| NRC double-click clip/split preference | No cut, Clip or Split; Alt+double-click always matches a camera face | Available semantic routes; native double-click timing remains unverified |
| NRC one-point clipper / no-cut wrap-merge fallback | Explicit shared Clipper Options switch; numeric one point; CLI actual points and wrap material | Partial parity: whole-brush fallback passes core/CLI/UI/compiler tests; component-only wrap, dedicated command and native acceptance remain open. A failed cut never wraps. |

The core refuses partial edits, removes fully discarded brushes and empty
owners, retains unaffected selection, and records selection in undo/redo.
The worker coalesces requests and commits the exact checked document only
while revision, selection, load identity, save bindings and creation context
still match. The original scene remains visible behind the kept wire result;
this is not a textured replacement preview. A new source binding cancels the
session in every view. Numeric Preview uses the same worker.

Evidence is retained under `.agents/tmp/level-clipper/`. Tests cover core
source fidelity and atomic refusal, owner removal, scene locks, all-view
state, four profile descriptors, numeric preview, save cancellation, queued
apply, undo/redo and 100%/200% high-visibility rendering with expanded text/RTL.
The generated compiler workflow uses classic, Valve 220 and brush-primitive
maps, checks both halves after reopening, compiles BSP/VIS/light, verifies all
authored corners and inherited materials, and validates exact PK3 payloads.
Physical input, screen readers, macOS/Linux, real-project acceptance and
large-selection latency remain open.

Verification passed with Windows clang-cl 20.1.7, Qt 6.10.1 and warnings as
errors: five focused suites (map core, scene locks, clipper UI, camera
components and camera resize shell), 42 generated CLI/compiler/package
checks, 278-command documentation coverage, the 22-page documentation build,
credits/changelog checks and translation extraction for 48 catalogues.
The final clipper-only rerun also checks invalid numeric planes and the
task-specific Clip label at 200% scale with expanded text and RTL. Owned
widget renders were visually inspected. `build-verified.log`,
`regression-verified.log`, `numeric-verified.log`, `compiler/verified.json`,
`translations.log` and `ui/` retain the evidence in the directory above.

Unrelated issue observed during visual checks: `MapViewport::paintEvent`
hard-codes a black background in high-contrast mode, including when the global
high-contrast light theme is selected. Palette fidelity needs a separate fix.

### Edge/face splitting — Available, 2026-10-18

**Split Edge/Face** creates corners at selected edge midpoints or face centres
plus a drag delta in the plan views and camera. The shared worker now checks
plan movements and arrow nudges as well as camera gestures. Release commits
the exact checked candidate; pending release, cancellation, source/save guards
and atomic multi-brush undo use the same controller. Split gestures preserve
existing corners. Successful splits switch every pane to vertex mode and
select the inserted corners. Numeric **Split Selected Edges/Faces** and
`map split-components` expose the same operation with explicit grid/collapse
options. Stored material mappings, flags, owner/source bindings and native
map dialect survive the shared geometry/persistence path.

| Reference workflow | VibeStudio route | Status and remaining difference |
| --- | --- | --- |
| Create a corner from an edge/face | Split mode in plan/camera; numeric dialog; `map split-components` | Available at midpoints/centres; checked outward convex rebuild |
| TB arbitrary grid-point insertion while editing vertices | Direct Insert Vertex / TB Shift gesture / numeric insertion / batched `map insert-vertices` | Partial overall; exact native horizontal/absolute-snap combinations remain open |
| TB plane extrusion and split extrusion | Dedicated plan/camera plane tool, numeric dialog and CLI | Available below; exact profile gestures and behind-face edge hover remain open |
| Edit with texture/UV lock | Exact corner UV fitting for component transforms, separate from object lock | Partial overall; topology insertion/split/removal policies and broader native acceptance remain open |

Verification passed with Windows clang-cl 20.1.7 / Qt 6.10.1 and warnings as
errors: four focused suites (brush core/CLI, numeric brush UI, camera/plan
components and clipper regression). Core coverage splits all 12 box edges and
six faces in classic, Valve 220, brushDef and brushDef3, with exact persistence,
undo/redo, flags, zero-motion, invalid indices, interior points, atomic
multi-brush refusal and scene locks. Widget checks exercise pending release,
cancellation, grid/enablement changes, new-corner selection across panes,
switching active plan views, and numeric local history. Owned renders at
100%/200%, expanded translations and RTL were inspected; no input was injected.

The compiler workflow's `--operation split` passes 33 steps across classic,
Valve 220 and brush primitives: face split, edge split, native reopen, BSP,
VIS, light, dependency packaging and PK3 payload validation. All ten authored
corners and all six inherited materials reach the compiler. The documentation
build covers 22 pages and CLI coverage includes 279 registered commands.
Evidence: `.agents/tmp/level-component-split/build-verified.log`,
`regression-verified.log`, `compiler/verified.json`, `translations.log` and `ui/`.
Native input, screen readers, macOS/Linux, large-map timing and real-project
acceptance remain open. No new unrelated issue was found in this increment;
the high-contrast light viewport issue above remains outstanding.

### Face extrusion — Available, 2026-10-18

**Extrude Faces** moves brush planes along their normals with a checked
plan/camera draft. **Split Extrusion** preserves the original plus a new outward
section, or partitions an inward extrusion. The camera exposes a labelled **N**
axis; plan handles project onto that normal. Snapping chooses the nearer grid
distance or face-vertex/grid-plane crossing. **Extrude Faces Precisely…** shows
the reference face and accepts an exact signed distance. `map extrude-faces`
provides repeatable `--face brush:face`, `--distance`, `--split`, optional
`--grid` and `--no-texture-lock`, plus ordinary save protections.

`prepareLevelBrushExtrusion` preserves every original face, and
`extrudeLevelMapFaces` commits one guarded replacement transaction after native
geometry/mapping round trips. The survivor retains its brush identity and
owner, new sections inherit scene membership, and linked copies share undo.
Texture lock translates the moved face's projection; reversed split caps
retain matching world UVs. The active face is resolved from its written plane
and selected by its solved centre across all panes. The worker also checks the
exact distance and Texture Lock state before committing a draft.

Limits: 128 brushes, one selected face per brush, parallel normals, the existing
128-face/256-vertex topology bounds and ±32768 coordinate/distance bounds.
Opposing normals can move in one direction but cannot split together. Split
sections need at least 0.02 units. The GUI reference is the first selected brush
in document order; the CLI uses the first `--face`. Behind-face edge hover,
exact TB/NRC modifiers and broader deformation texture lock remain open.

Windows clang-cl 20.1.7 / Qt 6.10.1 builds pass with warnings as errors. Seven
focused suites pass: brush core/CLI, plan/camera components, numeric brush UI,
clipper, map, scene locks and linked groups. Coverage includes every box face
in both directions with/without split in classic, Valve 220, brushDef and
brushDef3; exact source undo/redo; native flags and reversed-cap UV round trips;
sloping plane/texture-lock behaviour; split-volume conservation; normal/grid
math; atomic multi-brush refusals; owner/group/linked-copy preservation; and
replacement-ID collision refusal. Semantic viewport checks cover preview,
pending cancellation, one-step commit/undo/redo and shared moved-face picks.
The numeric dialog's exact inward split passes at 200% high-contrast light,
expanded text and RTL. Owned plan/camera/dialog renders were inspected.
No operating-system capture or input injection was used.

The compiler workflow's `--operation extrude` passes 57 checks: ordinary
extrusion, outward split, inward split, native inspection, strict CLI
refusals, BSP/VIS/light, dependency packaging and byte-exact PK3 validation
in classic, Valve 220 and brush primitives with VibeMap3 0.4.0. Each final map
retains the three authored sections, all 24 authored vertex samples and six
inherited materials in the compiler output. Evidence lives in
`.agents/tmp/level-face-extrusion/`: `build-verified.log`,
`regression-verified.log`, `compiler/verified.json`, `translations.log`,
`cli-docs.log` and `ui/`.
The final translation-helper naming adjustment also passes the core build and
brush suite (`final-build.log`, `final-core.log`). Translation extraction checks
all 48 catalogues without errors; CLI documentation covers 280 commands, the
docs site builds 22 pages, credits/changelog validation and `git diff --check`
pass. Disposable failed-run logs and abandoned empty test directories were
removed; useful verification evidence remains in that project-local folder.

This increment does not close the overall production goal. Command-level
parity, native device/screen-reader and macOS/Linux checks, dense-map timing and
real-project acceptance remain required. No new unrelated issue was found;
the high-contrast light plan-background issue recorded above remains open.

## Group context acceptance increment

The shared context service resolves closed groups as units, opens nested groups,
limits ordinary editing to their contents and directs creation/prefab placement.
The Scene tree, plan/camera gestures, Objects list, selection commands and CLI
`editor scene select|translate` use it. Explicit scene administration remains
available, and undo/redo repairs context when a node disappears. Context is
session-only, does not dirty native bytes, and invalidates prepared work.
Automatic linked propagation may update outside siblings through the normal
lock boundary in the same undo step; direct outside edits cannot.

`level-group-context-smoke` covers nested navigation, exact undo and refusal,
creation/prefabs, entity ownership, explicit locks, linked updates and Doom/Hexen
exclusive/shared boundaries. It reports 10,000-brush index/pick/toggle timing
without imposing a machine-dependent CI threshold. Compound binary Doom moves
now deduplicate shared endpoints across lines and selected vertices. Explicit
vertex membership cannot bypass outside boundaries, and UDMF extension/global
properties use the same atomic context guard, including mixed property batches.
`level-group-context-ui-smoke` uses semantic Qt actions and owned renders,
including scaled/high-contrast/expanded RTL Scene controls, camera/plan/list
selection and cancellation of a pending component commit on context change.
The CLI suite checks unit toggles, dry-run, persistence and outside-edit refusal.
`level_scene_workflow.py --group-context` compares nested translations against
independently authored BSP geometry, runs VIS/LIGHT and validates a dependency
package. No native input or game launch is implied by these checks.

Remaining grouping gates include nested linked instances, protected properties,
cross-editor metadata, empty-group
cleanup/name semantics, profile-specific native gestures, production maps and
macOS/Linux/screen-reader acceptance. The full goal remains active.

Windows verification on 2026-10-18: the 15 focused core/CLI/Qt suites passed;
the group-context compiler workflow passed 19 steps with seven brushes and
matching BSP lumps 1–13 (including draw-vertex UVs), followed by VIS/LIGHT,
dependency packaging and package validation. The manifest records the exact
application/compiler SHA-256 values. Evidence is retained under
`.agents/tmp/level-group-context/`: `verified-tests.log`, `compiler/verified.json`,
`compiler/steps.json` and `ui/group-context-100.png` / `group-context-200.png`.

### Group transfer acceptance increment — Available, 2026-10-19

The shared scene transfer service preserves complete closed-group hierarchies
through duplicate, arrays, native `.map` clipboard and prefab capture/placement.
It retains empty descendants, ownership, visibility and locks, assigns new UUIDs
and unique sibling names, and validates node/member/depth limits before commit.
Open ancestors and layers stay editing context. Partial copies retain source
membership. Same-map duplicates remain in existing linked families; portable
families receive fresh link IDs and portable singletons become independent.
Existing linked propagation explicitly requests geometry-only duplication.

Version 2 prefabs carry bound native scene metadata; flat version 1 assets remain
supported. Inspect JSON reports group and linked-group counts. Placement stages
namespaced properties and geometry before restoring locks and link baselines.
Linked prefabs permit translation and Z quarter-turns; unsupported rotations fail
atomically. Stale or malformed scene metadata fails before insertion. Initial
locked-group imports now check the original scene in the undo guard assertion.

Core coverage includes nested/open contexts, owned brush entities and patches,
locked/hidden descendants, empty descendants, link identity and rotation frames,
exact undo/redo, native save/reload, metadata refusal, scene capacity and binary
Doom/Hexen/UDMF thing copies. The asynchronous prefab UI test now carries a
nested assembly through export, package staging, preview and placement, including
100%/200% text and expanded RTL controls. No native input is exercised.
`level_scene_workflow.py --group-transfer` independently authors the expected
duplicate/array/prefab geometry and UVs, compares BSP lumps 1–13, then runs
VIS/LIGHT and validates the dependency package.

Still open: linked prefab source updates, nested linked instances, protected
properties, standalone empty-group copy and empty-group cleanup, Doom geometry
duplication, cross-editor scene metadata, complete TB/NRC command inventory,
production-map performance and native Windows/macOS/Linux acceptance. The full
goal remains active; this increment does not establish production readiness.

Windows verification on 2026-10-19: all 18 focused core/CLI/Qt suites passed.
Additional linked-family propagation and single-undo checks passed in the
transfer suite. The compiler workflow passed 21 steps with ten brushes and
matching BSP lumps 1–13, then VIS/LIGHT and validated dependency packaging.
All 48 translation catalogues passed extraction checks, CLI coverage validated
280 registered commands, and the 22-page manual site built without broken links.
Evidence is retained in `.agents/tmp/level-group-prefabs/`: `verified-tests.log`,
`transfer-test.log`, `compiler/verified.json`, `compiler/steps.json`,
`translations.log` and the owned `ui/prefab-100.png` / `prefab-200.png` renders.
The compiler manifest records exact application/compiler SHA-256 values.

### Component affine transforms — Available, 2026-10-19

Picked brush vertices/edges/faces and patch control points now share move,
rotation, scale and shear, a common or explicit pivot, final grid snapping and
an explicit collapse policy. **Transform Components…**, existing transform
commands in component mode and `map transform-components` use the same core.
Empty component picks cannot fall through to whole-object edits. Checked worker
publication includes component selection identity, cancellation and one undo.
Surviving component handles follow the accepted geometry across plan/camera panes.

**Lock Component UVs** is separate from object Texture Lock and defaults off.
The original corner-correspondence fitter checks all corners and refuses
unrepresentable planar UVs. Explicit map-wide Valve 220 conversion preserves
unselected geometry/UVs and is undone with the edit. Atomic publication also
fixes early linked propagation and small undo-limit hazards in movement,
splitting and removal. Symmetric hull rotations preserve moved face identities.

Windows verification: 18 focused core/CLI/Qt regression suites passed, including
four native face dialects, partial selections, reflections/degeneracy, mixed
brush/patch batches, linked edits, one-step history, stale picks and cancellation.
`level_transform_compiler_workflow.py --components` passed 36 steps across
classic, Valve 220 and brush-primitive maps, independently checking 72 compiled
UV samples after face movement, rotation, scale and shear, then BSP/VIS/LIGHT
and three validated PK3s. All 48 translation catalogues passed extraction checks;
CLI documentation covers 281 commands; the 22-page manual built without broken
links. Owned widget renders at 100% and 200% text cover high contrast and expanded
RTL. Evidence: `.agents/tmp/level-component-transform/verified-tests.log`,
`verified-tests.json`, `compiler/verified.json`, `compiler/steps.json`,
`translations.log`, `components-100.png` and `components-200.png`.

At this stage, direct affine manipulators and pivot gestures remained open (the
following increment adds them). Still open: arbitrary insertion
gestures, UV policies for topology insertion/split/removal, dense-map conversion
performance, physical input/screen readers, macOS/Linux and real-project
acceptance. The full TB/NRC inventory and production gates remain active.
The existing high-contrast light plan-view background issue is unchanged.

### Direct component manipulators — Available, 2026-10-19

Plan and camera views now share rotation rings, axis/uniform scale, six shear
directions and XYZ/view-plane pivot controls. The pivot is shared with numeric
editing; moving it changes no geometry or undo history. Persistent angle and
factor steps have accessible numeric controls. Direct affine gestures preserve
corners; final-coordinate snapping and explicit collapse remain numeric/CLI
options. These controls use VibeStudio's existing modeller ray mathematics and
component geometry service, with original interaction and preview code.

The bounded component worker stages an immutable source. Serial checks reject
older results; release waits for current validation, and invalid geometry or
changed source state cannot publish. Accepted drafts preserve surviving picks
and create one undo step. First save and Save As now retain the editing context
while cancelling uncommitted previews.

Windows verification: 13 focused core/CLI/Qt suites passed. The new semantic
suite covers rotation through 450°, axis reflection, six shears, uniform scale,
pivot snapping, stale/duplicate results, invalid pointers, zero-volume refusal,
pending cancellation, source pick changes, exact undo, plan/camera publication,
shared preferences and first-save continuity. Owned 100%/200% widget renders
cover options, expanded RTL and high contrast. No physical input or OS capture
was used. The compiler workflow passed 36 steps across three dialects, checked
72 compiled UV samples and validated three nine-entry PK3s through the shared
numeric service. Gesture output is separately checked against that service;
this is not a claim of a native end-to-end input/compile acceptance run.

Translation extraction checks passed all 48 catalogues; CLI documentation
validated 281 registered commands, credits validated four compiler pins and
22 credit tokens, and the 22-page manual site built without broken links.

Evidence is in `.agents/tmp/level-component-gizmos/`: `verified-tests.log`,
`verified-tests.json`, `compiler/verified.json`, `compiler/steps.json`, the saved
`gesture-shear.map` and owned `gizmo-options-*.png` / `gizmo-plan-*.png` renders.
The compiler manifest records exact application and compiler SHA-256 values.

Remaining gates include arbitrary insertion gestures, complete reference-editor
command inventory, native profile/input and screen-reader acceptance,
macOS/Linux, representative dense-map latency and real-project use. Current
previews discard cancelled results; measured cooperative cancellation latency
for large component batches remains part of the performance gate. The existing
high-contrast light plan background issue also remains open.

### Component selection and terrain seams — Available, 2026-10-19

Plan/camera selection now shares replace, add, subtract and toggle policies.
Camera clicks select coincident solved geometry together: edge endpoints and
face corners must match, rather than just their centres. Keys are cached during
the existing topology pass and quantise coordinates to 1e-6 map units. Plan
selection retains projected stacks. Existing scene-context and handle limits
still constrain what is available. All/None/Invert operate on component picks,
retain selected objects/mode, add no geometry history and cancel pending drafts.

The TrenchBroom profile's plain component lasso toggles enclosed handles and
Ctrl adds them. Other profiles preserve replacement by default. Two per-profile
gesture fields expose this through GUI, CLI and portable preferences, bringing
the shared catalogue to 86 fields. Other Ctrl/Shift selection keys provide
add/subtract alternatives; exact native profile parity is not claimed.

Windows verification: 13 focused suites passed, including shared corner/edge/
face picking, crossing-edge isolation, 100%/200% high-contrast/expanded RTL
owned renders, context-preserving selection commands, one-step terrain edits,
exact undo and stale preview cancellation. The initial gesture regression
expected 84 fields; it was updated to 86 and passed, including CLI save/reset
round trips. Existing clipper, group context, affine tools, placement, material
gestures, document and modeller control suites passed.

`level_component_selection_compiler_workflow.py` passed 36 steps across classic,
Valve 220 and brush-primitive maps. Both sides of a selected terrain seam retain
the moved corner after BSP/VIS/LIGHT, with 132 compiled vertices checked for
finite coordinates/UVs. Three five-entry PK3s contain verified native maps,
compiled maps and their generated textures. The Qt suite exercises the shared
corner gesture and undo separately; no native input, game launch, installed
game data or OS capture is used.

Evidence is retained in `.agents/tmp/level-component-selection/`, including
`verified-tests.log`, `verified-gestures.log`, `verified-model-controls.log`,
`final-tests-full.log`, `verified-summary.json`, `compiler-final/verified.json`,
`compiler-final/steps.json` and `selection-100.png` / `selection-200.png`.
The final run also checks custom combined toggle chords retain their exact
configured role. Compiler evidence records exact executable hashes.
All 48 translation catalogues, 281 CLI commands and credits checks passed;
the 22-page manual site built without broken links.
Depth cycling, selection conversion, arbitrary insertion, dense-map latency,
native platform/input and real-project acceptance remain open. The unrelated
high-contrast light plan-view background issue is unchanged.

### Component topology selection — Available, 2026-10-19

Switching vertex/edge/face modes converts current picks through solved
incidence. Upward conversion requires every source boundary component;
touching conversion accepts any incident pick. Downward conversion selects
boundary components. Grow, Shrink and Connected follow brush edges/endpoints
or patch-grid neighbours, independently within each seeded object. Patch
points survive all brush mode switches; shrinking a patch erodes its open
border. A fully selected closed brush has no boundary to erode.

Compact topology is cached during the existing viewport solve. The selection
commands do not solve brush planes again. Pane sharing uses exact solved IDs;
position-based repicking still handles geometry edits that renumber topology.
Queries preserve object context and geometry history and cancel pending edits,
including when their result matches the existing picks. A truncated handle
context or destination exceeding the limit refuses conversion without losing
the current selection. The read-only `map select-components` CLI uses the same
service and emits sorted selectors consumed by `map transform-components`.

Windows verification covers complete/touching conversion, missing face edges,
brush adjacency, repeated/mixed sets, patch boundaries, stale-ID refusal,
limited viewport contexts, pane/mode agreement, cancellation and exact undo.
Opening or clearing a map releases component state and prevents stale queries.
The 128-brush timing probe measures the cached query only; it does not establish
whole-editor latency. Existing selection, clipper, gizmo, affine, grouping,
placement, gesture, material, document and modeller checks passed. The new
UI harness initially omitted the normal pick-change notification and inspected
layout before hidden-widget rendering had laid it out. Those harness issues
were corrected; the final UI check uses real 100%/200% settings, expanded RTL
labels and inspected owned-widget renders. Native OS input was not controlled.

The optional compiler workflow's `--selection-topology` path passes 60 steps:
Grow/Shrink restores a seed; face-to-edge-to-vertex conversion supplies the
actual transform selectors; both top faces move through checked transforms,
BSP/VIS/LIGHT and three five-entry PK3s. Classic, Valve 220 and brush-primitive
maps retain the shared compiled seam; 120 compiled vertices have finite
coordinates and UVs. Evidence in
`.agents/tmp/level-component-topology-selection/` includes the regression logs,
UI recheck, `verified-summary.json`, executable hashes, `compiler-final/`,
and `tools-100.png` / `tools-200.png`. The final executable's compiler proof is
in `compiler-verified/`; earlier compiler runs remain as historical evidence.
Fifteen focused suites passed, with the six affected suites rerun after the
map-close lifecycle correction. All 48 translation catalogues, 282 CLI
commands and credits validate; the 22-page manual builds without broken links.

These topology commands are original VibeStudio extensions, not a claim of
native TB/NRC shortcut parity. Depth cycling, arbitrary insertion, broader
native/platform/real-project acceptance and whole-editor performance remain
open. The existing high-contrast light plan background issue remains open.

### Component depth cycling — Available, 2026-10-19

Plan and camera views share near/far cycling over eligible component handles.
Full geometry forms one stop, while crossing edges and distinct faces with the
same centre remain separate. Picks outside the overlap survive. Plan ordering
uses +Z/-Y/+X for Top/Front/Side; camera ordering uses its projection and honours
visible/X-ray picking. Behind-camera and offscreen handles remain excluded.
Tab/Shift+Tab, named menu/Tools commands and independently configurable wheel
chords use the same query. The profile catalogue now contains 88 fields, with
GUI/CLI/portable persistence and migration for existing FOV wheel bindings.
Truncated contexts refuse cycling and pending edits cannot publish after a
selection command cancels them. Geometry history changes only on a checked edit.

Plan dragging preserves the selected depth. Selected component handles paint
last, and conflicting object resize handles are hidden in component mode.
High Contrast Light now uses the studio's light background and dark accents;
both high-contrast variants use theme tokens. Overlap ordinal and XYZ coordinates
are available in status and accessibility descriptions.

Windows semantic checks cover ordering, wrap, clumping, unrelated picks,
occlusion, near clipping, invalid input, bounded contexts, fractional/custom
wheel input, pane agreement, pending-edit cancellation and exact undo. Ten
focused suites passed, including clipper, grouping, affine/gizmo and profile
regressions. The final camera empty-click cursor reset is also checked through
the affected cycle, camera-component and shared-selection suites. The initial
shell test had not activated its owned offscreen window, so a requested focus
change never reached the camera; the harness now verifies focus explicitly.
No native input was injected or captured. Owned 100%/200% light/dark and expanded
RTL renders were inspected, including selected/unselected overlap pixels.

The synthetic 16,384-hit query fell from approximately 154 ms to 31 ms after
replacing per-group tree/set allocation with hash indexing and compact lists.
This is a single core-query sample, not a whole-editor latency measurement.
Evidence is in `.agents/tmp/level-component-depth-cycle/`, including
`verified-tests.json`, `cursor-tests.json`, `verified-summary.json`, executable
hashes and `cycle-plan-*.png` / `cycle-tools-*.png`. The 22-page manual builds,
all 48 translation catalogues validate, and all 282 CLI commands remain covered.
No map format, transform or compiler protocol changed in this increment.

Arbitrary insertion gestures, the full reference-command inventory, native
platform/input and screen-reader acceptance, dense real-map latency and
real-project use remain open. VibeStudio remains pre-alpha. The earlier
high-contrast light plan background issue is resolved.

The source-layout check also found that the shared component-selection header
was missing from Meson's installed headers; its install entry is now verified.
Automatic approval review rejected removal of this increment's generated
`docs-site/` evidence directory as "blocked by policy". That disposable site is
retained under the task evidence directory; source and user assets are unchanged.

## Clipper workflows — 2026-10-19

The shared clipper now supports one-shot **Quick Clip**, explicit custom/caulk
and current-material cuts, independent point/side reset preferences, optional
two-point plan cycling and double-click Clip/Split. **Alt+double-click** retains
camera face-plane matching. Numeric clipping displays its material and opens
the same **Clipper Options…** controls. Defaults preserve previous VibeStudio
behaviour; saved options apply across profiles and sessions.

NRC's Ctrl+left-click/drag quick entry is a new profile field,
`plan.quickClipModifiers`. Other profiles retain their own defaults, including
the separately audited NetRadiant baseline. A named command provides the same
workflow in every profile. Existing custom selection bindings take priority;
explicit conflicts fail validation. Component/drawing tools keep their input
roles. A refused cut retains points and quick mode; success exits all views
after one atomic undo step.

`LevelMapClipOptions` validates explicit materials before editing, preserves
brush contents and clears inherited surface flags/value on the new caps.
Original face text stays exact. The async draft captures the material along
with document/selection/save bindings. Changing the material or opening options
cancels queued Apply. Accepted cuts can retain/rebase their plane; unrelated
document/selection changes and undo still invalidate stale points.

`map clip --cut-texture` uses that same service and defaults to inheritance
independently of GUI preferences. `editor clipper` provides strict, atomic
settings inspection/change/reset and dry runs. All 89 profile gesture fields
remain available through GUI, CLI and portable exports.

Ten suites passed on the final build: clipper options/core/CLI, clipper UI,
map core, scene locks, linked groups, gesture core/CLI, gesture UI, material
painting UI, camera components and component depth cycling. New checks cover
six map syntaxes (classic, classic flags, Valve 220, Valve flags, brushDef and
brushDef3), cap flags, preserved source lines, exact undo/redo, strict CLI and
settings refusals, pending material changes, retained/cancelled planes, quick
success/refusal/cancel, double-click routes and named controls. Offscreen
renders at 100%/200% include high-visibility themes and expanded RTL strings.
Visual review caught and fixed overlapping option labels at 200%; labels now
occupy separate rows and the regression checks their bounds.

The reusable `src/tests/level_clipper_compiler_workflow.py` passed 48 steps on
the final executable: visible and caulk cuts in classic, Valve and primitive
maps, VibeMap3 BSP/VIS/LIGHT, independent compiled cap-plane/material/flag
checks, dependency closure and six validated PK3s. Visible caps produce draw
surfaces; caulk caps retain their brush sides with `SURF_NODRAW` and no rendered
surface. Fixtures, textures and shader text are original; no game was launched.

Evidence is in `.agents/tmp/level-clipper-workflows/verified-summary.json`,
with test logs, rendered controls and `compiler/verified.json`. The final
executable SHA-256 is
`88bea0cbc48dcc133e18f004afd03c3d64b0438239a81a23179d61d5f37cfc4d`.
All 48 translation catalogues, 283 CLI registrations, source layout, credits,
changelog and the 22-page manual/site validated. The offline guide was regenerated.

No new unrelated product issues were found. Full textured previews, NRC's
one-point/no-cut wrap-merge fallback, native pointer/keyboard and screen-reader
acceptance, macOS/Linux, dense real-map latency and the wider command inventory
remain open. The production goal remains active.

Automatic approval review rejected removal of the verified disposable
`docs-site/` and empty `vibestudio-package-imports/` directories as "blocked by
policy". They remain under this increment's evidence folder; the rejected
cleanup was not retried.

### Direct vertex insertion — 2026-10-19

**Available:** **Insert Vertex** picks arbitrary grid points on solved edges or
bounded face polygons in the top, front, side and camera views. A plus diamond
previews the point; the checked wire hull appears after a bounded asynchronous
request. Coincident whole components group their selected incident brushes.
The shared core transaction preserves original corners, materials/flags,
source dialects, scene ownership, linked-instance propagation and one-step undo.
Interior/flat corners, invalid geometry or a locked member refuse the batch.

The explicit tool is available for every profile. The TB profile adds Shift
insertion, with Alt for Z movement where reachable; profile modifiers are
configurable through GUI, CLI and portable gestures (91 fields). Existing
vertex handles keep their selection role. Ordinary insertion follows the view
plane. Exact TB horizontal/absolute-snap combinations remain **Partial**.
The numeric brush editor remains a keyboard route, and `map insert-vertices`
now accepts repeated distinct `--brush` IDs with the same positions atomically.

Release waits for the latest serial; cancel, invalid pointer, selection/context
changes, focus loss and stale work cannot apply a preceding draft. Successful
insertion selects the new corners across views. Review removed old movement
handles that obscured the insertion preview. Offscreen tests explicitly wait
for document/render readiness between operations; early tests exposed a timing
assumption in that test harness, which was corrected.

Eleven final suites passed, covering the new core/CLI and UI paths plus camera
components, manipulators, gestures/core/UI, brush topology, scene locks, linked
groups, depth cycling and clipper UI. Six map syntaxes round-trip, batch failures
leave bytes/history/reports unchanged, and linked edits undo exactly. Owned
renders at 100%/200% cover high visibility, expanded strings and RTL. A cached
128-brush pick query measured about 1.15 ms in the initial run; this does not
measure a full dense-project frame or establish native platform acceptance.

The reusable `src/tests/level_vertex_insertion_compiler_workflow.py` passed
57 steps on the final executable: face and shared-edge corners in classic,
Valve and primitive maps, native VibeMap3 BSP/VIS/LIGHT, independent compiled
collision-plane and draw-vertex checks, dependencies and six validated PK3s.
Fixtures and textures are original. No game was launched or user input driven.

Evidence: `.agents/tmp/level-component-insertion/verified-summary.json`, final
suite logs, `captures/` and `compiler/verified.json`. Application SHA-256:
`64fffb6134a79be510eb0a6b9575c3b513df339f987af831c58a3d899cef6111`. All 48 catalogues extract, 283 CLI commands validate, the 22-page
manual/site builds, and the offline guide is regenerated. Source layout,
credits, changelog and whitespace checks pass.

No new unrelated product issues were found. Full TB/NRC inventory, topology UV
lock, full textured previews, native input/screen-reader/platform acceptance,
dense real projects and broader collision/degeneracy coverage remain open.
The full production goal remains active; VibeStudio remains pre-alpha.

Automatic approval review rejected cleanup of this increment's verified
disposable `docs-site/` and empty `vibestudio-package-imports/` directories
with "blocked by policy". Both remain in its evidence directory; the rejected
deletion was not retried. Required source and evidence files were preserved.

### Component drag profiles — 2026-10-19

**Available:** the TB profile now enables world-axis movement and insertion
in plans and the camera, with live constraints and relative/absolute vertex
snapping. Two portable per-view preferences bring the gesture schema to 93
fields. Existing profiles retain their movement policy. Normal extrusion,
explicit XYZ handles, affine transforms, numeric editing and CLI geometry
services remain integrated with the same selection, grid, UV and undo state.
The bounded behaviour audit above records precise remaining differences.

Only a validated preview advances the movement anchor. Release consumes the
latest prepared geometry; stale, refused or cancelled work cannot publish.
Deferred clicks preserve Ctrl selection and selected-set dragging. The core
tracker uses double precision and recognises the camera service's float
transport when accepting a checked result. No document or input-device access
occurs in the geometry tracker.

Thirteen final suites passed: component drag core/UI, insertion core/UI,
camera components, component cycling, clipper UI, manipulators, gesture core/UI,
brush topology, scene locks and linked groups. Tests cover all three plans,
perspective XY/Z movement, off-grid and hidden-coordinate snapping, initial
versus live Shift, dominant-axis ties, rejected preview anchors, vertical reset,
combined insertion modifiers, edge/face policy, selection preservation,
profile cancellation, exact core/GUI equivalence and one-step byte-exact undo.
An initial camera test moved less than the view's drag threshold; the fixture
was corrected to exercise an actual drag. The product threshold was retained.

Owned renders at 100% and 200% were inspected, including high visibility,
expanded translations and RTL. The compact large-text viewport elides status
text; its accessible description retains the complete constraint, coordinates
and validation state. Physical input, screen readers and native macOS/Linux
acceptance remain open. This increment changes interaction and delegates
geometry publication to the previously verified core; prior compiler/package
evidence remains recorded with its own executable hash.

Evidence: `.agents/tmp/level-component-drag-profiles/verified-summary.json`,
`verified-tests.*`, validation logs and `captures/`. Application SHA-256:
`666cf50b201aca6d49570eab099e81fabfba8508dfb103f1ada186ec8b7d6407`.

No unrelated product issues were found. Complete TB/NRC command inventory,
quick-snap clicks, movement traces, textured previews, topology UV semantics,
native/platform acceptance and the broader authoring gaps remain unfinished.
The production goal is active; VibeStudio is still pre-alpha and largely
untested in real projects.

Final validation also passed source layout, credits, changelog, whitespace,
283 CLI command registrations and all 48 translation catalogues (1,595 source
files, no unextractable literals). The offline guide was regenerated and the
22-page manual site built without broken links.

Automatic approval review rejected removal of this increment's disposable
`docs-site/` output with "blocked by policy". The generated site remains under
its task evidence directory; deletion was not retried. Source files, captures
and validation logs are preserved.

### Vertex quick snapping and movement traces — 2026-10-19

The TB profile's Alt+Shift click snaps a picked geometric brush vertex exactly
to another existing brush vertex. Coincident picks may span multiple brushes;
distinct source positions are refused. Crossing the drag threshold instead
starts movement of the grabbed geometry. Two independent, portable quick-snap
preferences bring the gesture schema to 95 fields; older custom selection
chords keep priority. Every profile has **Snap Picked Vertex…** with a named
keyboard target selector and stale-source checks. CLI `map transform-components
--operation snap --target brush:ID/vertex/ID` uses the same exact displacement
and atomic geometry transaction, with collapse enabled and normal UV/scene
guards. Move displacements now span ±65536, while final corners and pivots stay
within ±32768.

Movement and insertion share labelled X/Y/Z distance segments. Constrained
axes are thicker and pending checks are dashed. Labels avoid each other and
both possible viewport HUD rows; small panes may omit a label, with complete
values retained in accessible status. The primary plan object label returns
after an active component drag/insertion, leaving space for the trace while
selection markers remain. Visual review caught and corrected a 200% overlap.

Verification on Windows / Qt 6.10.1 / clang-cl 20.1.7:

- Sixteen targeted suites passed: vertex snap core/CLI and UI, component
  transform core/CLI/UI, component drag core/UI, insertion UI, camera components,
  gizmos, depth cycling, brush topology, scene locks, linked groups and gesture
  core/CLI/UI. The three affected UI suites passed again after the overlay
  spacing correction (14.13, 14.23 and 24.63 seconds).
- Snap fixtures cover four brush syntaxes, a shared coincident source, atomic
  locked-member refusal, invalid/ambiguous targets, no-op history, a 64000-unit
  displacement, off-grid targets with grid 64, all plan projections, camera,
  remapped Meta clicks without world-axis dragging, cancelled clicks/drags,
  stale target dialogues and exact undo. No physical input was injected.
- Owned renders cover 100%/200%, high-contrast light/dark, expanded translations
  and RTL. Axis letters and numeric distances provide non-colour information;
  target coordinates remain available when the selector text is elided.
- `level_topology_compiler_workflow.py --operation snap` passed 27 steps across
  classic, Valve 220 and brush primitives. Each seven-corner hull retains its
  six inherited materials through BSP/VIS/LIGHT. The removed corner lies
  outside compiled collision geometry; retained corners remain inside and
  appear in draw geometry. Three PK3s validate with exact authored map/BSP and
  texture payloads. No game installation or engine launch was used.

Evidence: `.agents/tmp/level-component-quick-snap/verified-summary.json`,
`verified-tests.*`, `overlay-tests.*`, `compiler/verified.json` and `captures/`.
Final application SHA-256:
`e3cd84b41dc295fbdf18587790bcfacf5c73f15e4b623744f9619b107936a27f`.
The sixteen-suite baseline used
`87d401ba56175eafca83f3aad7cc6d5af3d829d28278b3dd3e6fcd861705bf7d`;
the three overlay reruns and repeated compiler workflow use the final build.

No unrelated product issues were found. Synchronous source/target topology
preparation still needs dense-map latency measurements. Full command-level
TB/NRC inventory, native/platform/input/screen-reader acceptance, projected
click-clumping parity, textured previews, topology UV semantics, NRC
one-point/wrap-merge clipping and the broader authoring gaps remain open.
The production goal remains active; VibeStudio is still pre-alpha and largely
untested in real projects.

## NRC clipper wrap/merge — 2026-10-19

The clipper now offers explicit **Wrap/merge when the plane makes no cut**,
persisted with the shared clipper preferences and off by default. One point,
a degenerate plane, or a plane that neither cuts nor removes any source builds
a convex hull around whole selected brushes and the actual placed points.
The synthetic third point used for a two-point plane never joins the hull.
Valid cuts/removals take precedence; a protected, malformed or numerically
invalid cut never falls back. A single brush already enclosing the points
refuses a no-op without history. Exact-union **Merge Brushes** keeps its
gap-refusal contract.

Exterior planes retain material, UVs and flags, with deterministic first-source
choice for coincident supporting planes. New hull faces use the current
material, independently of custom cut/caulk policy. The last selected brush
retains its identity, entity and scene group, including overlapping entity/brush
selection. Empty source entities are removed. Normal scene protection,
linked-instance propagation and one-step undo apply. The worker cancels stale
preparation; the dashed preview names wrap/merge and suppresses discard hatching.
Numeric one-point entry is available through **Clip Selection…**. The CLI shares
the operation through `map clip --wrap-merge --wrap-texture`, actual `--points`
and optional two-point `--view-axis`. Reports distinguish clips and wraps.

The original hull builder permits interior/duplicate/coplanar points through
an envelope entry point while primitive creation retains its stricter contract.
Limits are 64 brushes, 2,048 source faces, 4,096 input points, 128 output faces,
256 output vertices and ±32768-unit hull coordinates. Original comments survive
once; generated and inherited planes are reparsed and checked before commit.

Eight focused suites passed on the final executable: wrap core/CLI, clipper
options/core/CLI, clipper UI, exact-merge core/CLI and UI, brush topology, map
core and placement cancellation. Coverage includes four UV/face dialects,
inside-point refusal, real-cut precedence, true two-point/collinear fallback,
selection-order destination, cross-owner cleanup, single- and multi-brush linked
updates, locks, cancellation and 64/65-brush budget boundaries. Offscreen semantic
UI tests cover plan/camera Apply, numeric one-point preview, pending material
changes, double-click, tool exit and undo, plus 100%/200% high-contrast,
expanded-translation and RTL settings layouts. No user input or OS capture was
used. Owned-widget renders were inspected.

`level_topology_compiler_workflow.py --operation wrap` passed 24 steps on the
final executable: classic, Valve 220 and brush-primitive maps each retain a
nine-vertex/nine-face hull through VibeMap3 BSP/VIS/LIGHT. Rendered corners and
collision planes include the new apex; five inherited materials and the explicit
new material survive. Three validated PK3s contain byte-exact MAP/BSP/assets.
This uses original synthetic fixtures and launches no game.

Evidence is retained in `.agents/tmp/level-clipper-nrc-advanced/`, including
`final-tests.json`, `compiler-final/verified.json`, `captures-final/` and
`verified-summary.json`. Final executable SHA-256:
`90f4a3709100227f7702a20933411afd8f53fae137181d626d84deaf2e707e68`.
Translation extraction passed for 48 catalogues, 1,601 source files and 26,206
call sites, with no unextractable literals. CLI documentation covers 283
commands; the regenerated offline guide and 22-page documentation site pass.
Source layout, credits, changelog and whitespace checks also pass.
Automatic approval review rejected cleanup of this increment's verified
disposable `docs-site` directory (35 entries) and empty
`vibestudio-package-imports` directory with “blocked by policy”. Both targets
and their ancestors were checked for links. No deletion ran or was retried;
the retained paths are recorded in `verified-summary.json`.

Remaining parity gates include wrapping only picked components, a dedicated
wrap command, NRC camera-plane details, textured previews, complete command-level
TB/NRC inventory, production-map performance and native Windows/macOS/Linux,
input and screen-reader acceptance. The broader component, UV, entity, patch,
group and prefab gates remain active. No unrelated product issues were found.
The full production goal is not complete; VibeStudio remains pre-alpha and
largely untested in real projects.


## Standalone and component wrap/merge — 2026-10-19

**Wrap/Merge** now has a standalone GUI command, the audited NRC Ctrl+U binding,
shared command catalogue/profile discovery and `map wrap-merge`. Whole-object
mode retains the previous hull replacement contract. Component mode gathers
only picked brush vertices, edge endpoints, face corners and patch controls,
then adds a new convex brush while retaining source objects and component picks.
Every component-hull face uses the current material, default UVs and zero flags.
Actual clipper points join the standalone command when the clipper is active;
the optional clipper fallback setting does not gate it.

The last selected object supplies native entity ownership and scene membership;
a brush destination also supplies the face dialect. An empty object selection
uses the last component set. A selected point entity falls back to worldspawn
and the current creation context. The normal guarded
scene insertion handles locks, editing contexts, linked copies and one undo.
Component limits are 256 sets, 64 source objects and 4,096 input points; output
limits remain 128 faces, 256 vertices and ±32768 units. Invalid IDs, flat picks,
unsafe materials and unrepresentable persisted corners refuse the whole edit.

**Wrap/Merge with Points…** provides zero to three numeric XYZ points, a material
override and source/result views. A coalescing worker prepares geometry and
package/staging materials; changed inputs clear the old preview. Publication
rejects stale document, component, clipper and asset state. The scrollable form
keeps numeric coordinates left-to-right in RTL, while validation/progress and
Apply remain outside the scroll area. Closing the dialog cancels pending work.

Seventeen focused suites passed after targeted fixes and reruns: component wrap
core/CLI and UI, clipper wrap/options/UI, exact merge core/UI, brush/map/primitive
services, placement cancellation, component transforms, scene locks, group
contexts, linked groups, editor profiles and command semantics. The catalogue
regression exposed and fixed an omitted static command entry. The mixed-source
CLI byte oracle now reopens the same persisted input as CLI, distinguishing
source-preserving serialization order from a newly inserted in-memory patch.
Coverage includes all four face dialects, vertex/edge/face/mixed/patch inputs,
owner/group destinations, linked additions, source retention, exact undo/redo,
malformed CLI, budgets and cancellation through finalization. Owned UI checks
cover rapid edits, invalid previews, actual clipper points, stale selections,
100%/200% high contrast, expanded RTL, focus/names and unclipped validation.
No user input control, OS capture or game launch was used.

The original `level_topology_compiler_workflow.py --operation component-wrap`
passed 30 steps. Classic, Valve 220 and brush-primitive maps each keep their
source brush and add a five-vertex/five-face pyramid from its picked top face
plus one point. VibeMap3 BSP/VIS/LIGHT preserves all authored and original render
corners; collision planes stop at the new apex and exclude unpicked lower
volume. Three validated PK3s contain byte-exact MAP/BSP assets and all original
materials, including the source face hidden by the new hull.

Evidence is retained in `.agents/tmp/level-component-wrap/`: `verified-tests.json`
with targeted `verified-core.json` and `verified-ui.json` reruns,
`compiler-final/verified.json`, owned dialog renders and `verified-summary.json`.
Final executable SHA-256:
`c1fd19866d413ddfc2ea76f6f197d2313347cf8ff0514400a1ab02e759de0923`.
Translation extraction passes 48 catalogues, 1,608 source files and 26,267 call
sites with no unextractable literals. CLI documentation covers 284 commands;
the refreshed offline guide and 22-page manual site build without broken links.
Source layout, credits, changelog and whitespace checks pass.

Automatic approval review rejected cleanup of this increment's verified
`docs-site` (35 descendants), superseded `compiler` fixture (112 descendants)
and empty `vibestudio-package-imports` directory with “blocked by policy”.
All exact paths and ancestors were checked for links. No deletion ran or was
retried; these retained paths are listed in `verified-summary.json`.

Remaining gates include NRC camera-plane parity, textured cut previews,
command-level TB/NRC inventory, broader component/UV/entity/patch/group/prefab
workflows, dense production-map performance and native Windows/macOS/Linux,
physical-input and screen-reader acceptance. No unrelated product issues were
found. The full production goal remains active; VibeStudio is pre-alpha and
largely untested in real projects.

## NRC two-point camera clip planes — 2026-10-19

The NRC profile now chooses a two-point camera plane from the dominant view
direction, repairs parallel segments using near-plane projection, and changes
an axial outer-face plane into a cut through the selected bounds. The shared
`levelCameraClipAxis` service changes only the stored extrusion axis/sign;
the two actual points remain intact for wrapping and deterministic replay.
Established planes stay fixed during camera navigation and across all views.
Dragging a point retains the axis unless correction is necessary; cancellation
restores the complete prior plane. Three actual points and exact face matching
keep their existing behaviour.

`camera.clipViewAxis` is a typed, persistent per-profile gesture preference,
enabled by default only for NRC. Other profiles, including standalone NetRadiant,
retain their defaults. Changing camera controls cancels an active point drag.
The CLI accepts two-point `--camera-eye`/`--camera-forward` cuts or explicit
`--view-axis`/`--view-sign` replay, and reports the resolved axis/sign in JSON.
Invalid, incomplete, non-finite, overflowing or degenerate requests never save.
The regular clipper worker, scene protection, undo, save and build/package
services remain the operation path; no AI service is required.

This bounded audit follows the credited NRC revision `449778b3`:

| Reference behaviour | VibeStudio route | Status / remaining acceptance |
| --- | --- | --- |
| Dominant camera axis with deterministic ties | Shared camera-plane service and NRC profile | Available; direction and tie oracles pass |
| Parallel-segment near-plane correction | Same service, GUI and CLI | Available; parallel, stored-axis, behind-camera and coincident-ray cases checked |
| Axial outer-face correction using selected bounds | Same service and selection-bound resolver | Available; positive/negative faces, invalid bounds and overflow checked |
| Retained plane across point dragging and navigation | Shared `LevelClipState` | Available; all views, cancellation and profile-change checks pass |
| Three actual points independent of two-point policy | Existing explicit-plane path | Available; policy changes preserve the plane |
| Modifier-based free point movement and wider scene surface/edge/vertex snapping | Existing brush-face placement only | Partial; native movement and snapping still need implementation and acceptance |
| Native interaction, rendered cut surfaces and production-map latency | Owned semantic and wire-preview checks | Partial; physical input, textured previews, platform and real-project acceptance remain open |

Thirteen focused suites pass, with `level-gestures-smoke` rerun after correcting
its expected preference count from 95 to 96. New core/CLI tests cover classic,
Valve 220, brushDef and brushDef3 source preservation, opposite keep direction,
exact camera/axis replay, dry-run persistence, malformed input and exact undo.
Owned shell tests cover face placement, cross-view state, checked application,
point-drag cancellation, profile changes and the unchanged legacy policy.
100%/200% high-contrast camera renders, including RTL, were inspected; no user
input or operating-system capture was used.

The final VibeMap3 workflow passes 42 CLI/compiler/package steps in classic,
Valve 220 and brushDef maps. Both halves reopen with sixteen corners and twelve
faces in total, all six source materials survive, all authored corners occur
in the compiled BSP, and each validated PK3 contains byte-identical MAP/BSP/
texture payloads. No game was launched. Application SHA-256:
`1eca61b5232eb873cb9b7c310d9109bb5f423cff0bd17ebf3702002bfe7fe48c`.
Evidence: `.agents/tmp/level-camera-clip-plane/verified-summary.json`,
`final-tests.*`, `gestures-final.*`, owned camera renders and
`compiler-final/verified.json`.

All 48 translation catalogues extract from 1,612 source files and 26,277 calls
with no unextractable literals. The 284 CLI registrations, source layout,
credits, changelog and whitespace checks pass. The offline guide was regenerated
and the 22-page documentation site builds without broken links.

Automatic approval review rejected cleanup of this increment's generated
`docs-site` (35 descendants), superseded `compiler` fixture (133 descendants)
and empty `vibestudio-package-imports` directory with “blocked by policy”.
The exact paths, ancestors and descendants were checked for links. No deletion
ran or was retried; `cleanup-audit.json` and `verified-summary.json` record them.

The full TB/NRC command inventory, broader component/UV/entity/patch/group/
prefab workflows, dense production maps, native Windows/macOS/Linux input and
screen-reader acceptance remain open. No unrelated product issues were found.
The full production goal remains active; VibeStudio is still pre-alpha and
largely untested in real projects.

## NRC modified camera-point movement — 2026-10-19

Camera clip points now have a shared pure tracker for fixed press anchors,
world XY/Z or axial view/depth movement, live Shift axis constraint and relative
grid snapping with explicit nearest-even ties. The NRC profile enables `world`
movement; other profiles retain `surface`. `camera.clipDragStyle` independently
selects `surface`, `world` or `view` through the gesture form, CLI and portable
preferences. The setting does not change the two-point plane policy.

After a plain point press, Shift constrains a horizontal/view-plane displacement
and Alt chooses Z/view depth. Ctrl takes precedence and returns to the existing
brush-face sampler. A surface miss retains the previous point. Modifier press
and release use the last pointer position without polling input devices. The
readout and accessible description name the point/axis or explain a refused
projection. No user input or operating-system capture is used in these tests.
Changing projection, grid, profile, focus or shared clip state retires the
tracker; cancellation restores the complete prior plane. The eventual cut uses
the existing checked worker, scene protection, atomic undo and save pipeline.

The bounded audit follows `ClipManipulatorImpl::Transform`, `TranslateFreeXY_Z`
and `float_snapped` at the [credited NRC revision](../CREDITS.md#shared-clipper):

| Reference behaviour | VibeStudio route | Status / remaining acceptance |
| --- | --- | --- |
| Shift chooses the strongest plane axis; Alt moves depth | Shared `LevelClipPointDrag`, camera semantic/native routing | Available; independent numeric oracles and owned viewport checks pass |
| View-dependent movement uses the dominant axial direction | `camera.clipDragStyle=view` | Available; all three axes checked in core, YZ/X in the camera |
| Fixed press anchors and relative nearest-even grid | Same tracker | Available; off-grid offsets, modifier transitions, tie signs and grab offsets checked |
| Ctrl overrides free movement with scene sampling | Existing camera brush-face resolver | Partial; precedence passes, wider edge/vertex/patch/model scene snapping remains open |
| Safe cancellation and visible refusal | Shared state/readout and existing lifecycle paths | Available; unresolvable projection, navigation, profile changes and external state replacement checked |
| Native first-point dragging, physical gestures and full previews | Existing VibeStudio plain drag can add a second point | Partial; the documented creation adaptation, textured cut previews and native/platform acceptance remain open |

New core coverage also checks invalid points, axes, rays and grid values,
behind-camera intersections, parallel planes, overflow, portable preferences
and NRC-only defaults. Owned camera checks cover live Ctrl precedence/release,
modified single-point creation, shared state, readout accessibility and a freely
moved oblique plane whose accepted MAP bytes match the core service exactly.
The cut uses one undo step and undo restores the original source exactly.
Gesture UI coverage applies and round-trips the new policy independently of
the plane policy and renders its control at 100%/200% with expanded RTL text.

The final binary also replays the camera-plane compiler/package acceptance
fixture: 42 CLI/VibeMap3/package steps pass in classic, Valve 220 and brushDef,
with all authored corners and six source materials retained and byte-identical
MAP/BSP/texture files in validated PK3s. This fixture covers axial camera cuts;
the freely moved oblique plane is covered separately by the GUI/core oracle.
No game was launched. Application SHA-256:
`fd4408d99dc912d38ee87676f1b05ab4863a41e36d187effc97a51bd822bd9b8`.
Evidence is under `.agents/tmp/level-camera-clip-drag/`, including the final
suite logs, owned renders, `compiler-final/verified.json` and `verified-summary.json`.

Fourteen focused suites pass across the recorded runs. The parallel run timed
out `level-clipper-ui-smoke` after 182.56 seconds without a reported assertion;
the same test passed in isolation in 44.97 seconds. Other thirteen suites passed.
The functional baseline used application SHA-256
`043ec8e46d70b9fbd969dc90542ced1d718db3abed02bcf033106c95c7a9dc8a`.
Visual review then found truncated movement-choice text at 200% with expanded
RTL labels. Only the two choice labels were shortened; the GUI/CLI gesture
suites and compiler workflow were repeated on the final build. The control's
100%/200% renders were inspected. The parallel timeout remains recorded;
isolated success does not establish dense-map or native input performance.

All 48 translation catalogues extract from 1,615 source files and 26,284 calls,
with no unextractable literals. Source layout, credits, changelog, whitespace,
284 CLI registrations and the 22-page documentation build pass; the offline
guide is regenerated. Stale gesture/profile counts in related documentation
were corrected. No new unrelated product issues were found.

Automatic approval review rejected removal of this increment's verified
`docs-site` (35 descendants), superseded `compiler` fixture (133 descendants)
and empty `vibestudio-package-imports` directory with “blocked by policy”.
The exact targets, ancestors and children were checked for links. No deletion
ran or was retried; the retained paths are in `cleanup-audit.json` and the
verification summary.

The full command-level TB/NRC inventory, wider scene snapping, native first-point
drag parity, textured previews, dense-map performance, broader authoring tools,
native platforms/input, screen readers and real-project acceptance remain open.
The production goal remains active; VibeStudio is pre-alpha.

### Pinned command discovery — 2026-10-19

The [command catalogue](level-editor-commands.md) is generated from clean
reference checkouts at TB `v2026.2`
(`4c756be1a54a28d921d18c77716a5f170bdc31d0`) and NRC
`449778b3840f8f2062daab15c87cb012f2ac945c`. It records 596 entries with source
paths, line ranges, fragment hashes, conditional guards and registration kinds:
179 TB actions, five TB tag/entity families, 301 NRC core names, 96 plugin tokens
and 15 plugin providers. Eight plugin providers are included in NRC's listed
plugin build target and seven are source-only; this does not certify builds.

There are 32 initial reviews: 26 Partial and six Planned. Clipping, components,
grouping, linked instances, wrapping/merging and selection cite existing
implementation/tests and describe their differences. TB sweep, linked extraction
and protected linked properties remain explicit gaps. The other 564 entries
have no parity status yet. Context menus, gestures, inspectors, preferences,
plugin sub-dialogues, formats and production workflows still need inventory and
acceptance. The first completion gate remains unchecked.

`scripts/level_command_inventory.py` reproduces the source scan, validates review
evidence/route references and regenerates the readable report. It ignores comments,
resolves adjacent literals and reviewed generators, preserves conditional branches
and rejects unknown expressions at supported sites. It reads references without
executing upstream code. Nine regression cases cover omitted/false registrations,
generated families, plugin constants and build classification, invalid evidence,
stale routes and report drift. Meson includes the scanner validation and regression
suite. The reference scan and GUI/CLI route check pass against the unchanged
application SHA-256
`fd4408d99dc912d38ee87676f1b05ab4863a41e36d187effc97a51bd822bd9b8`.

Evidence for this increment is in
`.agents/tmp/level-command-inventory/verified-summary.json`. This is discovery and
review progress, not a new editor tool or another run of the prior GUI/compiler
acceptance suites. Documentation, source-layout, credits and changelog checks
pass in seven focused Meson suites, including the nine scanner regression cases.
The offline guide is regenerated and the 22-page documentation site builds with
no broken links. The final scanner suites were repeated after tightening failed
registry-capture rejection. The full production goal stays active.

An existing documentation issue surfaced during the audit: the runtime
`editor scene` command summary omits its implemented `link`, `update-links` and
`unlink` operations. The detailed implementation and linked-group tests contain
those routes. The following sweep increment corrects that summary.

### Numeric face sweep — 2026-10-19

**Sweep Faces…** adds a material-aware numeric preview and `map sweep`, backed
by `core/level_sweep` and the cancellable placement service. It follows the
pinned TB 2026.2 behaviour for combined face-bound centres/normals, straight,
arc/helix and cubic Hermite S-bend stations, shortest rotation, world-axis taper,
affine iterations, integer rounding, source retention and current-material
faces with default mapping/flags. The first off-grid station stays exact;
collapsed/out-of-world sections are reported individually. No valid section
means no edit. Limits are 64 faces, 64 segments, eight iterations, 4096 attempted
sections and ±32768 units.

The multi-owner hull insertion service validates all native brush records before
adding one undo transaction. Original entity/group destinations survive and
linked copies update through the shared scene service. A regression exposed
history eviction discarding the parent edit before linked follow-ups folded
at a small undo limit. History now trims after propagation; sweep tests verify
exact undo and redo with a limit of one.

The dialog coalesces changes on a background worker and invalidates stale
previews immediately. It offers source, result and combined views with package
and staged material images. Apply checks the normal document/package/component
selection snapshot. Closing cancels preparation. Text and JSON CLI reports both
identify omitted sections, and invalid options preserve existing output files.
The related `editor scene` discovery summary now lists its implemented `link`,
`update-links` and `unlink` routes.

Seventeen distinct focused suites passed across this increment, covering sweep
core/CLI/UI, wrapping, placement jobs and UI, primitives, linked groups, scene
locks, map history, scene context/transfer and scene CLI. After the final source
changes, sweep and linked-group checks were repeated, followed by the five
map/scene history suites and staged-material UI acceptance. Coverage includes
analytic arc/S-bend coordinates and noncommuting affine repetition, four native
face dialects, source retention, multiple owners, locked destinations, linked
propagation, 512 sections, resource limits, cancellation, stale publication,
strict CLI arguments and save/reopen. Owned-widget renders at 100%/200% text,
high contrast and expanded RTL were inspected. No operating-system input,
screen capture or game launch was used.

`level_sweep_compiler_workflow.py` passes 99 steps for twelve cases: straight,
arc, S-bend and repeated transforms in classic, Valve 220 and brushDef maps.
VibeMap3 BSP/VIS/light output retains all 60 generated collision brushes and
matches independent cap-coordinate half-space oracles. Exposed destination
corners remain in render geometry; overlapping inner corners of the repeated
turn are correctly removed from the visible CSG surface. Original map objects
remain unchanged. Every PK3 validates and retains exact map, BSP and texture
payloads. This does not establish brushDef3 compiler or engine runtime acceptance.

The final executable SHA-256 is
`c354d506b5acd4d6e0220f3cc7454506e974bdf87348499e576e994b32e151d1`;
the external VibeMap3 compiler SHA-256 is
`cfb68f1cbfba42b17e4c02874a64d0ff4c8349327f4619a642a5f9d3b7174727`.
Evidence is retained in `.agents/tmp/level-sweep/`, including the final
verification summary, per-phase test results, `compiler/verified.json`,
runtime command registries, translation extraction and owned-widget renders.
All 48 catalogues extract from 1,624 files and 26,367 translation calls with
no unextractable literals or skipped catalogues. Runtime discovery contains
123 palette commands, 102 shortcuts without conflicts and 285 CLI routes.
Nine documentation/inventory/translation validation suites pass. The offline
guide is regenerated and the documentation site builds all 22 pages without
broken links; source layout, credits, changelog and whitespace checks pass.

The command catalogue now marks TB Sweep Tool and Perform Sweep **Partial**:
28 Partial, four Planned, 564 unreviewed. Native destination-cap manipulation,
grid-step scale shortcuts, full modifier parity, dense-map performance,
macOS/Linux and assistive-technology acceptance remain open. Numeric controls
do not close those interaction gaps. The broader production gates remain
unchecked and the goal stays active. No additional unrelated product defects
were found beyond the repaired linked-history and scene-help issues.

Automatic approval review rejected the non-recursive removal of the verified
empty `.agents/tmp/level-sweep/vibestudio-package-imports` directory with
“blocked by policy”. Its exact path and ancestors were checked for links;
the directory remains, and the deletion was not retried.


### Sweep viewport controls — 2026-10-19

**Sweep Tool** now binds one destination draft to all three plan views and the
level camera. Centre movement uses shared world-plane/grid constraints; the
camera admits Alt vertical movement, with live Shift axis and Ctrl absolute-grid
changes. Plans show their perpendicular rotation ring; the camera offers all
three axes. Rotations compose in world space. The scale handle follows the
rotated source arm, with nearest grid-plane snapping, uniform scale and a 0.01
minimum. Named decrease/increase commands use one grid step (one world unit
when snapping is off). The TrenchBroom profile installs **Y**; viewport Enter,
`[` and `]` defaults respect command overrides and explicit unbinding.

The compact modeless palette shares material, path, segment, iteration and
transform fields with the handles. Its optional textured source/result preview
uses the existing asynchronous placement/material services. The main views show
cap outlines and checked hull edges; pending caps are dashed. The viewport
readout and accessible descriptions report the sweep destination and validation.
Source manipulators yield while the tool owns the draft. Source selection stays
visible. Release keeps the draft; explicit creation applies one checked map
transaction. Escape cancels a gesture, then resets a transformed cap, then leaves
an identity tool. Map, selection, scene, package, profile and shortcut changes
invalidate the draft. The normal placement guard also rejects stale application
before the periodic closure check runs.

Focused checks exposed and fixed a projected-handle overlap: the scale label
could intercept the centre in a side view or perspective projection. A small
scale marker and an outward label now keep the centre usable. Invalid pointer
or captured-ring-ray proposals restore the initial destination on release.
The sweep readout also takes precedence over underlying component-editing hints.

Verification and retained evidence:

- Canonical Meson/Ninja build: `builddir/src/vibestudio.exe`, SHA-256
  `2df4cad9f8c8882bc2f4a133e05e5bd90e758b034e3414ed0d8396ee6d948bc1`.
- 29 distinct suites passed across implementation and validation phases,
  including sweep/core/UI/handle, component gizmo, profiles, placement jobs,
  linked groups, scene ownership/locks, CLI, documentation, credits, catalogue
  and translation checks. The final invalid-ring-ray guard also passed its
  focused executable check. Phase records preserve earlier failures and fixes.
- Semantic tests cover three plan frames and an independently projected
  perspective camera; relative/absolute grid movement, live constraints,
  quaternion composition, scale clamping/snapping, custom/unbound keys,
  cancellation, four actual view bindings, stale context, source immutability,
  explicit publication, exact undo and accessible readout restoration.
- Owned Qt renders cover the four-view draft and compact/textured palettes at
  100%/200% text size, high contrast and expanded RTL. No user input injection,
  operating-system capture or game launch was used.
- The expanded compiler fixture passes 123 steps: five transform/path cases
  across classic, Valve and brushDef maps, 72 swept collision sections, analytic
  cap containment and exposed render corners, retained source records, and
  byte-verified map/BSP/texture PK3 payloads. The compound case independently
  checks Rx(90) followed by world Rz(90), with uniform scale. Compiler SHA-256:
  `cfb68f1cbfba42b17e4c02874a64d0ff4c8349327f4619a642a5f9d3b7174727`.
- Runtime metadata: 127 palette entries, 102 default-profile shortcut entries,
  zero conflicts and 285 CLI commands. Profile tests separately verify the
  TrenchBroom **Y** binding. The command catalogue remains 596 entries with
  32 bounded reviews: 30 Partial, two Planned and 564 unreviewed.
- Translation extraction: 48 catalogues, 1,631 source files and 26,401 call
  sites, with zero unextractable literal calls or skipped catalogues. The manual,
  generated offline guide and documentation site checks pass.
- Evidence is under `.agents/tmp/level-sweep-handles/`, including
  `verified-summary.json`, phase results, `compiler/verified.json` and owned
  renders in `ui/`. Automatic approval review rejected removal of the empty
  task-local `vibestudio-package-imports` directory as **blocked by policy**;
  it remains untouched, with `cleanup-audit.json` recording the read-only check.

This increment remains **Partial**. Main-camera textured section previews,
complete native gesture and assistive-technology acceptance, dense real-project
performance, native macOS/Linux verification and brushDef3 compiler/runtime
acceptance remain open. The command inventory and production gates above are
not complete. No new unrelated application issue was found in these checks.

### Linked object correspondence — 2026-10-19

**Status: Partial.** Linked entities and their brushes/patches now carry persistent
object UUIDs scoped to the linked family. This is the foundation for protected
properties and extraction; those commands remain **Planned**.

- Automatic and explicit updates transfer correspondence through the actual
  copy-origin map. Deleting a child does not re-pair its neighbours by position;
  additions and partial duplicates get new identities. Complete group duplicates,
  save/reopen and rotated prefab transfers retain the correct pairs.
- Scene metadata version 4 stores and validates the mapping, including implicit
  entity children. Versions 1–3 remain readable. Legacy peers retain unknown
  correspondence until an actual copy/update establishes it; locked legacy peers
  remain unresolved. Invalid, stale or future metadata stays opaque.
- Linked creation and explicit updates now publish an isolated prepared document.
  A scene-limit refusal retains geometry, selection, revision, undo/redo and caller
  output. Regression testing found and fixed an existing one-slot history bug:
  intermediate commands are retained until the transaction is folded.
- **27 distinct suites pass** across 18 product checks and nine validation checks.
  Coverage includes shared sweep controls, scene/group GUI and CLI routes,
  prefabs, placement, object manipulation and documentation. The new identity
  fixture checks exact undo/redo, a locked peer with one history slot, mixed
  legacy/new correspondence, malformed metadata and a full-scene refusal.
- Verified application SHA-256:
  `438e90667a68a302641cbaab36275e0b712560bbbfc32ec0982f8fde55d20da0`.
  Runtime metadata remains 127 palette entries, 102 default-profile shortcuts,
  zero conflicts and 285 CLI commands. The catalogue remains 596 entries,
  32 bounded reviews (30 Partial, two Planned) and 564 unreviewed.
- Translation extraction passes for 48 catalogues, 1,635 source files and
  26,407 call sites, with zero unextractable literal calls or skipped catalogues.
  The manual, generated offline guide and documentation site checks pass.
- Evidence is in `.agents/tmp/level-linked-identities/`, including
  `verified-summary.json`, product/final validation results and the original
  history regression log. The initial validation failures (missing private-header
  manifest entry and catalogue wording assertion) were corrected and retained
  separately. No disposable task directories remain.

No new input control, game launch or screenshot was needed. Compiler acceptance
was not rerun for this metadata increment; the previous sweep compiler evidence
remains in `.agents/tmp/level-sweep-handles/`. Protected-property authoring,
extraction, nested linked groups, wider transform/conflict semantics, dense-map
performance, native cross-platform and assistive-technology acceptance remain
open. No unrelated application issue was discovered.

### Protected entity properties — 2026-10-19

**Status: Partial.** Linked entities now support protected values and protected
absence through the inspector, Add/Clear commands and three `editor scene`
operations. The full production goal remains active.

- Instance protection uses persistent object correspondence. Updates preserve
  each target's value or absence when either side protects a key, after spatial
  transforms, while retaining the target's flags. Unprotect/Clear restore an
  existing unprotected counterpart's value when one is available. Brush entities,
  multi-selection and all-protected peers use the same service.
- Metadata and values publish as one undo step, including a one-slot history.
  Locks, open-group context, invalid keys/values, incomplete legacy correspondence,
  the 4 MiB metadata bound and stale modal context refuse changes atomically.
  Version 5 persists protection, including standalone entities with no scene
  nodes; older schemas remain readable and malformed/future metadata stays opaque.
- Copy/paste, ordinary duplication, first linked-copy creation, prefab capture,
  target-name namespacing, insertion, unlinking and deletion/undo retain the
  intended flags. Dependency/build/package services still read native entity
  values and asset paths through the existing document services.
- Inspector checks expose Local/Shared/Mixed states, protected missing keys and
  a separate column from spawnflags. Semantic tests cover mixed selection,
  ordinary value editing, clearing, cancellation, stale selection and exact undo.
  A discovered Qt item-lifetime crash was fixed by deferring row reconstruction
  until its checkbox signal returns. Native user input was not controlled.
- **31 distinct suites pass** across product and validation phases, including
  sweep controls, linked groups, scene locks/context, prefabs, placement, shared
  command semantics, GUI/CLI, credits, docs and translations. Additional transfer
  checks verify new linked clones and protected prefab target names. Owned Qt
  renders cover 100%/200% text, high contrast and expanded RTL. Native keyboard,
  screen-reader and macOS/Linux acceptance remain unverified.
- Verified application SHA-256:
  `a183221b5f383d6f14a4fb73be474ac68687c294007264bfd7b32851185822c3`.
  Runtime metadata reports 129 palette entries, 102 default-profile shortcuts,
  zero conflicts and 285 CLI commands. The catalogue has 596 entries and
  32 bounded reviews: 31 Partial, one Planned and 564 unreviewed.
- Translation extraction processes 48 catalogues, 1,641 source files and
  26,461 call sites, with zero unextractable literal calls. The manual, generated
  offline guide and documentation site pass validation. Evidence is retained in
  `.agents/tmp/level-protected-properties/`, including `verified-summary.json`,
  phase reports, crash diagnostics and owned renders in `ui/`.
- Automatic approval review rejected cleanup of four task-local failed-test
  folders as **blocked by policy**. They remain untouched; `cleanup-audit.json`
  records their paths and the read-only link/content checks. No cleanup retry
  was attempted.

Linked extraction is the next grouping gap. Nested linked instances, broader
transform/conflict semantics, cross-editor metadata, dense real-project testing
and the full TB/NRC command/workflow audit remain open. Compiler acceptance was
not rerun for this metadata increment; the prior sweep compiler evidence remains
in `.agents/tmp/level-sweep-handles/`. No unrelated application issue was found.

### Linked group extraction — 2026-10-19

**Status: Partial.** A proper subset of an open linked group can now be extracted
across its complete family. The original instances keep their remaining contents,
and each selected counterpart becomes part of a fresh linked family. The full
production goal remains active.

- Each instance is cloned independently before pruning. Partial brush owners,
  protected values/absence, actual geometry, UVs, spatial frames, parent and
  visibility survive. Selecting an entity subsumes redundant child selections.
- The isolated transaction checks correspondence, ownership, affected locks,
  node limits and metadata size. Incomplete or disjoint ownership refuses the
  operation. One undo restores exact native bytes, selection and the original
  editing context, including a one-slot history. Success retains the parent
  context and selects the new source counterpart.
- **Extract Linked Groups** in command search and **Tools > Groups** uses the
  cancellable placement worker and normal stale-context guard. CLI
  `editor scene extract-linked` shares the service, dry-run, overwrite and safe
  publication paths and reports durable new-group UUIDs.
- Core fixtures cover whole/partial owners, world patches, rotated/mirrored
  counterparts, exact brush/patch geometry and UVs, local point-entity properties,
  independent subsequent updates to both families, prefab transfer, save/reopen,
  capacity/lock/correspondence/ownership refusals and late cancellation. GUI
  fixtures cover cancellation, stale selection, undo/redo and save at 100%/200%
  text with high contrast, expanded translation and RTL using owned Qt renders.
- **26 product suites pass**, including sweep controls, scene locks/context,
  linked correspondence/protection, prefabs, placement jobs/UI and shared command
  semantics. The generated compiler workflow passes **54 steps** across classic,
  Valve 220 and brush-primitive maps: six BSP/VIS/LIGHT builds preserve all
  compiled surface geometry, UVs and normals while changing two brush owners to
  four with their correct local values. Three PK3s validate their source/BSP bytes
  and generated texture dependencies. No game, native input or screen capture
  was used.
- Evidence is in `.agents/tmp/level-linked-extraction/`, including focused/edge/
  product reports, owned renders under `ui/`, and `compiler/verified.json`.
  The initial prefab test selected child groups through a closed parent; it was
  corrected to use their parent editing context. The compiler fixture resolves
  native selectors again after saving, because correspondence UUIDs rather than
  native object numbers are persistent. No unrelated application issue was found.
- A further isolation regression exposed baseline refresh of unrelated families
  during a staged compound edit. Refresh now covers only the two affected halves;
  unrelated pending state remains intact. The failing regression is retained in
  `isolation-before.log`, followed by the passing affected-suite rerun. Mixed
  brush/patch owners also retain exact patch controls/UVs and local properties.
- The ten documentation/catalogue/translation validations pass, giving **36
  distinct passing suites** with the product phase. Runtime metadata reports
  130 palette entries, 102 default-profile shortcuts, zero conflicts and 285 CLI
  commands. The 596-entry catalogue now has 32 Partial reviews and 564 unreviewed
  entries; no entry is claimed Available. The English plural check now has an
  explicit 180-second timeout after its default 30-second limit proved too short
  alongside full-tree extraction. English plural forms were regenerated.
- Translation extraction passes for 48 catalogues, 1,645 source files and
  26,489 call sites, with zero unextractable literal calls or skipped catalogues.
  Application SHA-256 is
  `79e1492afb9fc2fbcb2f57f035a8a6de61d8b2b606615ef3b30888039663f49f`.
  Compiler SHA-256 is
  `cfb68f1cbfba42b17e4c02874a64d0ff4c8349327f4619a642a5f9d3b7174727`.
  `verified-summary.json` records the final reports and source hashes.
- Automatic approval review rejected cleanup of the empty task-local
  `vibestudio-package-imports` and `vibestudio-plurals-7rylo_yn` folders as
  **blocked by policy**. They were left untouched; `cleanup-audit.json` records
  their exact paths and read-only checks. No cleanup retry was attempted.

Nested linked groups, broader transform/conflict semantics, cross-editor
metadata, dense real-project performance, native accessibility/platform testing
and the full TB/NRC command/workflow audit remain open. No production-readiness
gate is closed by this bounded increment.

## Repeat object commands — 2026-10-19

The shared service records moves, rotations, resizing, shears, mirrors and
ordinary duplication. It retains world pivots across selection changes, replays
with current texture lock and commits once, with cancellation, scene locks,
linked propagation and local protected values. Internal linked work is excluded
from recording. Clear invalidates in-flight preparation without changing map or
undo state. Undo/redo clear the sequence, Save As retains it, and reopen resets it.

The GUI exposes Repeat, Clear and Copy Repeat Recipe in Edit, Tools and command
search. TB keys Ctrl+R/Ctrl+Shift+R coexist with its Ctrl+B compiler binding;
other profiles leave the new keys unassigned. `map repeat` uses strict versioned
JSON from the GUI or CLI transform output, with selection/context, conversion,
dry-run and overwrite policies. The schema distinguishes moves from affine
transforms so native Doom/Hexen thing heights survive replay.

Tests also exposed and fixed native sector movement and repeat selection
boundaries after array insertion. Sector moves now carry unique boundary
vertices; preflight rejects missing targets, out-of-range coordinates and
collapsed lines before mutation. The existing smoke assertions were updated
from the former immovable-sector contract, with direct movement and atomic
failure/undo coverage. Profile-only shortcut descriptors must resolve to a real
implemented profile binding. The CLI documentation check also found and repaired
a missing `map sweep` entry in `CLI_STRATEGY.md`.

The two TB repeat actions now have bounded Partial reviews: 34 of 596 entries are
reviewed, 562 remain unreviewed, and none is claimed Available. Array, component
and other command families, large real maps, native input and accessibility,
macOS/Linux testing and the full parity audit remain open. See
[Repeat object commands](../LEVEL_REPEAT.md). Final validation and source/binary
hashes are recorded in `.agents/tmp/level-repeat/verified-summary.json`.


## Clipboard placement — 2026-10-20

**Status: Partial.** The shared paste service now resolves original-position,
explicit-offset, plane, camera-centred and view-ray placement before insertion.
TB's default paste follows the cursor and Ctrl+Alt+V retains original coordinates;
NRC keeps original-position paste and Shift+V centres the assembly at the camera.
Explicit commands are also in Edit, search and **Tools > Clipboard**.

The default profile's orbit view uses its visible focus-plane centre for
**Paste to Camera**. Cursor placement chooses the frontmost brush along the
orthographic line, including surfaces before its focus plane; a miss stays at
that depth. Perspective misses retain the 256-unit fallback. Ray targeting uses
existing visible brushes and cannot hit the incoming geometry. Point classes use
loaded bounds; brush-entity origin properties do not enlarge the assembly bounds.
The final orbit check exposed tiny trigonometric components at exact camera
poles; these are normalised to zero before selecting a grid direction/corner.

All targets share cancellable asynchronous preparation, stale-result refusal,
scene context and locks, native hierarchy transfer, texture lock and one undo.
CLI `map paste` exposes the same target modes, grid, definition bounds and scene
destination; `--orthographic` selects orbit ray semantics. Dry-run, overwrite and
strict malformed/duplicate/incomplete option checks preserve existing outputs.
Model and sound references, brush owners, patches and internal linked families
remain part of the shared authoring/build/package flow.

Evidence under `.agents/tmp/level-paste/` includes:

- **51 distinct passing suites**, combining the broad run with affected-suite
  reruns. Paste core fixtures cover four face dialects, bounds, target/filter
  rules, UVs, ownership, linked transfer, locks, cancellation at each phase,
  invalid inputs, exact undo/redo and save/reopen. Sweep handles/UI, component
  selection/transforms, groups, prefabs, repeat, placement and camera/profile
  suites cover shared-service regressions.
- Offscreen paste UI checks cover TB, NRC and the default orbit profile at
  100%/200% text, high contrast and expanded RTL, all plan projections, camera
  placement, cancellation, stale selection, exact undo and accessible tool rows.
  Actual Clipboard sections and individual rows were rendered by owned Qt
  widgets and inspected; no native input or OS screen capture was used.
- **85 CLI/compiler/package workflow steps** pass. Six BSP/VIS/LIGHT builds
  across classic, Valve 220 and brush-primitive maps match independent offset
  placements exactly: 12 compiled surfaces and two models per variant, including
  UVs, normals and entity ownership. Three PK3s validate their nine entries and
  byte-identical source/BSP payloads. Orbit ray targets additionally match
  independently calculated offsets. No game was launched.
- Runtime registration reports 136 palette entries, 106 shortcut descriptors,
  zero conflicts and 286 CLI commands. Four clipboard commands have bounded
  Partial reviews; the catalogue now has 38 Partial reviews and 558 unreviewed
  entries, with no entry claimed Available.
- The broad checks found a missing public-header install entry, now supplied.
  An unrelated workspace test still expected the camera clipper to force a plan
  view; its assertion now verifies the established maximised-camera behaviour
  and synchronised hidden plans. No unrelated application defect was found.

See [Clipboard placement](../LEVEL_PASTE.md) for exact rules and limitations.
`verified-summary.json` records the final test phases, translation validation,
compiler/application hashes, source hashes and temporary-file cleanup outcome.
TB face-clipboard auto-dispatch, complete foreign clipboard/group/entity-ID
compatibility, dense real maps, native input/screen readers and macOS/Linux
acceptance remain open, along with the rest of the command/workflow audit.
The full production goal remains active; no production-readiness gate is closed
by this increment.


## Face clipboard — 2026-10-20

**Status: Partial.** Normal paste now recognises plain `.map` face records before
object placement. The last record supplies attributes to picked faces or all
selected brush faces. TB normal copy exports picked faces, otherwise objects;
NRC and other profiles retain object copy. **Copy Surface** exports the inspected
face in every profile and captures its session source. Sampling remains local to
the session. TB face picks disable Cut so owner brushes cannot be removed.

Face attributes retain target contents, copy explicit surface flags/value, and
preserve omitted source fields. Valve targets keep their own UV basis and rotate
it about its normal by the requested angle change. Classic/Valve transfers keep
the target dialect; matrices transfer only to matrices. Raw paste leaves the
sampled source unchanged. **Surfaces > Face attributes** and CLI
`copy-surface --format map` / `paste-surface --mode attributes` share the service.
Normal GUI paste variants use this dispatch without moving or inserting objects.

Parsing and transfer run asynchronously with complete-input validation, bounds,
locks, cancellation, stale map/face-selection guards and one undo step. Review
found that the shared face writer could omit explicit zero fields on previously
unflagged targets; this now persists the explicit fields and exact undo restores
their absence. No package image lookup or AI connector is required for this mode.

Evidence under `.agents/tmp/level-face-clipboard/` includes:

- **37 distinct passing suites**, combining the 31-suite run with affected
  surface, viewport, map writer and metadata reruns. Coverage includes four face
  dialects, strict parsing, malformed UTF-8, optional/zero flags, oblique Valve
  basis rotation, cross-format limits, locks, stale plans, cancellation,
  save/reopen and exact undo/redo. Sweep core/handles/viewport, components,
  scene, prefab, repeat and object-paste regressions also pass.
- Semantic offscreen UI checks exercise TB, NRC and default profiles; normal
  Copy/Paste variants, explicit Surface paste, picked/whole-brush targeting,
  unchanged sampled source and stale picks on the same owner. Surface controls
  were rendered and inspected at 100%/200%, high contrast and expanded RTL.
  No OS input or screen capture was used.
- **33 CLI/compiler/package steps**, repeated against the retained acceptance
  executable after the writer fix. Classic, Valve 220 and brush-primitive inputs
  complete BSP/VIS/LIGHT; **24 compiled UV samples** match independent expected
  coordinates. Three PK3s validate seven entries each. No game was launched.
- Runtime registrations resolve all reviewed routes with zero shortcut conflicts.
  The command catalogue now has **40 Partial reviews and 556 unreviewed entries**,
  with no entry claimed Available. Translation extraction covers all 48
  catalogues, and English plural validation includes the new face-count status.

Concurrent modeller work briefly left an incomplete build entry, then resolved
it. Shared default Meson logs were also overwritten by another run; this task's
captured output retains the first/broad results, and later phases use distinct
log names. The final CLI/compiler acceptance uses an independently copied
executable in `verified-binary/`; hashes, source snapshot, phase records and
cleanup outcome are in `verified-summary.json`.

Full foreign clipboard/group/entity-ID conversion, optional face colours,
game-default surface semantics, a CLI object-copy/cut equivalent, dense real
maps, native input/screen-reader acceptance and macOS/Linux verification remain
open. The command/gesture/panel/workflow audit continues. See
[Face clipboard](../LEVEL_PASTE.md#face-clipboard) and its reference credits.


## Object clipboard ownership and cut — 2026-10-20

**Progress; goal remains active.** Copy now preserves a selected brush entity's
properties and only its selected children, including mixed brushes and patches.
Whole-owner selection preserves source child order without duplication. Complete
native groups and protected properties transfer; a partial wrapper cannot claim
its entire group. The audit also found that old Cut could copy a brush then
invoke component deletion. Cut now prepares whole-object deletion alongside the
clipboard in the shared worker, refuses component/clipper tools and active drags,
and validates context and clipboard ownership before publishing one undo step.
Cancellation and failed preparation leave both untouched. Clipboard acceptance
precedes map publication, including a second check for synchronous tool changes.

`map copy` and `map cut` now use the same core service, with strict selectors,
bounds, path validation (including file identity aliases), explicit overwrite and
dry runs. Cut commits its fragment
before the remaining map and reports `clipboardWritten` / `mapWritten`, including
late save failures. In-place map saving retains the normal exact backup. This is
a two-file workflow with explicit partial publication, not a filesystem transaction.
Model, audio and material references remain usable by existing dependency and
packaging services. No dependency or stack change was needed.

Evidence in `.agents/tmp/level-object-clipboard/verified-summary.json` records:

- **31 distinct passing regression suites**, including new core/CLI and semantic
  GUI clipboard suites plus sweep viewport/handles, group transfer/context,
  linked objects, protection, locks, prefabs, placement jobs, paste and face/surface
  clipboard regressions. New tests cover exact undo/redo, owner properties,
  cancellation, stale picks/clipboard, input bounds and late save failure.
- **45 CLI/compiler/package steps** across classic, Valve 220 and brush-primitive
  maps. BSP/VIS/LIGHT results match independently authored expected geometry,
  UVs and owner properties: 18 surfaces and three models in each variant. Three
  PK3s validate nine entries apiece. No game was launched.
- TB, NRC and default GUI profiles; owned widget renders inspected at 100%/200%
  with high contrast and expanded RTL. No OS input or screen capture occurred.
- 290 CLI registrations, 136 palette commands, 106 shortcuts and zero conflicts.
  Translation extraction passed across 48 catalogues with zero unextractable
  literals; English coverage contains 408 plural messages. Manual and CLI docs
  accompany the implementation; the offline guide is regenerated.

The NRC Copy selected-tree export was reviewed in the pinned `select.cpp` and
`map.cpp`. Worldspawn-property export and partial-child ordering differ and remain
explicit limitations. The catalogue now has **41 Partial reviews and 555
unreviewed entries**, with none Available. Foreign format/group/entity-ID
conversion, complete modal-tool equivalence, dense real maps, native
input/clipboard/screen-reader acceptance and macOS/Linux verification remain
open. Continue the command, gesture and panel audit against the pinned references.
No new unrelated product issues were found; concurrent modeller work is preserved.

The final CLI file-identity check also rejects hard links and case aliases of the
source. Its affected core/CLI suite passed again, and all 45 compiler/package
steps were repeated against the final retained executable. The fresh docs site
built with no broken links. Automatic approval review rejected deletion of the
generated docs site and empty package-import cache as “blocked by policy”; neither
was removed or retried. See `cleanup-audit.json` in the evidence directory.


## Brush-volume selection — 2026-10-20

**Partial.** The TB profile now tests each convex brush independently, including
SAT edge-cross separation, sampled patch grids and projected Tall footprints.
Inside, Touching and Tall consume source brushes even for an empty result. NRC
uses each brush's AABB: Inside removes all selected objects; Touching keeps the
selection and adds matches. Other profiles retain the combined-bounds query.
Closed groups are bounds-tested units; hidden/locked descendants and objects
outside the open group are excluded. Loaded entity bounds share the GUI/CLI
path, with a 16-unit unknown-class fallback.

Consuming operations use normal scene-lock/link-aware deletion, then restore the
outer command's source/result selection after any linked followers on undo/redo.
Preparation runs on the existing cancellable placement worker. The GUI rejects
stale map/selection/view/profile/filter/definition results and disables region
commands during component and clipper tools. TB keys are Ctrl+E, Ctrl+T and
Ctrl+Shift+E. CLI queries prepare privately; explicit --output uses the shared
conflict-checked document writer, with --dry-run, --overwrite, --definitions and
--context. Saved maps preserve remaining materials, owner properties and asset
paths for dependency review, compilation and packaging.

The bounded source audit and implementation details are in the
[manual](../manual/levels.md#select-by-brush-volume) and
[credits](../CREDITS.md#brush-volume-selection). Evidence is retained under
`.agents/tmp/level-region-selection/`; `verified-summary.json` records the final
binary, suite results, compiler workflow and UI captures. Core fixtures include
separate volumes, empty corners of rotated brushes, crossing and skew solids,
all Tall axes, sampled patch curvature/edge crossings, definition bounds, closed
and open groups, hidden/locked objects, linked deletion with a one-entry history
limit, cancellation, stale selectors and a 4,096-brush debug workload. GUI tests
cover TB/NRC/default commands, undo, tool-state updates, shortcut scope, stale
selection/filter/tool results and progress/cancellation accessibility.

Final evidence records **40 distinct passing regression suites**, including
sweep, scene/group/linked operations, document persistence, prefab, placement,
repeat and clipboard coverage. The final targeted run also verifies CLI group
context and actual selecting-phase progress at 100%/200%, expanded RTL and high
contrast. The 4,096-brush synthetic debug check completed in 325 ms in the focused
run; this is a local measurement, not a real-project performance guarantee.

**93 CLI/compiler/package steps** passed against the retained executable
`969cea95c6a5b81f8e5fd5689e9a6db18ac74d28e3ce5d4d4ecb65749dc60d45`.
Six policy/dialect variants (TB/NRC across classic, Valve 220 and brush primitive)
match independently written expected BSP geometry, materials and entities:
12 surfaces and two models each. Three PK3s validate nine entries each.
CLI docs validate 290 registrations; runtime metadata has 140 palette commands,
106 shortcuts and zero conflicts. Translation extraction passed for 48 catalogues,
1,689 source files and 26,928 call sites with zero unextractable literals;
English coverage contains 410 plural messages. The regenerated offline guide and
22-page documentation site validate. No game was launched and no OS input or
screen capture was used.

The catalogue now has **46 Partial reviews and 550 unreviewed entries**, with
none Available. The goal remains active. Complete native/foreign group/filter
behaviour, NRC model-derived bounds, TB game-specific Tall world-limit clipping,
native input/screen-reader acceptance, macOS/Linux and dense real maps remain
open. TB grid-edge patch matching has its native sampled-surface limitation;
NRC Tall and TB Partial Tall are explicit extensions. Next audit candidates are
Select Siblings/by source line, remaining topology/material/patch commands and
broader compiler/real-project acceptance. No new unrelated product issues were
found; concurrent modeller work was preserved.

Automatic approval review rejected cleanup of
`.agents/tmp/level-region-selection/docs-site` and
`.agents/tmp/level-region-selection/vibestudio-package-imports` as “blocked by
policy”. Both verified disposable targets remain; no deletion ran and neither
was retried. `cleanup-audit.json` records them as retry-prohibited.


## Selection history — 2026-10-20

**Partial.** Source review of TB v2026.2 SelectionCommand, CommandProcessor and
Map_Groups confirmed that object selection and group navigation belong in the
undo timeline without modifying map contents. VibeStudio now provides atomic
`changeLevelMapSelection` and `changeLevelSceneGroupContext` services, used by TB
plan/camera picks, object/scene lists, selection commands and group navigation.
NRC and default controls retain their previous selection-history policy.
Selection order, primary object and group creation destination replay together.

Saved-state checks ignore selection-only steps, retain equivalent save boundaries
through branching, trimming and folding, and keep unreachable/unsaved content
modified even when undo is exhausted. Invalid/no-op selections preserve redo.
Selection history uses a separate revision so geometry caches stay warm while
staged document replacements detect an away-and-back selection change. Ordinary
deletions now restore selection after their followers. Compound Doom history
counts each geometry-changing child/follower, restoring node validity on undo.

Evidence is retained in `.agents/tmp/level-selection-history/verified-summary.json`,
with compact logs in `test-reports/`, owned Qt renderings in `captures/`, the
tested executable in `verified-binary/`, and final CLI/compiler/package results
in `compiler-final/verified.json`. **48 distinct suites pass**, including sweep
geometry/viewport controls, clipper, component transforms/selection, object/face
clipboard, paste, groups/linked extraction, document save, Doom/UDMF and command
metadata. UI assertions now distinguish prior selection steps from the one content
transaction under test. The TB/NRC/default UI checks include 100%/200% text, high
visibility, expanded RTL, accessible history state, list activation, profile
switching and rejection of a worker after a selection round trip. A debug
4,096-brush selection/undo/redo took 11 ms. No OS input, screen capture or game
launch was used.

The retained executable passed **93 CLI/compiler/package steps**: six TB/NRC ×
classic/Valve 220/brush-primitive variants match independently constructed BSP
geometry, materials and entities, each with 12 surfaces and two models. The
workflow also validates three PK3s. CLI documentation covers 290 registrations;
runtime metadata has 140 palette entries, 106 shortcuts and zero conflicts.
Translation extraction passed for 48 catalogues, 1,698 source files and 26,989
call sites with no unextractable literals; 410 English plurals and the 22-page
docs build validate. The exact binary and compiler hashes are in the summary.

The catalogue has **48 Partial reviews, 548 unreviewed entries and no Available
entries**. The goal remains active. Face/component selection history, visibility/
filter history, command collation and Select Siblings/by Line Number remain open,
alongside the broader authoring and native/cross-platform/real-project gates.
The CLI has no persisted undo session; its object queries remain transient.
A transient link failure in concurrent modeller joint-editor work cleared during
verification; that work was preserved. No additional unrelated product defect
was identified.

Automatic approval review rejected cleanup of four verified disposable directories
as “blocked by policy”. No deletion ran. Under `.agents/tmp/level-selection-history/`,
`docs-site`, `vibestudio-package-imports`,
`level_clipper_ui_smoke_test-GwrMWs` and `level_clipper_ui_smoke_test-uWImrG`
remain and are retry-prohibited; `cleanup-audit.json` records the checks and refusal.

## Hierarchy and source-line selection — 2026-10-20

**Partial.** The resumed goal made verified progress. TB v2026.2's Select Siblings
and Select by Line Number now have shared core, GUI and CLI routes. Sibling
queries traverse brush-entity ownership and scene parents, preserve layer and
open-group boundaries, and select closed groups atomically. Independently
assigned child memberships cannot bypass a group's exclusions or select locked
geometry through an entity wrapper. Source-line queries handle brush and patch
spans, point entities and brush-entity property fallback. Invalid lists are
rejected atomically; localised decimal digits are accepted. Lines refer to the
loaded source snapshot through edits and saves, with new spans acquired on
reopen. This reference is explicit in help, accessibility descriptions and CLI
JSON. Foreign TB metadata and group-bracket positions remain incomplete.

Both commands use cancellable preparation, view/filter/context guards and the
profile's selection-history policy. TB Ctrl+B selects siblings; its compiler
binding is Ctrl+Alt+B. Menus, Tools > Select and command search expose both
commands in all supported profiles. CLI queries return stable selectors and a
source hash without writing a map or persisting selection. The manual documents
differences from native TB input validation and VibeStudio's scene representation.

Evidence is retained at `.agents/tmp/level-object-selection/verified-summary.json`:

- **36 distinct suites pass**, including new core/UI selection cases, command
  metadata, selection history, scene locks, groups/linked objects, placement,
  clipboard/paste, prefabs, repeat, clipper, component selection and all three
  sweep geometry/viewport suites. `test-reports/` preserves compact outputs.
  Initial UI assertions were corrected to test saved-content equivalence rather
  than require the literal `saved` state in profiles that keep `clean`. A command
  metadata failure exposed redundant shortcut descriptors; their removal keeps
  normal profile/palette routing and user remapping intact.
- TB, NRC and default profiles pass at 100% and 200% text scaling, including
  high visibility, expanded RTL, localised numeric input, accessible roles,
  mnemonic/default controls, cancellation and stale selection/filter rejection.
  Owned Qt dialog renderings are in `captures/`; no OS input/capture was used.
- A debug 16,000-brush sibling query took **251 ms**. Separate 100,000-brush GUI
  cancellation checks produced 273/281 heartbeats and maximum gaps of **14/34 ms**
  at 100%/200% scale. Both cancelled without publishing selection or history.
- The retained executable in `verified-binary/` passed **86 CLI acceptance steps**
  across classic, Valve 220 and brush-primitive maps, including group/context/
  lock/filter rules, strict option diagnostics and three source-line-to-move
  handoffs. Query source bytes and hashes remain unchanged. Exact results and the
  executable SHA-256 are in `cli-final/verified.json` and the summary.
- The runtime registry validates **293 CLI commands**, 142 palette entries and
  106 shortcuts with zero conflicts. Translation extraction passes for 48
  catalogues, 1,712 source files and 27,174 call sites with no unextractable
  literals; 413 English plurals validate. The offline guide is current, the
  22-page site builds, credits validate and the full worktree diff check passes.
  The final site preview is retained in `documentation-preview/`; the offline
  guide was regenerated after concurrent model manual updates.

The catalogue now has **50 Partial reviews, 546 unreviewed entries and no
Available entries**. The original production-grade goal remains active.
Face/component selection history, visibility/filter history, full native
selection semantics, foreign scene metadata, native/cross-platform acceptance
and real production-map work remain open alongside the rest of the TB/NRC audit.
Doom/UDMF hierarchy/source-line queries are not part of this increment.

Unrelated work was preserved: a transient link failure while concurrent modeller
skinning symbols were being added resolved on the later build. Its missing
`model skinning` CLI reference entry was added from registered help, restoring
full documentation validation. No additional unrelated product defect was found.

Automatic approval review rejected the initial bulk documentation rewrite before
execution; explicit reviewable file patches completed those edits. It also
rejected cleanup of `docs-site` and the superseded `cli` fixture directory under
`.agents/tmp/level-object-selection/` as “blocked by policy”. Both were verified
disposable and link-free; no deletion ran, and neither was retried. They remain
retry-prohibited in `cleanup-audit.json`. Optional cleanup does not block the goal.

## Temporary visibility — 2026-10-21

**Partial.** Hide Selection, Show All Hidden and Isolate Selection now use shared
document-owned session state across the plan panes, camera, scene selection and
headless queries. The TB profile records visibility and selection as one
non-content history step. NRC and default profiles keep these changes outside
undo while invalidating stale prepared work. Saving, native map bytes, scene
metadata and Doom node validity remain unchanged. Show All clears session hiding;
persistent scene visibility and filters retain their separate controls.

Closed groups hide as one selection unit. Isolate includes locked objects and
objects outside the open group, keeps an explicitly selected brush entity's
children, and keeps a selected child's owner without revealing its siblings.
Object-list selection reveals the chosen object and required ancestors while
preserving independently hidden children. Doom isolation keeps the selected
vertex, line, sector or thing visible; a vertex retains incident geometry, and
lines retain their required endpoints and sector references. Structural undo and
redo remap current hidden identities, including compacted Doom indices and later
transient changes to inserted or deleted objects.

All object visibility commands use the cancellable placement worker and guarded
publication. They are disabled during incompatible component, clipping, drawing
and surface workflows. Context-menu shortcut hints follow the active profile and
user remapping. `map visibility` accepts explicit selection and hidden selectors,
open-group context and WAD map names, returning a session result and source hash
without writing the map. Its results feed selection queries and normal authoring.

Evidence is retained at `.agents/tmp/level-visibility/verified-summary.json`:

- **49 distinct suites pass**, including visibility core/UI, plan selection,
  selection outlines, geometry caching, object/region/history selection, scene
  locks and groups, clipboard/paste, prefabs, placement, repeat, clipper and all
  three sweep suites. The broader run found and corrected entity selection that
  revealed independently hidden children. The outline test now reads the current
  high-contrast palette instead of assuming an obsolete magenta highlight; its
  independent outline/dash-coverage checks remain intact.
  After correcting Doom vertex isolation, four focused visibility and Doom
  preview suites also pass against the final build.
- TB, NRC and default profiles pass at 100% and 200% text scaling, including
  high contrast, expanded RTL, shared four-view state, object-list reveal,
  profile history, tool guards, cancellation and stale selection rejection.
  Owned Qt renders show the isolated brush in all four views. These checks use
  semantic widget methods and Qt render targets; no OS input/capture is used.
- A debug 16,000-brush isolate and separate 100,000-brush GUI cancellation cases
  are measured in the summary and `captures/dense-visibility-*.json`. Cancellation
  preserves the live document and exposes accessible phase, progress and Cancel
  controls. The final run records **161 ms** for isolation and **13/14 ms** maximum
  timer gaps at 100%/200% scaling. These synthetic fixtures do not establish
  real-map performance.
- The retained executable passes **102 CLI acceptance steps** across classic,
  Valve 220 and brushDef, plus native Doom, Hexen, Quake, Quake II and Quake III
  fixtures. Tests cover strict option errors, groups/context, owner/child
  isolation, explicit WAD map names, every Doom primitive selection and three
  visibility-to-selection-to-move handoffs. Query source bytes and hashes remain
  unchanged. `cli-final/verified.json` records the executable SHA-256.
- The runtime catalogue validates 295 CLI commands, 142 palette entries and 106
  shortcuts with zero conflicts. Translation extraction covers 48 catalogues and
  1,728 source files with no unextractable literals; 412 English plural messages
  are refreshed. The offline guide, 22-page documentation site, credits and full
  worktree whitespace check pass. Compact test outputs are in `test-reports/`.

The command catalogue now has **55 Partial reviews, 541 unreviewed entries and
no Available entries**. The production-grade goal remains active. Full TB
inherited/forced container states, temporary hiding inherited by future group
children, component-mode hiding, filter history, native input, cross-platform
and real-project acceptance remain open alongside the wider authoring audit.

The 200% expanded-RTL capture at 1600 × 1000 exposes existing transform-inspector
clipping and crowded viewport/status text. These were recorded in the accessibility
document; the transform fields are addressed in the next increment below, while
viewport/status crowding remains open. Concurrent modeller changes temporarily
caused source-encoding and Qt-iterator build failures; later builds passed with
that work preserved. Its missing `model weight-paint` CLI reference entry was
added from registered help, restoring full CLI documentation validation.

Automatic approval review rejected cleanup of four verified disposable directories
under `.agents/tmp/level-visibility/` as “blocked by policy”: the superseded `cli`
fixtures, `level_visibility_ui_smoke_test-kbWLSk`,
`level_visibility_ui_smoke_test-pVFUCX` and the empty `vibestudio-package-imports`
cache. No deletion ran and none was retried. They remain retry-prohibited in
`cleanup-audit.json`; this optional cleanup does not block the active goal.

## Responsive transform inspector — 2026-10-21

**Partial production-gate progress.** The numeric Centre/Size inspector now keeps
the same native editors in a width-dependent layout. Wide panes use the compact
matrix; narrow panes stack axis rows and put labels above fields when necessary.
It measures the current font, style and locale instead of forcing three numeric
fields into ignored size hints. Reflow retains pending text, selection focus and
Centre X/Y/Z then Size X/Y/Z tab order. The normal shared authoring services still
perform the edit and undo; no CLI, map format, history or settings schema changes
are required.

Shared studio sidebars now retain a horizontal scroll fallback if a native
control's minimum width exceeds an extremely narrow pane. This scrollbar has
a translated accessible name/description and native keyboard focus, and each
numeric editor has a description and buddy label. The fallback benefits other
studio surfaces using `SidebarPage` without duplicating the level editor layout.

Evidence is retained under `.agents/tmp/level-inspector-layout/`, with the final
result in `verified-summary.json`:

- `level-transform-layout-ui-smoke` covers TB, NRC and default profiles at
  100%/200%, widths 320/420/560/1000, expanded RTL, Arabic numeric formatting and
  live 125%/150%/175%/200% scaling. It verifies complete native numeric size hints,
  caption/note bounds, horizontal reachability at extreme widths, accessible
  controls, focus/tab order, preserved pending text, one authoring commit and
  exact native-byte undo. It checks real widgets and document state rather than
  duplicating layout calculations.
- Six related suites pass: numeric transforms, temporary visibility, sweep
  controls, shared inspector layout, sidebar metadata and model weight transfer.
  The latter checks the adjacent compiler fix described below. The focused
  layout/inspector tests are repeated after adding the named, focusable scrollbar.
- Owned 100% and expanded 200% RTL renders were inspected. The compact matrix and
  stacked fields fit the ordinary narrow sidebar; longer content remains
  vertically scrollable. No OS input, mouse/key event injection, screen capture
  or game launch is used. Native input/screen-reader and macOS/Linux checks are
  still required.
- The manual, setup/accessibility records, changelog, generated offline guide,
  translation catalogues and documentation preview are updated. Runtime CLI and
  command-catalogue checks are recorded with the executable hash in the summary.

The prior turn was verified progress; this turn closes the observed transform
field clipping defect. The broad production objective remains active, with
**55 Partial command reviews and 541 unreviewed entries**. Crowded viewport/status
text, remaining authoring parity, native profile acceptance and real production
maps remain open; this increment does not establish whole-shell accessibility
or production readiness.

An unrelated concurrent modeller build failure used `.a`/`.b` on edge indices
stored as `std::pair<int,int>`; correcting those accesses to `.first`/`.second`
restored compilation. A newly registered modeller UI test was temporarily absent
while that work was being written; it appeared before the successful rebuild.
Concurrent modeller changes were preserved.


## Inherited temporary group visibility — 2026-10-21

The native reference distinguishes existing members from newly authored nodes:
TB's `Node::visible` inherits, while `Map_Nodes::addNodes` explicitly ensures
new nodes are visible. The previous temporary-visibility gap description about
future group children is superseded by this distinction.

The shared session state now supports Inherited, Hidden and Shown for object
identities and group UUIDs. TB Hide targets a closed group, resets shown
descendants and preserves independently hidden children. Existing members moved
into that group inherit hiding; new authoring creates shown exceptions in the
same content undo step. Isolate retains selected groups, ancestors and empty
descendants. Object-list activation respects closed-group selection units;
individual child reveal occurs inside an open group and preserves hidden
siblings. Show All resets both sets, including shown-only sessions. Structural
undo/redo retains removed states and remaps both hidden and shown identities.

All plan panes, camera preview, selection services and `map visibility` use the
same resolver. The CLI accepts `--policy flat|inherited`, `--shown`, and group
UUIDs in supplied states, rejects contradictory states and remains query-only.
Cache keys include shown overrides; hidden owner labels cannot cull shown child
geometry. Explicit child membership does not turn a required owner into a
selected group during isolation. Saved scene visibility and filters remain
separate masks. NRC/default still hide current group members, while authoring
reveal is a documented shared adaptation.

Validation evidence for this increment is retained under
`.agents/tmp/level-inherited-visibility/`. The goal remains active and the
catalogue remains 55 Partial reviews, 541 unreviewed and zero Available entries.
Layer-transition downgrades, copied visibility attributes, direct empty-group
controls, component-mode hiding, exact NRC creation semantics, native input,
cross-platform and real-project acceptance remain open. Existing viewport/status
crowding also remains open.

Validation: **50 distinct suites pass**, including visibility core/UI, scene
locks and groups, selection/history, rendering, sweep controls, clipper,
clipboard, prefabs, placement, repeat and Doom/UDMF. **113 CLI workflow steps**
pass on classic, Valve 220, brushDef and native Doom/Hexen/Quake fixtures.
The corrected offscreen visibility suite passes in 111.34 seconds across three
profiles, 100%/200% scaling, high visibility and expanded RTL. Its first run
incorrectly expected individual reveal inside a closed group; opening the group
corrected that test setup. An independent-membership isolation regression also
prevents keeping an owner from selecting its entire closed group.

All **297 registered CLI commands** have documentation. Extraction checks 48
catalogues, 1,747 source files and 27,546 translation calls with no unextractable
literals; every catalogue contains the new visibility strings. The retained
binary, compact test reports, CLI results and source fingerprints are in
`.agents/tmp/level-inherited-visibility/verified-summary.json`. Concurrent IK
work briefly lacked a registered test source and a CLI strategy entry; both
are now resolved. A concurrent French catalogue write completed, and isolated
extraction then passed.

Automatic approval review rejected cleanup under
`.agents/tmp/level-inherited-visibility/` as **blocked by policy**. Nothing was
deleted: two failed-run Qt test directories, the empty package-import directory,
and twelve generated map/WAD/settings files remain. Exact paths and the
zero-link audit are recorded in `cleanup-audit.json` and `cleanup-result.json`.
No retry or alternate removal was attempted.


## Visibility through layer transfer and duplication — 2026-10-21

The pinned TB `Map_Nodes::reparentNodes` resets Shown states for nodes whose
containing layer changes. Scene moves, assignments, removals and reset now apply
that rule to existing object and group identities, preserving explicit child
memberships. The scene command filters selection after the visibility change
and restores both on undo. Transient Hide wins over restoring a shown exception;
redo captures the current states so a later Show All remains respected.

Ordinary duplication, arrays and linked-copy creation copy explicit attributes
through object-origin maps and newly allocated group UUIDs. Insertion reveals
roots while retaining independently hidden descendants. New empty groups also
receive a shown exception inside a hidden destination. The same copy path
covers binary Doom/Hexen and UDMF. Linked propagation is deliberately distinct:
TB `LinkedGroupUtils::cloneAndTransformRecursive` constructs inherited children,
so an instance update does not spread source visibility exceptions or force
children visible inside a hidden target group. All transitions remain within
the original authoring undo step; no session flag enters portable metadata.

`map visibility` now accepts read-only Inspect, Move, Assign and Duplicate
previews with supplied hidden/shown states. They call shared scene/insertion
services, return the resulting scene and preserve source bytes and hashes.
This provides headless coverage of session-only transfer behaviour without
creating a native file format for temporary visibility.

The goal remains active. Saved scene visibility still masks shown exceptions;
direct empty-container visibility controls, component hiding, exact NRC creation
semantics, native input, cross-platform and real-project acceptance remain open.
The command catalogue remains Partial. Evidence for this increment belongs in
`.agents/tmp/level-visibility-transfers/`.

Validation passed: **50 regression suites and 125 CLI workflow steps**, followed
by an additional core run covering layer removal and scene reset. The viewport
suite passed TB, NRC and default profiles at 100% and 200% scale, including high
contrast, expanded RTL strings, all plan panes and the camera. Dense cancellation
recorded 11 heartbeats with a 13 ms maximum gap at 100%, and 10 heartbeats with a
5 ms maximum gap at 200%. The final 16,000-brush core isolation took 224 ms.
The retained binary, compact results and source fingerprints are in
`.agents/tmp/level-visibility-transfers/verified-summary.json`.

The offline manual and 22-page documentation site build. All **298 registered
CLI commands** validate against documentation, and isolated translation
extraction reports 48 catalogues, 1,759 source files and 27,705 translation calls
with no unextractable literals. Concurrent modeller curve registration briefly
preceded its implementation/test sources; the completed canonical build passed.
A missing `model curves` CLI strategy entry was added from registered help.
The first CLI transfer run exposed the existing shared-line duplication
restriction in its compact fixture; the line-separated fixture passes. That
restriction and existing viewport/status text crowding remain open.

The next visibility review has a concrete upstream difference: pinned NRC's
[`scene::Node`](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/libs/scenelib.h)
starts with visible exclusion flags, and its
[`BrushNode` copy constructor](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/brushnode.h)
constructs a fresh node. These GPL-2.0-or-later references were inspected on
2026-10-21; no code was copied. Complete entity/patch clone review and a
profile-specific policy remain to be implemented. Brush entity conversion also
uses delete/paste paths and needs a separate hierarchy/visibility identity audit.

Automatic approval review rejected cleanup of this increment's `docs-site` and
empty `vibestudio-package-imports` directories as **blocked by policy** before
execution. Both remain under `.agents/tmp/level-visibility-transfers/`; exact
paths, zero-link audits and the rejection are recorded in `cleanup-audit.json`
and `cleanup-result.json`. No retry or alternate removal was attempted.

## Profile-specific insertion visibility — 2026-10-21

The previous increment made verified progress. The pinned NRC review continued
through brush, patch, brush-entity and point-entity constructors: each creates a
fresh node rather than copying exclusion flags. `LevelInsertionVisibility` now
selects fresh inherited states for NRC and preserved attributes with root reveal
for TB/default. New geometry, empty groups, paste, prefab insertion, linked-copy
creation, duplicate, arrays and repeated duplication share the document policy.
Existing identities still remap, and linked replacement always inherits.

The shell supplies the policy on load/adoption and profile changes. Changing it
advances the session revision without dirtying contents or altering existing
hidden/shown states, undo, redo or repeat commands. Pending authoring checks reject
the old revision; completed undo/redo restores recorded states independently of
the current policy. The controls reference has a translated row describing new
and duplicated objects. `map visibility --profile ID` uses the same policy and
derives its default flat/inherited hide mode from that profile; explicit
`--policy` only overrides hiding. JSON reports both choices.

The command review now includes ordinary TB Duplicate and NRC CloneSelection as
**Partial**, bringing the catalogue to **57 Partial, 539 unreviewed and zero
Available** out of 596 entries. Their visibility differences have direct
GUI/core/CLI coverage; duplication is not yet exact: the GUI currently nudges
right/down for all profiles, while pinned TB and NRC default to no displacement.
NRC's optional post-clone nudge, unique-target cloning, source name/target rules
and component-mode eligibility remain to be completed or reviewed. A test also
confirmed that multi-copy arrays currently do not record Repeat history.

Saved-layer/filter equivalence, direct empty-container controls, component
hiding, native platform interaction and real-project acceptance remain open.
Evidence for this increment is retained under
`.agents/tmp/level-visibility-profiles/`.

The follow-up culling review confirms that NRC's `ForEachVisible` rejects hidden
parents and their descendants. It also exposes a remaining owner-state gap:
`HideSelectedWalker` stores a hidden flag when every child is hidden, while
VibeStudio currently derives the owner label from its children. This can differ
when inserting a later child and requires its own profile-aware hide policy.

Validation passed: **51 distinct suites and 135 CLI workflow steps**. The expanded
group-clone test exposed an application defect: ordinary plan/camera picks used
the Objects list's select-and-reveal path, unhiding independently hidden group
members. These intents are now separate. The corrected offscreen suite passes
TB, NRC and default controls at 100%/200%, high contrast and expanded RTL;
explicit Objects-list activation still reveals and records TB selection history.
The subsequent 50-suite regression run passes sweep, clipper, selection/history,
clipboard, grouping, prefabs, placement and Doom/UDMF workflows.

The 100,000-brush cancellation checks recorded 11/10 heartbeats with maximum gaps
of 12/7 ms. All **299 registered CLI commands** have documentation. The offline
guide and 22-page site build; isolated extraction checks 48 catalogues, 1,766
source files and 27,758 translation calls with no unextractable literals.
The runtime catalogue check caught an unregistered offset-dialog reference;
the duplicate reviews now cite the verified public shortcut route. Compact
reports, owned-widget renders, CLI replay inputs and the retained binary are in
`.agents/tmp/level-visibility-profiles/verified-summary.json`.

Existing viewport/status/inspector crowding at 200% expanded RTL and the
shared-line text-map duplication restriction remain unrelated open issues.

Automatic approval review rejected cleanup of two failed-run Qt fixture
directories and the empty package-import cache as **blocked by policy** before
execution. Nothing was deleted or retried. Exact retained paths and zero-link
audits are in this increment's `cleanup-audit.json` and `cleanup-result.json`.

## Duplicate placement and NRC object mode — 2026-10-21

The preceding visibility increment made verified progress: 51 suites and 135 CLI
checks passed. This increment addresses the next pinned-source difference. TB
and NRC now default to copying at the original coordinates. The optional nudge
is one grid step right/down in the active plan, including when the camera has
focus. `editing.nudgeAfterDuplicate` is a per-profile preference on the existing
gesture dialog's new Editing tab, with CLI, reset and portable import/export.
NRC disables both duplicate actions in component modes; its ordinary node-copy
path remains unchanged. Pending placement rejects preference changes, while
completed undo/redo and Repeat use recorded geometry and offsets.

New owned-widget coverage exercises three profiles at 100%/200%, all plan axes,
camera focus, both policies, exact undo/redo, Repeat and stale-preview rejection.
Gesture core/CLI coverage checks default isolation, aliases, invalid values,
dry-run and import/export. Evidence belongs under
`.agents/tmp/level-duplicate-placement/`.

Full native target/name behaviour and CloneSelectionAndMakeUnique remain open,
as do array Repeat, native platform interaction and real-project acceptance.

Validation passed: **54 distinct suites and 135 CLI workflow steps** on the
retained application. The new duplicate viewport suite passed in 110.34 seconds;
the subsequent 52-suite regression run also passed the gesture layout/accessibility
checks and visibility, sweep, clipper, selection/history, groups, prefabs, clipboard,
placement and Doom/UDMF tests. All **299 registered CLI commands** have documentation.
The offline guide and 22-page site build. Isolated extraction checks 48 catalogues,
1,774 source files and 27,874 translation call sites with zero unextractable
literals; all catalogues include the new messages. The tested binary, source
fingerprints and compact reports are in
`.agents/tmp/level-duplicate-placement/verified-summary.json`.

Automatic approval review rejected removing this increment's verified empty
package-import cache as **blocked by policy**, before execution. The directory
remains; no retry or alternate removal was attempted. `cleanup-result.json`
records the path and zero-file/link audit. Useful replay inputs, widget renders,
the documentation site and logs remain as evidence. Existing 200% expanded-RTL
viewport/inspector crowding and shared-line text-map duplication restrictions
remain open.

The next NRC clone review has a concrete ownership requirement:
`CloneSelected(true)` clones selected children into a new brush entity when their
owner is only partly selected. `Map_mergeClonedNames` then groups cloned name and
target-reference properties, chooses unused numeric suffixes against the map's
namespace, and redirects those cloned properties together. This differs from
the prefab path's assembly prefix and needs its own operation, CLI route, Repeat
record and Shift+Space binding. The pinned `radiant/select.cpp`, `radiant/map.cpp`,
`libs/uniquenames.h` and `plugins/entity/namekeys.h` were inspected; no implementation
was imported in this increment.

## NRC unique duplication — 2026-10-21

**Status: Partial; goal active.** `CloneSelectionAndMakeUnique` now has a separate
authoring operation, **Duplicate, Make Unique**, NRC **Shift+Space**, and
`map duplicate --make-unique` (`map clone` is the duplication alias; `map copy`
remains the clipboard command). Ordinary duplication retains **Space**.

The new mode copies only selected children into a fresh brush/patch entity owner,
or all children of an explicitly selected entity. World primitives keep their
world owner. It moves a partial owner's origin with the copied assembly without
transforming unselected siblings. Copied `targetname`, `target`, `killtarget` and
integer-suffixed `target` properties share a deterministic namespace remapping.
Outgoing references participate even without a copied destination. Suffix
allocation handles zero padding, Unicode, long names and arbitrarily long decimal
strings without integer conversion or fixed buffers.

The shared placement worker retains texture-lock, cancellation, scene-lock,
visibility and publication guards. Partial owners cannot claim a whole closed
group; complete selected groups follow normal group copying. Undo/redo restore
the exact native bytes and selection. Single-copy Repeat records
`duplicate-unique` and allocates names against the current destination on replay.
Unique CLI arrays reserve each previous copy and count new partial owners in the
existing resource budget. Strict CLI parsing rejects unknown/repeated options,
ambiguous input paths and malformed flag values before publication.

Edit, the level tools, context menu and command search expose the operation.
It uses the same per-profile nudge preference and requires object mode on every
profile. Doom maps retain ordinary thing duplication and reject unique mode.
The new operation preserves shared project assets, validation, save-as, compiler
and package handoffs; it neither generates assets nor needs AI. The GUI offset
preview still offers ordinary duplication, and multi-copy arrays are not recorded
by Repeat. Native physical input, assistive technology, macOS/Linux and large
real-project acceptance remain open. Existing 200% expanded-RTL crowding and
compact shared-line text-map duplication limits also remain open.

The catalogue now records **58 Partial / 538 unreviewed / 0 Available**, from
596 entries. Evidence for this increment is retained under
`.agents/tmp/level-clone-unique/`; the final build/test report records the exact
verified application and source hashes. The next related comparison is NRC's
entity connection pair: `EntitiesConnect` requires exactly two picked instances,
while the current VibeStudio command connects multiple selected sources to the
last entity; NRC also registers `EntitiesKillConnect` separately. These remain
unreviewed until their selection, naming and game-specific behaviour is checked.

Verification: **58 distinct suites passed**, including the expanded unique core/
CLI suite and the three-profile UI suite at 100%/200% (114.59 seconds). Sweep
core, handles and UI, clipper, groups, linked objects, visibility, clipboard,
prefabs, Repeat, placement and native Doom regressions are included. The registry
test now resolves camelCase profile bindings before comparing canonical command
IDs. CLI tests additionally cover unique recipe export, dry-run, save/reopen and
replay, as well as `--input`, equals selectors and flag ordering. All 48 catalogues
contain the new messages; source extraction reports no unextractable literals,
412 English plurals pass, 300 registered CLI commands are documented, and the
22-page documentation site builds. `verified-summary.json` and
`verified-tests.json` preserve the exact results and hashes; the retained
application SHA-256 is
`169ff533d7c40221c21b8247c9087825c46c9402c8eeb85f43865d76d90505b9`.

Automatic approval review rejected removal of the verified empty
`vibestudio-package-imports` test cache as **blocked by policy**, before execution.
It remains at the evidence location; `cleanup-audit.json` and
`cleanup-result.json` record the zero-child/zero-link check and rejection. No
retry or alternate removal was attempted. The useful site, logs, registries and
verified application are retained.

## NRC entity connections — 2026-10-21

**Status: Partial; goal active.** Connect now follows NRC's exact two-instance
selection and source-name priority in that profile. Primitive picks resolve
their entity owner, including worldspawn. An existing source `target` replaces
the destination's name; otherwise its name is reused or a classname-based name
is allocated against the NRC namespace. Names remain case-sensitive and retain
whitespace. Other profiles preserve the existing multi-source adaptation.

KillConnect is a separate action using `killtarget`, enabled for Quake, Nexuiz
and Xonotic game context. NRC binds Ctrl+K and Shift+K respectively. The current
project's game wins, then the selected installation, then built-in map evidence;
engine-family labels are not used as game identities. CLI `map connect` and
`map connect-entities` accept `--profile`, `--kill-target` and `--game`, retain
Save As and dry-run protection, and expose the resolved owners and naming mode.
Strict option parsing rejects ambiguous paths, mixed selector spellings and
invalid/repeated options before publication.

The shared transaction respects locks and open-group boundaries, propagates
linked content while retaining protected instance properties, preserves
selection, and records one exact undo step. Redundant NRC connections add no
history. Placement preparation is cancellable and rejects stale map, selection,
profile or game state. Pair eligibility avoids allocating whole-map indexes;
bulk legacy owner resolution and last-pick deduplication remain linear. Native
maps continue through normal asset/dependency review, compiler and package
services without an AI or external-service requirement.

The automated Qt action suite covers TB, NRC and default profiles at 100%/200%,
including high contrast, expanded RTL text, enabled state, real Tools actions,
undo/redo and stale publication. The retained Qt render still shows the existing
crowding at 200% expanded RTL; this is not a production-readiness claim. Native
physical input, assistive technology, macOS/Linux and large real-project
acceptance remain open. Doom 3's name/numbered-target rules remain outside the
current idTech1/2/3 authoring scope.

Evidence is retained under `.agents/tmp/level-entity-connections/`. The next
related audit is NRC `SelectConnectedEntities`: it adds direct incoming and
outgoing `target` neighbours to the existing selection, excludes hidden paths
and worldspawn, and does not compute a transitive closure. VibeStudio's separate
source/target commands currently depend on the drawing graph, ignore selected
patch owners, and lack this combined operation. The pinned-source findings are
recorded in `next-selection-audit.json`; that command remains unreviewed.

Final review also aligned the target-link graph's key matching with native
connections: differently cased custom keys no longer mask `targetname` or create
false `target` links. Fresh NRC names read the exact `classname` key rather than
the editor's permissive cached class label. A parsed case-variant fixture covers
both authored names and the link graph; affected map, geometry-cache and Qt
connection suites are rerun after this correction.

Verification: **60 distinct suites passed**, combining the focused and broad
regression runs. Five affected map, geometry-cache and connection suites passed
again after the exact-key graph correction; the final three-profile 100%/200%
Qt connection suite took **89.22 seconds**. Initial test fixture selection
mistakes were corrected before the final results. The catalogue records
**60 Partial / 536 unreviewed / 0 Available**, from 596 entries. All 48 catalogues
contain the seven checked connection messages; extraction reports no
unextractable literals, 412 English plurals pass, all 300 registered CLI commands
are documented, and the 22-page documentation site builds. The retained tested
application SHA-256 is
`417523958d683d0583c06afd7505a7c3726960b4b5ddd8b17c273b6048ed7f6e`.
`verified-summary.json` and `verified-tests.json` record source hashes, exact
runs, validation scope and the retained application. Other concurrent modeller
changes in the shared worktree were preserved.

Automatic approval review rejected removal of the superseded `docs-site`
output and empty `vibestudio-package-imports` cache as **blocked by policy**,
before execution. Both remain under this increment's evidence folder. The
cleanup audit verified their exact paths and zero links; no retry or alternate
removal was attempted. The current generated site is `docs-site-final`.

## NRC connected-entity selection — 2026-10-21

Reviewed `Select_ConnectedEntities` and its selected-name/result walkers in
NRC `radiant/select.cpp` at `449778b3840f8f2062daab15c87cb012f2ac945c`, under
GPL-2.0-or-later (compatible with this repository). The implementation is an
original adaptation; [credits](../CREDITS.md#nrc-connected-entity-selection)
pin the source and describe the studio-specific boundaries.

**Select Connected Entities** now adds one hop of incoming then outgoing exact
`target`/`targetname` matches. Original picks retain order, repeated hits are
deduplicated, selected brushes/patches resolve their owners, and unanchored
entities can match. Worldspawn is excluded from seed names and matches. The
algorithm gathers all seed names before adding results, so a second invocation
is required to advance another hop. Other game-specific reference keys remain
outside this NRC command.

The shared query honours locks, hidden paths, view exclusions and open-group
boundaries. Closed groups are atomic. An excluded child prevents selecting its
owner wrapper or a containing closed group; permitted ungrouped siblings can
still be added. Existing hidden picks remain and supply seed names, matching
the upstream collector. Loaded mixed primitives use their source order; new
mixed primitives without spans follow stable native-array order. Selection
changes never rewrite native map bytes or invalidate asset/compiler/package
inputs. TB records non-content history, NRC/default use transient selection,
and a no-op adds no history.

The action is shared by Edit, Select, Tools, context menus and command search.
NRC gets Ctrl+Shift+E; TB retains Complete Tall on that key. Object picks can
drive the query while vertex, edge or face mode is active, as in NRC. Active
clipping, drawing and strokes remain guarded. Cancellable preparation rejects
stale selection, map, view filters and history policy. The read-only
`map select-connected` CLI accepts repeated object/exclusion selectors and an
open-group context, reports original/result picks, added count and source hash,
and rejects write options or source-line input.

The new core/CLI and UI tests passed alongside existing object-selection,
profile and command-registry suites. The Qt suite exercises TB/NRC/default at
100% and 200%, including high contrast and expanded RTL, actual Tools routing,
component modes, profile keys, cancellation, undo/redo and stale results. It
took **98.68 seconds**. The synthetic 20,000-neighbour query took **413 ms**.
The first build's test-only settings-accessor typo was corrected before these
results. Evidence is under `.agents/tmp/level-connected-selection/`.

The command remains **Partial**: physical input, native assistive technology,
macOS/Linux and real-project acceptance are open. The retained 200% expanded-RTL
render still shows existing shell crowding. The older one-direction source and
target commands still depend on the drawing graph and ignore patch-owner seeds;
they require a separate review. Concurrent modeller edits were preserved.

The next pinned-source comparison is NRC **Select All of Type** (Shift+A).
Its native behaviour considers every selected entity owner, falls back to the
texture browser shader when no owner class exists, and replaces matching faces
on selected brushes in face mode. VibeStudio's existing action only considers
the primary entity class. `next-type-selection-audit.json` records the exact
reference functions and remaining integration checks; this command is still
unreviewed.

Final verification: **62 distinct suites passed** across the focused and broad
runs, including sweep, clipper, groups/prefabs, linked objects, history,
visibility, clipboard, region selection, connections, CLI and command inventory.
The connected-selection UI test was then extended to seed actual vertex, edge
and face picks: it verifies that all plan panes and the camera preserve those
picks and that changed component picks invalidate pending results. That final
suite passed in **94.65 seconds**; production level code was unchanged.

All 48 catalogues contain the 13 checked messages, extraction reports zero
unextractable strings, 413 English plural messages pass, the tested application's
301 CLI commands are documented, and the 22-page site builds without broken
links. The command catalogue records **61 Partial / 535 unreviewed / 0 Available**.
`verified-summary.json`, `verified-tests.json` and `baseline-hashes.json` record
the exact validation scope and source/binary hashes. The retained application
SHA-256 is `628f04a0454ae9cd98e275cc6a097201c855d19ed821403c3896efdf07303135`.
Concurrent modeller changes to CLI registration and Meson are identified
separately; they were preserved and do not change the recorded application.

Automatic approval review rejected removal of this increment's verified empty
`vibestudio-package-imports` cache as **blocked by policy**, before execution.
It remains in the evidence folder; no alternate removal or retry followed the
rejection. An earlier read-only cleanup audit hit a PowerShell parameter error
and was stopped without deleting anything; the replacement audit confirmed an
empty directory and no links before the rejected removal.

## NRC type selection — 2026-10-21

Reviewed NRC `Select_AllOfType`, its selected-class/result walkers, brush and
patch shader walkers, and case-insensitive shader comparison at
`449778b3840f8f2062daab15c87cb012f2ac945c`. The GPL-2.0-or-later reference is
compatible with this repository. This is an original adaptation with
[pinned source credits](../CREDITS.md#nrc-type-selection).

**Select All of Type** considers every selected non-world entity or owner of a
selected brush/patch. Object mode replaces picks with exact `classname`
matches; missing and empty exact keys remain valid empty-class matches. Without
any non-world owner class, it replaces picks with brushes then patches using
the current texture-browser material. Material comparison ignores case and
preserves whitespace. Face mode replaces matching faces on selected brushes;
vertex and edge modes leave selections unchanged.

The shared query honours hidden paths, locks, filters, open-group boundaries
and atomic closed groups. Selecting an owner cannot implicitly include an
excluded child. As a studio adaptation, face mode also accepts brush geometry
selected through an entity selector, applies visibility and group restrictions,
and validates solved topology. The GUI rejects incomplete handle results and
disables the face command when handles are truncated. Object picks, native map
bytes and history remain unchanged by face queries. Object-selection history
follows the active profile; no-op queries add no entry.

The action is available through Edit, Tools, Select, context menus and command
search. NRC binds Shift+A to the new command. Other profiles retain their
existing **Select All of This Class** keys and behaviour. Preparation remains
cancellable and rejects changed materials, picks, views, filters and history
policy. Active component drags now also block the shared object queries.
`map select-type` exposes the same read-only behaviour, including reusable
face selectors, source hashes and explicit vertex/edge no-op results. Selection
does not change shared asset, compiler or package inputs or require cloud AI.

Evidence is retained under `.agents/tmp/level-type-selection/`. Initial core
and Qt functional checks passed, but shortcut checks caught the new binding
being placed in GtkRadiant's block. The correction restores that block and
explicitly sets the new command and clears the legacy key in NRC's block;
the original failed runs remain recorded separately from final verification.

The command remains **Partial**. Physical input, native assistive technology,
macOS/Linux and real-project acceptance remain open. Existing shell crowding
at 200% text scale with expanded RTL remains an unrelated issue. Concurrent
modeller changes in the shared worktree are preserved.

The next comparison is NRC **Select Textured** (Ctrl+Shift+A), recorded in
`next-textured-selection-audit.json`. Its additive, independent brush-face and
patch selection differs from the existing whole-object **Select by Texture**
route. The source has different visibility rules for brushes and patches;
the studio adaptation needs explicit group/lock boundaries and verified
surface-edit, material and clipboard handoffs. It remains unreviewed.

That inspection also confirmed a shared integration gap: component-face picks
(including the new type query) drive geometry tools, while **Surfaces > Picked
Faces** and the surface clipboard still read camera/Inspector surface picks.
These models need a shared, tested handoff before the face-selection workflow
can satisfy native NRC material-edit behaviour. The manual and command review
record this limitation; addressing it is the next prerequisite for Select
Textured, rather than adding another isolated selector.

Final verification: **67 distinct suites passed**, combining focused results,
the four corrected shortcut/profile checks and 59 wider regressions. Coverage
includes sweep geometry and viewport controls, clipper, component selection
and dragging, scene locks, groups/prefabs, linked objects, selection history,
visibility, clipboard/paste, region and connection selection, Doom/UDMF, CLI
and command metadata. The final type-selection Qt suite took **96.64 seconds**;
the 20,000-entity class query took **416 ms**. Initial shortcut failures are
retained as superseded results, not counted as successful runs.

All 48 catalogues contain the 22 checked messages. Extraction covers 1,806
source files and 28,076 call sites with no unextractable literals; 416 English
plurals pass. All 48 compiled catalogues were rebuilt, and the three new English
singular/plural pairs were recovered and checked from the generated QM. The
source-catalogue report passes; an initial report pointed at the compiled-only
directory and correctly reported missing TS sources before its root was fixed.
All 302 registered CLI commands are documented, the offline guide is current,
and `docs-site-final` builds 22 pages without broken links. The catalogue has
**62 Partial / 534 unreviewed / 0 Available** entries.

`verified-summary.json`, `verified-tests.json` and `baseline-hashes.json` retain
the scope and exact source/binary hashes. The tested application SHA-256 is
`54a66c11a0e9c3b40b67ec702f00998871ce41bbf4bbfb8170c59923afbbf241`.
Concurrent Meson changes are recorded separately; no listed test/application
binary changed during the broad run. The full production goal remains active.

Automatic approval review rejected cleanup of this increment's superseded
`docs-site`, empty `vibestudio-package-imports` directory and raw `focused.log`
as **blocked by policy**, before execution. The audit verified exact paths and
zero links. These targets remain; no retry or alternative removal was attempted.
The current site is `docs-site-final`, and `test-reports/` contains sanitised
test output without environment dumps.

## Component faces and surface authoring — 21 October 2026

The component-to-surface handoff identified during the type-selection review
now routes picked brush-face components to quick adjustments, detailed alignment,
face clipboard commands and selection-inclusive Radiant transfers. Both Surfaces
target choices use exact component faces while component mode is active. Empty
picks and Vertex/Edge mode do not expand writes to whole objects. Patch-point
picks and object selection remain intact. Object-mode behaviour is unchanged.
Queued work and alignment previews reject changed component picks or mode;
active geometry gestures block new surface operations. Entering component mode
clears old camera surface picks. Explicit hit targets retain their separate flag
policy from additional component targets, including through native strokes.

The shared surface transfer service accepts an optional captured selection;
an explicit empty selection differs from an omitted selection. The read-only
`map select-type --mode faces` result now supplies canonical, one-based
`surfaceTargets` for copy, paste and material commands alongside zero-based
component selectors. No new dependency, setup option or cloud service is needed.

Evidence for this increment is retained under
`.agents/tmp/level-component-surfaces/`. The first focused run passed nine
suites; the two six-profile/scale UI suites reached their timeouts while running
concurrently. Both had reached their fifth combination without an assertion
failure. The existing type-selection UI test then passed alone in 95.94 seconds,
consistent with its previous 96.64-second run. Heavy UI verification is therefore
serial, with the original timeouts and assertions retained. Final verification
is recorded below when complete.

The owned 200% expanded-RTL render still exposes the existing shell/status and
viewport crowding. Native physical input, assistive technology, macOS/Linux and
real-project acceptance remain open. This handoff is **Partial**, and the wider
production goal remains active. NRC Select Textured still needs independent,
additive surface selection and remains unreviewed.

The serial six-combination component-surface UI suite passed in 103.81 seconds,
including native selection-inclusive paste and held-stroke worker publication.
Review then identified a highlight handoff gap: component picks updated handles
and commands, but filled camera faces could remain stale until another refresh.
The final correction updates filled highlights immediately on face-mode and pick
changes, includes only picked brush faces while Face mode is active, and restores
whole-object highlights on leaving it. The UI suite now checks both transitions.

The first highlight verification timed out in both six-combination UI suites.
A timing probe located most delay before map readiness: one scenario spent
47.32 seconds reaching the map and only 0.74 seconds in face queries. The probe
was deliberately stopped after two complete scenarios to switch to the existing
`vibestudio.startup` logging category. A subsequent full type-selection run passed
in 155.64 seconds with unchanged assertions and timeout. Readiness varied from
11.86 to 45.17 seconds; external modeller and openQ4 builds overlapped the slower
run. The map-loading suite separately passed in 60.19 seconds. These observations
do not establish an exclusive cause or a performance fix. Variable shell startup
remains an additional issue, and final verification retains the phase timings.
Windows diagnostic runs use `QT_FORCE_STDERR_LOGGING=1` to retain Qt messages;
ordinary test failures and earlier timeouts remain in sanitised reports.

The final shared-machine functional run uses Meson's `--timeout-multiplier 2`
outer allowance. No test assertion, operation-level readiness wait, or product
performance threshold was changed. This accommodates six independent shell
constructions while other builds run; normal-budget failures remain recorded.
It does not certify startup performance or remove that issue from the backlog.

An A/B/A diagnostic of the NRC 200% expanded-RTL scenario isolated a stylesheet
construction cost: normal startup completed the scenario in 15.18 seconds,
deferring the application stylesheet completed it in 6.19 seconds, and repeating
normal startup took 14.30 seconds. These are one-scenario diagnostics, not full
suite passes. Their source, sanitised reports and owned-widget renders are kept
with this increment. Production construction now defers styling only while page
trees are built and attached, restoring it before item-view polish and layout.
The full six-combination test asserts theme/font preservation; its temporary
diagnostic filters have been removed. Shared-surface regressions extend the
planned set from 83 to 89 suites.

The first build of that change exposed an unrelated modeller compile error:
the UV-density dialog referenced the nonexistent `ModelSurface::material` field.
It now uses its existing `modelPreviewSurfaceMaterialKey` helper. The shared
worktree's concurrent modeller changes are preserved; their feature acceptance
remains separate from the level-editor goal.

Both full six-combination UI suites passed with the page-styling change:
type selection in 53.74 seconds and component surfaces in 41.03 seconds. The
owned renders preserve high contrast, but the component fixture exposed the
Windows offscreen plugin's decorative fallback font. Its test setup now uses
Segoe UI like the existing type-selection fixture; a separate render check
will verify that test-only correction after the main regression run. The 200%
expanded-RTL shell crowding remains open. CLI documentation also gained the
concurrent `model uv-density` entry; all 303 registered commands are covered.
All 48 catalogues compile, and the new UV-density plural brings the checked
English plural count to 419. No claim is made about human translation coverage.

The 89-suite run completed with 88 passes and one failure: the broad shell
interaction suite reported 81 assertions after 383.99 seconds. Its sanitised
report remains in the evidence directory. Isolating the first failures found
that closing a progress dialog left the offscreen fixture without an active
window. The test harness now restores its owned shell before shortcuts when
no dialog or popup is active. The first three checks then passed in 7.58 seconds.
Further checks exposed older assumptions about unchanged-archive Save As,
fractional model playback speed, primitive preview readiness, persistent
world-space clip planes, asynchronous cuts and camera framing controls. These
test expectations are being aligned with the already documented workflows;
the full shell rerun remains pending. No production focus behaviour was changed.

The separate Segoe UI component-surface run passed all six combinations in
41.34 seconds. Its owned-widget render confirms the font correction and retains
the existing 200% expanded-RTL crowding as an open acceptance issue. The next
complete shell run reached 360.86 seconds with 49 failed assertions, down from
81; the early level checks, including clipping and camera face picking, passed.
An isolated 23-check run then passed the build/launch and package transitions
after the harness waited for the actual launch confirmation rather than
cancelling its new progress dialog. That run retained four audio time-label
failures, subsequently aligned with the visible playback-state prefix.

Further harness updates cover shared camera clipping, asynchronous viewport and
query readiness, lazy thumbnail loading, primary-object selection order and
explicit disposal of fixture history. These do not establish a complete shell
pass; the next unfiltered run is pending. A concurrent Meson change also used
the modeller test environment before its declaration; its existing definition
was moved ahead of the first use to restore configuration. The separate
modeller build and its feature acceptance remain outside this increment.

The next complete shell run passed every check except two in 274.90 seconds.
The palette-source failure exposed a product defect: an automatically inferred
Doom palette was rejected from the browser cache because the source's fallback
family was Quake. A later refresh then replaced the resolved source label with
**Loading**. The accepted worker result now retains its resolved palette after
the existing request-identity validation. The regression also checks leaving and
returning to Textures. The camera render showed correctly occluded geometry with
intentional resize handles overlaid; the geometry-occlusion test now hides that
editing overlay while retaining the selected triangle highlights. The full
unfiltered rerun after these two corrections was required.

The fresh combined verification passed **92/92 suites** at their normal time
limits, including the unfiltered shell suite in 379.77 seconds, six-case type
selection in 56.27 seconds and six-case component surfaces in 50.76 seconds.
Sweep geometry, viewport controls and handle tests passed alongside clipping,
groups, prefabs, history, surface transfers and shared studio regressions.
The build covered 89 executable targets; no tested binary changed during the
run. The retained application SHA-256 is
`86da74a9be3845fbb5b836110e9f5cb7d03ee32b587eaf5a11c263dcae2e7f38`.
Evidence is in `.agents/tmp/level-component-surfaces/verified-summary.json`,
`verified-tests.json` and `final-validation-sources.zip`. The source comparison
records concurrent modeller edits and the start of the next Select Textured
increment; those changes are outside this verification result.

All 48 catalogues compile; extraction has no unextractable literals and all
419 English plural messages have both forms. The manual, generated offline
guide, 22-page documentation site, 303-command CLI reference and changelog
checks pass. These checks do not establish human translation completeness.
Owned-widget renders confirm the occlusion and font corrections, while
200% expanded-RTL crowding remains open. Native physical input, assistive
technology, macOS/Linux and real-project acceptance remain open. The command
catalogue still has 62 Partial reviews, 534 unreviewed entries and no Available
entries. The production-grade goal remains active.

### Next increment: independent material surface query

The pinned NRC `Select_FacesAndPatchesByShader` path was rechecked at
`449778b3840f8f2062daab15c87cb012f2ac945c`. It adds independent face picks without
parent-brush selection, adds visible whole patches, preserves other picks and
mode, and uses case-insensitive shader comparison without whitespace trimming.
The implementation begins with a shared cancellable query and read-only
`map select-textured` CLI. New studio matches respect visibility, locks and
open-group context consistently; closed groups must be opened. Exact ordered
geometry fingerprints support later stale-face rejection independently of
material-only edits. Complete results are bounded at 16,384 surface targets.

The viewport command, Ctrl+Shift+A NRC binding, independent-face state,
highlight/count display, Escape precedence, patch/component atomic publication
and authoring handoffs remain required before reviewing the command for GUI
parity. Evidence for this increment belongs in
`.agents/tmp/level-textured-selection/`. The command catalogue is unchanged.
The first build exposed unrelated new modeller spin code calling sorting
helpers private to other implementation files. Local deterministic sorting
helpers restore those references; modeller feature acceptance remains separate.

Automatic approval review rejected the previous increment's disposable fixture
and cache cleanup as “blocked by policy”; it was not retried. Those files remain
under `.agents/tmp/level-component-surfaces/`, alongside the retained evidence.

The additive-query increment is verified against retained application SHA-256
`12b9330df59d22622813a2660c70b54b160b283ad0e2811e33ceefdca318003f`, with no
source or binary drift during the final tests. Eight level suites and the
modeller mesh-tool suite pass. The new query suite covers classic, Valve 220,
brushDef and brushDef3 maps; additive/no-match results; exact face identities;
patch and object ordering; visibility, locks and group boundaries; cancellation;
complete 16,384-target results and overflow refusal; selection-only history;
and a CLI material-authoring handoff that preserves the original source file.
All 304 CLI registrations validate; all 48 catalogues compile and all 421
English plural messages have two nonempty compiled forms. Documentation and
changelog checks pass, including the 22-page site and generated offline guide.

This is **nine passes out of ten final suites**, not a clean full-application
result. The original ten-suite set passed before the concurrent modeller Spin
suite expanded. Its expanded version reports a failure when mixing independent
poses with retained skeletal poses, then reaches its normal 120-second timeout.
The failure remains unresolved and recorded separately; no assertion or timeout
was relaxed. The modeller sorting-helper compile correction is retained, and
further modeller acceptance remains outside this level-editor increment.

The summary and all test results are in
`.agents/tmp/level-textured-selection/verified-summary.json` and
`test-reports/textured-reconciled.json`; `reconciled-sources.zip` binds the source
snapshot. Earlier build/test logs remain alongside them. The viewport work is
still outstanding, with implementation notes in `ui-integration-notes.json`.
The overall goal remains active; no command was promoted to Available or newly
counted as reviewed for GUI parity.

## 2026-10-21: Select Textured viewport integration

The command now appears in the selection menus, Tools and command search, with
Ctrl+Shift+A confined to NRC. It retains a separate, geometry-keyed independent
face selection, adds whole matching patches, and preserves exact existing
vertex/edge/face/patch-point picks. Patch additions are preflighted against the
128-object/16,384-handle component capacity before publishing any result.
Queries remain cancellable and reject changes to material, filters, profile,
document or selection during preparation.

Plan views draw dashed face perimeters; camera highlighting uses constant-time
face membership tests and includes whole query-selected patches in Face mode.
Viewport descriptions and the Surfaces panel expose localisable counts. Surface
adjustments, alignment, material assignment and clipboard transfers share exact
targets. Geometry/owner changes, hidden or locked context and reloads retire
independent picks; material and UV edits preserve them. Escape clears surface
and component picks before leaving component mode and then clearing objects.
Independent face geometry manipulation remains a separate parity gap.

The first six-case UI run identified a missing command-catalogue entry, which
prevented the NRC binding from installing. Test fixtures also needed real
texture dimensions for patch UV shifts and had to wait for the camera to become
enabled after asynchronous undo refresh. Those corrections produced a clean
68.39-second run across NRC, TB and default profiles at 100% and 200%, with
expanded RTL text and high contrast in the enlarged cases. Assertions cover
exact face/patch authoring, component preservation, cancellation, stale results,
surface versus geometry undo, same-path reload and staged deselection. A pure
viewport capacity check verifies refusal without handle/pick mutation.

The catalogue now records Select Textured as Partial: 63 reviewed entries,
533 unreviewed, zero Available. The 200% expanded-RTL capture still shows
existing shell and sidebar crowding. Native input, assistive technology,
macOS/Linux and real-project acceptance remain open. Regression evidence is retained in
`.agents/tmp/level-textured-viewport/`. The overall goal remains active.

The first broad run passed 40 of 42 suites, including the full shell. Camera
component tests incorrectly assumed that TB selection had left an empty undo
history; they now compare the captured undo/redo depth and history revision,
while retaining exact geometry and single-edit checks. The held surface-stroke
test exposed a production picking inconsistency: initial hits refined subpixel
triangle edges, while later hits used pixel IDs alone. Initial and held picks
now share the same refinement, with held picks using the original rendered
corners and camera, including near-plane clipping and material regrouping.

A deterministic renderer test reproduced the old mismatch in perspective,
orthographic and clipped views, before and after preview reordering. The fixed
renderer, camera components, held strokes and six-case Select Textured UI all
passed focused checks. The next broad run passed 43 of 44 suites; its full shell
test failed a combined texture-list/status assertion. The strengthened test
captures the synchronous material-application status before unrelated event
processing, checks every face and one undo step, waits for the texture-list
refresh and verifies exact map undo. Its focused check passed in 5.72 seconds.
The unfiltered shell passed in 319.25 seconds after the repair. Select Textured
UI passed in 51.65 seconds and the viewport renderer passed in 1.51 seconds;
additional renderer checks passed at device-pixel ratios 1.5 and 2. The rebuild
also incorporates concurrent shared modeller changes. The two application
builds and their source snapshots are retained separately; do not describe this
as one clean 44-suite run of the latest application.
Further modeller edits arrived during that rebuild; its dry run still lists
pending model targets. The later source archive records that point in time and
cannot reconstruct every compiled object exactly. Binary hashes identify the
executables actually tested; this is not a clean build of the whole current
working tree.

The retained later application SHA-256 is
`08391a9855cf7733259791c34f44028303ebbae35d3e5124c75b8b3fe9c260f4`;
the broad-run application is
`acc6adad12cf528c8d773bff55f3b3deb63a665d584750ff47e83ebd5797796f`.
Neither tested set changed binaries during its run. All 304 CLI registrations
validate; 48 compiled catalogues include both nonempty forms for all 427 English
plural messages. The manual, offline guide, 22-page site, catalogue references,
credits and changelog checks pass. `verified-summary.json` records the exact
rounds, source drift and pending build targets. Captures use owned Qt render
targets only. No command was promoted beyond Partial; the goal remains active.

A follow-up deletion audit is recorded in `next-parity-review.json` beside the
evidence. The shared Delete action already routes component mode to component
removal. NRC falls back to whole-object deletion when no components are picked;
the studio currently does not. Empty-pick policy, eligibility, menu/shortcut
consistency and locked/pending-state coverage need an explicit review before
those commands are promoted. Independent-face geometry manipulation and the
200% expanded-RTL shell layout remain separate implementation gaps.

## 2026-10-21: Delete parity and complete command review

This session continued the goal after the Codex thread stopped at its usage
limit in the middle of the Delete increment. The shared tree, including
concurrent modeller work, built cleanly in `builddir` with warnings as errors.

**Delete.** The interrupted UI regression failed because its fixture peaked
brush 0, but saving writes a component-edited brush after its unedited sibling,
so the reopened map numbers the peaked brush 1. The test now finds the peaked
brush by shape. All six profile/scale combinations pass (64.81 seconds), with
the core deletion suite. The reorder itself is recorded as the
`brush-order-on-save` work package below; it changes brush numbering after
save and reopen, which TB and NRC do not.

**Command review.** The 529 unreviewed catalogue rows were reviewed in 40
batches against the pinned TB and NRC checkouts, and each batch was checked
by an independent verifier that confirmed cited code, palette ids, CLI names and
evidence paths, searched for equivalents the first reviewer missed and
challenged every Not applicable verdict. The verifiers made 471 corrections.
The catalogue now records every command: 458 Partial, 101 Planned and 37 Not
applicable. Not applicable is a new catalogue status for commands with no
meaning for the supported games or architecture, such as the UFO:AI plugin and
idTech4-only visportal and preview toggles; each row states its reason. A
Partial row cites a palette or CLI route; 99 cited palette ids belong to
existing level actions that still lack palette entries and are covered by the
`palette-coverage` package. Thirty-one rows with an existing control but no
command route are Planned. Validation of the larger catalogue now resolves each
evidence path once.

Of the reviewed rows, 145 applicable commands already have an equivalent
route, 277 are partial and 70 are missing. The 347 open gaps were grouped into
101 work packages: tier A (core authoring), B (everyday navigation and
workflow), C (specialised tools and plugins) and D (low value). Three parallel
implementation lanes work through them in waves, each package being
implemented, independently verified and fixed before the next starts.
Five packages come from outside the catalogue: independent-face geometry
manipulation, brush order on save, the 200% expanded-RTL layout, the textured
sweep preview in the main camera and palette coverage.

**Regression baseline.** A complete 606-test run of the shared tree exposed
older suites that the targeted increment runs did not include. The
camera-resize shell suite assumed an empty undo stack, but the TrenchBroom
profile now records selection changes in history; it now counts content edits
and passes. The same assumption affects the component-cycle and brush-job UI
suites. The overlay-worker and pane-phase suites expect the previous fixed
high-contrast plan palette, which now follows the theme tokens. These are
being corrected separately from the parity lanes. Heavy UI suites timed out in
that run because several builds shared the machine; they are rerun serially
before any conclusion.

The goal remains active. Native physical input, assistive technology,
macOS/Linux and real-project acceptance remain open, as do the gesture,
inspector and workflow inventory layers.

## 2026-10-22: Parity wave 1

### Package `brush-order-on-save`

**Brush file order on save (lane 1; one of the five packages from outside the catalogue).** replaceLevelMapBrushGeometry() used to drop a rewritten brush's source lines and save its new text just before the entity's closing brace. Every later brush in the entity moved, so a reopen or a compiler message numbered them differently. LevelMapBrush now carries spliceLine and spliceBrushId, and serializeMapText() writes the rewritten brush just before the first source line its edit dropped. Component edits keep the brush's own place: a repeated edit inherits it and the "// brush N" marker stays. commitBrushReplacement() gives each replaced brush's place to the result that keeps its ID (split extrusion, wrap/merge), or else to the first piece recorded in sceneObjectOrigins in the same entity (clip, carve, hollow, intersect, merge). Any other pieces join the end of the entity, as NetRadiant Custom's clipper adds only the fragment. A shared helper, levelMapBrushSaveLine(), drives the serializer, levelMapObjectAtNumber/levelMapObjectNumber (Find Brush by Number and compiler-line lookup) and the order of copied .map text. Copies with a different ID, and brushes moved to another entity, fall back to the end of the entity. Undo and redo carry the fields in their snapshots. The CLI uses the same core code, so it needs no new route. Tests: the new level-brush-file-order-smoke (core and CLI, four face dialects) checks component, repeated component, clip, split extrusion, carve and duplicate edits. It checks reload numbering against document numbering, ID stability for untouched brushes, survival of the marker and group membership, copy order, exact undo back to the source bytes and redo, and that map move-components, clip and carve write the same bytes as the core. A core-only mutation that turns splices off fails 92 of its checks. 31 related core/CLI suites pass, and 6 of the 7 related offscreen UI suites pass. level-component-wrap-ui-smoke fails a pseudo-expanded label-fit check that has nothing to do with this change. Limits left: new brushes, extra pieces, copies, pastes and linked-copy refreshes still join the end of their entity, as TB and NRC append new nodes, so brushes in later entities are renumbered after such edits. The in-memory brush vector order is unchanged. New pieces' in-memory IDs still differ from the numbers a reopen gives them. VibeStudio remains pre-alpha.

Remaining: Nothing left in the package scope. Possible follow-ups, not required by the brief: - Make the in-memory document->brushes order match file order. commitBrushReplacement still appends pieces to the end of the vector, which matters only for rendering and outliner order. - Linked-group refreshes still delete the target brushes and duplicate the source, so refreshed linked copies are appended at the end of their entity (TB likewise replaces linked children). - New brushes keep max+1 IDs in memory, but a reopen numbers them by file position.

### Package `entity-primitive-ownership`

Entity primitive ownership (lane2, package entity-primitive-ownership). The shared service is declared in src/core/level_entity_ownership.h and implemented in src/core/level_map.cpp. It provides moveLevelMapSelectionToEntity (last-selected, first-selected or named entity:N target, worldspawn included, point entities refused), moveLevelMapSelectedPrimitivesToWorld (NRC Ungroup Primitives), makeLevelMapSelectionStructural with DetailOnly and TrenchBroom LeaveEntities policies (setLevelMapSelectionDetail(false) now routes through it, so in Quake only the selected brushes leave a func_detail), and explodeLevelMapSelectionEntities (bobToolz Explode Entity; optional unique targetnames). Each edit is one guarded undo step. A replace-objects step, extended to carry patches in place, keeps every primitive's id and moves its text to the end of its new owner, so scene membership, temporary visibility and selection follow it. Emptied source entities go, and hidden primitives of a selected entity stay. Locked, linked and unsuitable content is refused without touching the map. A brush leaving for the world takes its old entity's scene node, otherwise the open group or current layer. GUI: six commands in the Brush Entity menu, the Tools tab's Entities group, the view bar's Create menu and the palette; Ctrl+M in the NRC profile (stripped from the NetRadiant baseline profile); Alt+S plus the makeStructuralLeavesEntities policy in the TB profile, shown in the controls reference. CLI: map move-to-entity, map explode-entity, map move-to-world --selected-only and map detail --structural --policy trenchbroom. Tests: level-entity-ownership-smoke (core + CLI) and level-entity-ownership-ui-smoke (3 profiles at 100% and at 200% with high contrast, expanded strings and RTL) pass. Mutation checks on emptied-entity removal, scene placement and the linked refusal each made the core test fail. Related suites pass: level-map-tools, scene-locks, object-selection, visibility, brush-tools, brush, duplicate-unique, entity-connections, studio-semantics, editor-profiles, level-sidebar-ui and level-profiles-ui. The source-layout, CLI-docs and docs validators pass and the docs site builds. Limits left: native physical-input and macOS/Linux acceptance; TrenchBroom smart tags other than the detail flag are not cleared; the edits run synchronously like Tie to Entity rather than as a cancellable LevelPlacementOperation; without entity definitions, a point entity is recognised by an origin with nothing in it; Explode keeps the original entity for its first visible primitive instead of replacing it.

Remaining: Nothing in the package brief is left undone. Limits still open: 1. Native physical-input and macOS/Linux acceptance for all five catalogue commands. 2. TrenchBroom's Make Structural also clears other smart tags (liquid contents, transparency surface flags and so on, from the game config); only the detail flag is cleared here, because VibeStudio has no smart-tag model yet. 3. The ownership edits run synchronously in the UI thread, like Tie to Entity and Move to World, rather than through `LevelPlacementOperation` and `LevelPlacementTaskDialog::prepare` with progress and cancellation. Very large entities would block briefly. 4. Without entity definitions in core, a point entity (a refused move target) is recognised as one with an origin and no primitives.

### Package `palette-coverage`

Palette coverage (2026-10-21, lane3). Shipped: about 180 entries in src/core/studio_semantics.cpp, so every map.* action the Levels page registers is in the shared registry with its hyphenated id, a translatable label and a summary. This covers the 99 ids already cited by 334 review routes, the layout and sidebar-tab commands, and entity.definitions. **Show Map Statistics** (map.statistics) and **Show Worldspawn Keys** (map.worldspawn) are new View > Level Sidebars commands. Each opens its Map tab section expanded, with focus in the list. Because profile bindings now resolve, the TrenchBroom profile binds V/E/F, Ctrl+Alt+Shift+V and Ctrl+Shift+D. The NetRadiant Custom profile binds V/E/F, the keypad patch row and column keys, Ctrl+I, Ctrl+Shift+M, Ctrl+T, Ctrl+Shift+W, Shift+I, Shift+T, Alt+D, Alt+S and Ctrl+Shift+Tab. Ctrl+Shift+I and Ctrl+W stay unbound because the studio uses them everywhere. The standalone NetRadiant profile does not inherit these keys. Tests: the new level-palette-coverage-ui-smoke (6 profile/scale passes, real key presses with exact undo, the CLI check) passes. studio-semantics-smoke, editor-profiles-smoke, level-sidebar, level-geometry-tools, level-gestures (core and UI), level-profiles, level-component-drag, level-view-links and level-textured-selection UI tests pass. shell-interaction-smoke passes; one earlier run under load had 2 camera-timing failures that passed in isolation and on a full rerun. Inventory validation against the built `ui semantics` output resolves all 596 reviews' GUI routes. Limits: F in the TB and NRC profiles toggles face editing, so the Keyboard Shortcuts list's built-in row 'Frame the selection: F' is inaccurate for those profiles. NRC Flip Texture U and Smooth Patch Rows have no key. Reviews owned by other packages still say 'no palette entry' until those packages update them. Native physical input, screen readers and macOS/Linux acceptance remain open. Status: Partial; VibeStudio remains pre-alpha.

Remaining: 2) The Keyboard Shortcuts dialog's built-in row 'Frame the selection: F (Levels view)' in application_shell.cpp is static. Under the TrenchBroom and NRC profiles F now toggles face editing. I did not edit that shared file; the row could say the profile can take the key. 3) In the NRC profile, Flip Texture U (Ctrl+Shift+I) and Smooth Patch Rows (Ctrl+W) have no key, because the studio uses those keys everywhere for Initialize Project Manifest and Close Package. Changing that needs a decision on those shortcuts. 4) Native physical input, screen readers and macOS/Linux acceptance are untested. No CLI change was needed, because pure registry and view routes need none. 5) 59 reviews owned by other packages still say 'no palette entry' or 'missing from the registry' even though the entries now exist. Each owning package should drop that wording: - arbitrary-rotate-scale-dialogs: nrc:ArbitraryRotation, nrc:ArbitraryScale - nrc-prism-primitives: nrc:BrushCone, nrc:BrushPrism, nrc:BrushSphere - make-room-options: nrc:CSGroom - selection-helpers-nrc: nrc:ExpandSelectionToEntities, nrc:RegionSetSelection - reveal-material-and-find-replace: nrc:FindReplaceTextures - texture-fit-and-projection: nrc:FitTextureHeight, nrc:FitTextureWidth, nrc:TextureProjectAxial - map-file-import-export: nrc:ImportMap, nrc:SaveSelected - object-scale-gizmo: nrc:MouseRotateOrScale - directional-nudge-commands: nrc:MoveSelectionUP/DOWN and the four tb Move objects reviews - patch-shape-natural-mapping: nrc:PatchCylinder, nrc:SimplePatchMesh - exact-patch-density-options: nrc:PatchXactSphere - quick-axis-transforms: nrc:RotateSelectionX/Y/Z - selection-size-and-workzone-overlays: nrc:ShowSize3d - diagnostics-and-help: nrc:ShowStats - texture-reset-commands: nrc:TextureReset/Cap, tb Reset texture alignment - component-uv-lock-and-snap: nrc:TogTexVertexLock, tb:Menu/Edit/UV Lock - leak-trail-navigation: nrc:TogglePointfile - sidebar-toggle-commands: nrc:ToggleTextures - bobtoolz-entity-generators, ctf-swap-teams, worldspawn-key-helpers, patch-uv-expression, portal-viewer (2) and filter-material-library-kinds: their plugin reviews - escape-cancel-cascade: tb Deactivate current tool - camera-relative-texture-moves: tb Flip textures horizontally/vertically and the four Move textures (fine) reviews - entity-primitive-ownership: tb Make structural - entity-definition-create-commands: tb Entities Create - data-driven-filter-tags: tb Entities Toggle - brush-from-points: tb Create Brush - assemble-brush-tool: tb Brush Tool - object-rotate-gizmo: tb Rotate Tool - transform-shear-tool: tb Shear Tool - plan-projection-recentre-zoom: tb Reset 2D Cameras - smart-tag-commands: tb Tags Enable/Disable

### Package `independent-face-manipulation`

Independent face manipulation (2026-10-22, lane 1). Faces picked by Select Textured now join component transforms in Edit Faces mode without selecting their parent brushes, matching NetRadiant Custom's face mode workflow. src/core/level_textured_selection adds two functions. levelSurfaceFaceComponentSets() checks each pick (face exists, query geometry fingerprint unchanged, visible, not filtered, not locked, inside the open group, at most 16,384 picks and 256 objects) and merges it into the handle sets. A face joins its brush's face set; a brush with vertex or edge picks receives the face corners as vertices. transformLevelMapSurfaceFaces() then calls the shared transformLevelMapComponents(), so the pivot, UV lock, Valve 220 permission, scene guard, linked-group propagation and single undo step are unchanged. After the rebuild each pick follows the face that carries its transformed plane; faces split by grid rounding fall back to pieces built only from their own corners, and faces that disappear are retired and counted. The placement worker (TransformComponents with surfaceFaces) makes the numeric dialog cancellable. The dialog states its face scope in an accessible label. Arrow keys in a plan view nudge the faces when no handles are picked. The plan and camera manipulators and handle drags carry the faces through the component worker. The shell keeps the pick states on either side of each transform so undo and redo restore matching picks. `map transform-components --face face:B:N [--context GROUP]` uses the same service, can be combined with --set, and returns the remapped faces, brushGeometrySha256, retiredFaces and parentBrushesSelected: false. Tests: level-face-transform-smoke covers core, refusal, limit, cancellation and CLI cases, and passed in 2.15 s. level-face-transform-ui-smoke covers three profiles at 100% and at 200% with high contrast, expanded strings and right-to-left layout, including the dialog, undo/redo, the arrow nudge, a manipulator result that matches the core byte for byte, and Escape; it passed in 43.26 s. Twelve related suites passed, including textured selection, component transform, gizmo, drag, camera components, component surfaces, studio semantics and editor profiles. Limits left: transforms move face corners, not NetRadiant Custom's face planes, so rotation and scaling can give different shapes next to non-perpendicular faces. Picks over more than 256 brushes are refused. Edit Vertices and Edit Edges modes, splitting, extrusion and vertex insertion stay handle-only. Faces without handles cannot be dragged directly in a plan view (use the manipulator or the arrow keys), and the camera has no arrow nudge. With large picks, each manipulator refresh re-solves up to 256 brushes. Native physical input, screen readers, macOS/Linux and large real projects have not been tested. VibeStudio remains pre-alpha.

Remaining: Nothing in the package brief is left undone. Possible follow-ups: (1) An optional NRC-style plane transform for rotating and scaling faces. The current transform moves corners, which can give a different shape next to non-perpendicular faces. (2) Dragging faces that have no handles directly in plan views, and an arrow-key nudge in the camera view. (3) Taking picked faces through split, extrude and vertex insertion, which still use handles only. (4) Caching the merged sets so the manipulator refresh does not re-solve up to 256 brushes each time with large picks. (5) The coordinator's catalogue refresh, for the new %n plural strings in level_component_transform_actions.cpp and level_geometry_actions.cpp, and regenerating the offline guide after the manual edits.

### Package `entity-colour-commands`

Entity colour commands (NRC EntityColorSet and EntityColorNormalize) shipped through one shared core service, src/core/level_entity_colour.{h,cpp}. That service now also holds the colour parser the Inspector swatches use; it used to live in application_shell.cpp. Set Entity Colour (map.setEntityColour, palette map.set-entity-colour) writes _color to each selected entity and to the entity owning each selected brush or patch, once each. Colouring is limited to Quake-family .map entities. Worldspawn is written only when it is selected itself, never through a world brush, and hidden entities are left out and counted. A QColorDialog opens on the primary entity's _color or on the colour remembered in levels/entityColour. Nothing to colour, a locked target, or a target outside the open group is reported before the picker opens. The value is written in each key's own 0-1 or 0-255 form with any trailing brightness kept. Normalise Entity Colour (map.normaliseEntityColour, palette map.normalise-entity-colour) divides the primary's colour by its brightest channel (black becomes 1 1 1) and writes it to every target in fractions. Like NRC, it also remembers the result for the picker. Both commands are one renamed set-properties undo step through setLevelMapEntitiesProperty and the scene guard, so locks, the open group and linked copies behave as for any key edit, and an unchanged value adds no step. They appear on Edit > Keys, the entity right-click menu and the Tools tab. K is bound in the NetRadiant Custom profile (NRC 449778b3) and the NetRadiant profile (NetRadiant b4b295d7 binds EntityColor to K); Normalise has no key, as upstream. `map entity-colour` (alias entity-color) takes --colour "r g b" (0-1, 0-255, commas or #rrggbb) or --normalise/--normalize [--each], with --object or --where and an optional --key. It saves to --output only and reports mode, value, source, entities, changed, skipped, worldPrimitives and hidden. Tests: level-entity-colour-smoke and level-entity-colour-ui-smoke pass. So do studio-semantics-smoke, editor-profiles-smoke, level-entity-ownership-smoke/ui-smoke and level-palette-coverage-ui-smoke. shell-interaction-smoke timed out under heavy concurrent load in checkDockMirroring, but its Inspector colour-picker check passed and its three FAIL lines are in unrelated code-editor and navigation checks. Limits left: - Behaviour that knowingly differs from NRC: NRC writes worldspawn's _color when a world brush is selected and always writes %g fractions. - The GtkRadiant 1.6 profile has no K, because no checkout was available to verify it. - Native physical-input and macOS/Linux acceptance remain open.

Remaining: The package is complete. Optional follow-ups: 1. Bind K in the GtkRadiant 1.6 profile once its radiant/entity.cpp EntityColor accelerator is checked against a checkout; none was available, so it stays unbound. 2. Add a GUI route for Normalise Each, which is CLI-only for now. 3. Rerun shell-interaction-smoke alone on an idle machine. It timed out twice in checkDockMirroring under concurrent load, and its three FAIL lines (checkNextProblem, checkPlaceHistory, checkNavigationRail) are outside this package. 4. Native physical-input and macOS/Linux acceptance for both commands. 5. The coordinator still needs to run the i18n refresh and regenerate the offline guide; neither was run here, per the guide.

### Package `component-uv-lock-and-snap`

component-uv-lock-and-snap (lane1, 2026-10-22). What shipped: 1. A fitted fallback for component UV lock. When a face can no longer keep every corner's texture coordinates in one planar projection, `lockComponentFace()` (src/core/level_brush.cpp) keeps three corners exactly, chosen by `fittedUvCorners()` (unmoved corners first while they span a stable triangle), and lets the others drift, following NRC's texture vertex lock (`Texdef_from_ST`, 449778b3). A classic face that cannot show the fitted mapping takes the new least-squares `nearestClassicLevelTextureProjection()` unless Valve 220 conversion is allowed. `LevelComponentTransformRequest::fitUvLock` turns it on, and `fittedFaces` is counted in both edit reports. 2. GUI: the saved toggle **Fit UVs When Lock Fails** (`map.componentUvFit`, on by default) in Geometry, Tools > Vertices and the palette, plus a matching checkbox in **Transform Components…**. Every component edit reads the UV settings through one shared helper. Status messages report fitted faces. The TrenchBroom profile binds U to **Lock Component UVs**. CLI: `map transform-components --uv-lock off|on|fit`, with `uvLockMode` and `fittedFaces` in the JSON output. 3. **Round Selection to Grid**: `roundLevelMapSelectionToGrid()` in src/core/level_grid_round.*, with a `RoundToGrid` placement operation, `map.roundToGrid` and the `map round-to-grid` CLI command. It rounds brush vertices (including the brushes of selected brush entities), patch control points and point-entity origins in one undo step, or only the picked components (including Select Textured face picks) in a component mode. Hidden objects, worldspawn and brushes the grid would collapse are skipped with reasons. Scene locks and the open group are enforced through `guardLevelSceneEdit`. NRC's Ctrl+G now runs it and Snap Selection to Grid has no key in NRC; the NetRadiant baseline keeps Ctrl+G on Snap Selection to Grid. Tests: new level-grid-round-smoke (core + CLI) and level-grid-round-ui-smoke (NRC, TB and default profiles at 100% and 200% with high contrast, expanded translation and RTL) pass. The extended level-component-transform-ui/cli-smoke and level-palette-coverage-ui-smoke tests pass. Related suites also pass: component-transform, component-drag, vertex-snap, gizmo, face-transform, brush-tools, placement, placement-jobs, textured-selection, geometry-tools-ui, studio-semantics and editor-profiles. validate_cli_docs, validate_docs, validate_source_layout and validate_credits pass. Limits left: - The fit chooses unmoved corners rather than NRC's plane-defining triangle, so drifting corners can differ from NRC and TrenchBroom (TB re-fits from the matching old face). - The lock also covers face components, where NRC covers only vertices and edges. - Patch points always keep their UVs. - In face mode, NRC snaps a picked face's plane points; VibeStudio rounds the face's corners. - Brush-entity origin keys are not rounded. - Doom-format maps fall back to Snap Selection to Grid. - Native physical input and macOS/Linux acceptance remain open.

Remaining: The package is complete in scope; these items remain open: 1. The fit has no visual preview of which corners will drift before release; drags only report the fitted-face count in the status bar afterwards. 2. Corner choice differs from NRC's plane-defining triangle and TrenchBroom's re-fit from the matching old face, so drifting corners can differ from both editors. 3. Round Selection to Grid does not round brush-entity origin keys, and in face mode it rounds corners rather than NRC's face plane points. 4. Doom-format maps use Snap Selection to Grid instead. 5. Native physical input and macOS/Linux acceptance remain open for all three catalogue commands. 6. The i18n catalogues were not refreshed, as instructed: the coordinator needs to run `extract_translations.py --write` and `english_plurals.py --write` for the new strings, several with %n plurals. 7. `docs/OFFLINE_USER_GUIDE.md` was not regenerated, as instructed.

### Package `large-grid-sizes`

Large grid sizes (lane3, package large-grid-sizes). The Levels working grid now runs from 1 to 1024 units. One shared list, levelEditorGridSizes() in src/core/level_editor_controls.h, feeds the view-bar grid list, the View tab mirror, the map.grid<N> commands and their palette entries (map.grid512 and map.grid1024 are new), Smaller/Larger Grid enablement and profile validation, which now accepts defaults up to 1024. Larger Grid steps past 256 to 1024 and is disabled there; every plan pane follows, the status bar and the accessible value name the size, and it is kept for the next session. NRC's GridPrev, GridNext and setGridPower at 449778b3 turn snapping on, and so does NetRadiant at b4b295d7. This is now profile data, LevelEditorControls::gridChangeEnablesSnap, on for netradiant-custom and netradiant. Grid commands, [ / ] and keyboard or mouse choices in either grid list then tick Snap and say so in the status bar. The Controls reference shows it as Changing the grid. Users can override it per profile with the gesture preference editing.gridChangeEnablesSnap, in the GUI, through editor gestures and in portable files. editor controls --json reports gridSizes and gridChangeTurnsSnapOn; no map command was added, because the working grid is a view setting and map commands take --grid. The Eureka profile binds 9 and 0 to the new sizes per its bindings.cfg; NRC, TB and GtkRadiant give them no key, as upstream does. The grid commands and snap policy live in the new src/app/level_grid_actions.cpp. Tests: level-large-grid-smoke (core and CLI) and level-large-grid-ui-smoke (NRC, TB and default at 100% and at 200% with high contrast, expanded strings and RTL, plus Eureka) pass. That includes real ] / 9 / 0 presses, list keyboard choices and a 1024-unit nudge as one exactly undone step. viewport-overlay-smoke now proves clean sparse line placement at 512 and 1024. Related suites pass: level-gestures (field count now 99), editor-profiles, studio-semantics, level-views-ui, level-sidebar-ui, level-palette-coverage-ui, level-gestures-ui, level-profiles-ui, level-duplicate-ui, and the filtered shell-interaction map and shortcut checks. The source-layout, CLI-docs and docs validators pass, and the docs site builds. Limits left: fractional grids (0.125 to 0.5); at 1024 (and at 1) the disabled step command no longer turns Snap on as NRC's always-enabled GridNext/GridPrev do; grid presets are not checkable radio items; in the Radiant profiles the arrow-key camera drive moves one grid size per press, so 1024 units at the largest grid; native physical-key and macOS/Linux acceptance.

Remaining: Nothing in the package scope is left. Upstream gaps that remain: - Fractional grids (0.125, 0.25, 0.5) are out of scope; MapViewport and the grid store sizes as whole numbers. - Smaller/Larger Grid are disabled at 1 and at 1024, so [ / ] there do not turn Snap on as NRC's always-enabled GridPrev/GridNext do. - Grid presets are palette commands, not checkable radio menu items. - In the Radiant profiles, the plan arrow-key camera drive moves max(grid, 16) units per press. At the largest grid that is 1024 units, whereas NRC's camera speed does not depend on the grid. - Native physical-key acceptance on Windows, macOS and Linux is still open. Tidy-ups for the coordinator: - The comment on `stepLevelMapGrid` in `src/app/application_shell.h` (line ~1148) still says "1 to 256 units". I left it so a comment-only header change would not force every lane into a full app rebuild; fold it into your next header edit. - The i18n catalogues need the usual refresh for the new strings listed under validators.

### Package `entity-definition-create-commands`

entity-definition-create-commands (lane2). Shipped TrenchBroom's generated Create <class> commands. A new core service, src/core/level_entity_create.{h,cpp}, gives every point and brush class of the loaded definitions a command `map.createEntity.<class>`; worldspawn, base classes and unusable names are left out, and two names with the same command id give one command. A point class runs through a new CreateEntity placement operation. Its position comes from the TB-derived cursor placement Paste at Cursor already used: the camera's ray through the pointer, or the active plan view's plane at its brush depth, at the view centre when the pointer is elsewhere. The definition bounds and the grid are applied through levelPasteOffset, then addLevelMapEntity adds the entity. A brush class runs tieLevelMapSelectionToEntity on the selected brushes and patches and the brushes of selected brush entities. Each create is one undo step. A locked, hidden or out-of-group brush, or a locked current layer, is refused with the reason before anything changes, and a new entity joins the open group. On the Levels page the commands are registered from the loaded classes and registered again in one batch when the classes change (new StudioCommandRegistry::unregisterCommand). They act on the Levels page only; point classes are enabled whenever a Quake-family map is open, brush classes only once brushes are selected. The shared studio_semantics registry resolves generated ids (a palette entry, and shellCommandIdExists). The registry keeps user keys for Create commands whose class is not loaded. Create Entico… (`map.createEntity`) types a class in a picker. The Brush Entity menu reuses the generated actions, so they show their keys. Paste at Cursor now uses the same pointer-placement helper. No profile binds the commands, matching TrenchBroom's defaults; NRC has no equivalent. New CLI `map create-entity` uses the same service: point classes take --origin, --camera, --plane-point or --ray-origin with optional --grid; brush classes take --object; --key, --context, --definitions, --dry-run and --overwrite are supported. Tests: level-entity-create-smoke (core and CLI) and level-entity-create-ui-smoke (NRC, TB and VibeStudio profiles at 100% and 200% high-contrast, expanded and RTL; plan-view and camera placement, picker, user key via Qt Test, locked refusal, definitions reload) both pass. Related tests pass too: palette coverage, paste, placement, object clipboard, entity ownership, map tools, semantics, editor profiles and settings. Limits left: plan views do not anchor on a selected brush face under the pointer as TrenchBroom's 2D views do; definition default keys are not filled in; the generated ids are not listed in the static `ui semantics` output, though lookups resolve them; TB's per-class Toggle visible commands are a separate catalogue row; native physical input and macOS/Linux acceptance remain open. VibeStudio is pre-alpha and this is largely untested in real projects.

Remaining: Nothing is left within the package's scope. Possible follow-ups: 1. TrenchBroom's 2D placement against a selected brush face under the pointer; the plan views currently use the brush-depth plane. 2. Filling in definition default keys when an entity is created. 3. Generating TrenchBroom's per-class Toggle visible commands, which are a separate catalogue row. One edge case: if a user's key belongs to a Create command whose class is not loaded and is then given to another command, both keep it. When the class comes back, the first-registered command gets the key and the clash is listed in the registry's shortcut conflicts. The i18n catalogue refresh, the offline guide regeneration and the CHANGELOG and catalogue JSON updates are left to the coordinator, as the guide says.

### Package `quick-axis-transforms`

quick-axis-transforms (lane1). Shipped: - The shared core module `src/core/level_quick_transform.{h,cpp}`: the NRC world-axis command table with NRC's signs (X -90, Y +90, Z -90), the Doom support rule, and the grid-rounded quick-turn pivot built on `levelMapSelectionBounds()` and `snapLevelMapPosition()`. - Six shell commands, `map.flipX` to `map.rotateZ` (`src/app/level_axis_transform_actions.cpp`). They reuse the existing Mirror/Rotate placement requests, so Texture Lock, entity angles, Repeat, scene locks, open-group context, component mode and the single undo step are unchanged. They appear in Edit > Transform, the Transform toolbar menu, the Tools tab and the palette, with no default keys in any profile, as in NRC. - A data-driven pivot policy, `LevelEditorControls::quickTurnSnapsPivot`. It is on for netradiant-custom and trenchbroom only, applies while Snap is on, and covers Rotate 90° Left/Right and Rotate X/Y/Z. A custom component pivot always wins. Users can override it through the gesture preference `editing.quickTurnSnapsPivot`; the controls reference shows it as "Quick-turn pivot" and `editor controls` reports it as `quickTurnPivot`. - `map rotate --pivot-grid <units>` uses the same core pivot. Tests: - `level-axis-transform-smoke` (core and CLI) and `level-axis-transform-ui-smoke` (NRC/TB/VibeStudio profiles, 100%/200%, high contrast, expanded translator, RTL) pass. - The UI test covers byte equality with the shared service for all six commands from non-matching panes, odd-footprint on-grid results that follow the profile policy, Rotate 90° Right and Left in each projection, front-view Flip Vertical of a Quake brush, Snap off, component-mode Rotate Z, Repeat feed, exact undo, and accessible Tools tiles and palette entries. - Related suites pass: level-transform, rotation, large-grid, editor-profiles, studio-semantics, transform-ui, repeat-ui, palette-coverage, rotation-ui, udmf-transform-ui, doom-mirror-ui, view-workspace-ui, gestures, gestures-ui and component-transform-ui. - shell-interaction-smoke failed one unrelated focus-sensitive Doom Page Up check on its first run, and timed out on the rerun under load. Limits left: - Flips mirror about the exact centre in every profile. This deliberately differs from NRC's grid-snapped flip pivot; it gives TB's result for on-grid selections. - Grid ties round away from zero, where NRC's llrint rounds them to even. - Profiles other than NRC and TB, including Xonotic NetRadiant, keep the exact centre unless the user opts in. - Component-mode flips still do not feed Repeat. - Native physical input and macOS/Linux acceptance remain open.

Remaining: Nothing in the package brief is left undone. Deliberate differences and open items: 1. Flips keep the exact-centre pivot in every profile rather than NRC's full-grid snapOrigin. This is documented in docs/EDITOR_PROFILES.md#quick-turn-pivot. If strict NRC flip parity is wanted, add a separate profile flag for flips. 2. Grid ties use VibeStudio's snapLevelMapPosition (half away from zero), not NRC's llrint (half to even). 3. Component-mode flips still do not feed Repeat. That is a pre-existing limitation outside this package. 4. Native physical input and macOS/Linux acceptance remain open. 5. The coordinator needs to: - refresh the i18n catalogues: new tr() strings in level_axis_transform_actions.cpp, the VibeStudioSemantics, VibeStudioLevelControls and LevelGestures entries, and the CLI descriptor text - regenerate docs/OFFLINE_USER_GUIDE.md, since docs/manual/levels.md, cli.md and editor-profiles.md changed - add the CHANGELOG line

### Package `fractional-grid-sizes`

Fractional grid sizes (lane3, 2026-10-22). The Levels working grid is now a double from 0.125 to 1024 units, as in NetRadiant Custom's GridPower; TrenchBroom offers 0.125 to 256. The size list, profile validation and the new helpers `levelEditorFinestGridUnits`, `levelEditorSteppedGridUnits`, `levelEditorGridSizeText` and `levelEditorGridCommandId` live in `core/level_editor_controls`. The same double carries through `MapViewport`, the plan grid painter (`mapGridDrawnSpacing` thins lines to at least 6 px apart), snapping, nudges, drawn brushes, clip points, gizmos, placement, the 3D camera's move grid, the 2D export (`MapRenderOptions::gridSize`, `map render --grid`) and the saved view state. Grid 0.125, 0.25 and 0.5 are in both grid lists and command search, with no default key as upstream. All `map.grid<N>` commands are checked radio items. Smaller Grid halves down to 0.125. Binary Doom and Hexen maps keep whole units: the fractions are greyed out, a fractional choice moves to 1, and Smaller Grid stops at 1 with a message; UDMF and Quake-family maps keep the fractions. Tests: level-fractional-grid-smoke (core plus the editor, map snap and map render CLI) and level-fractional-grid-ui-smoke (offscreen shell; three profiles at 100% and 200% with high contrast, expanded strings and RTL; a binary Doom phase) both pass. So do level-large-grid-smoke/-ui-smoke, viewport-overlay-smoke, map-render-smoke, editor-profiles-smoke, studio-semantics-smoke, level-palette-coverage-ui-smoke, level-views-ui-smoke and 14 other related suites, plus the shell-interaction grid check (with checkMapGuards). Still open: - the Hollow thickness prompts take whole units (they offer 1 on a fractional grid); - 0.125-unit lines are only visible at 48 px per unit or closer; below that they thin out instead of fading as in TB; - no test reads the 3D view's move grid back, because ModelViewport is owned by the modeller session and has no getter; - acceptance with physical input and on macOS and Linux; - pre-existing: an unsnapped drag on a binary Doom map can still leave fractional in-memory coordinates that the save rounds.

Remaining: The package is delivered in full. Still open: - **Hollow thickness prompts.** Hollow and Hollow Touching still ask for whole units through QInputDialog::getInt, so on a fractional grid they offer 1. NRC uses the grid size there. Switching them to getDouble would change those dialogs and their tests. - **Fine-line rendering.** Lines are thinned by doubling the drawn spacing, not faded as in TB. 0.125-unit lines appear only at 48 px per unit or closer; the maximum zoom is 64. - **3D move grid not checked by a test.** ModelViewport::setMoveGrid gets `levelMapGridUnits()`, but no test reads the value back: ModelViewport is owned by the modeller session and has no getter. - **Physical input and other platforms.** Acceptance with physical keys and mouse and on macOS and Linux has not been done. - **Pre-existing Doom gap.** An unsnapped drag on a binary Doom map can still leave fractional in-memory coordinates that the binary writer rounds on save. This is unrelated to the grid. - **Translation catalogues need a refresh** for the new and changed strings (coordinator task).

### Package `linked-groups-trenchbroom`

Linked groups brought to TrenchBroom's model (2026-10-23). **Status: Partial.**

- Terminology follows TrenchBroom: groups that share content are **linked groups**, a set of them is a **link set**, and **Create Linked Duplicate** makes a member. "Linked copy", "instance" and "family" left the UI, help, CLI and docs, because no member is an original. Command ids are now `map.createLinkedDuplicate`, `map.selectLinkedGroups`, `map.updateLinkedGroups` and `map.separateLinkedGroups` (semantics `map.create-linked-duplicate` and so on); the TrenchBroom profile keeps Ctrl+Shift+D.
- Frames: each member's frame is a full 3x4 affine map (`LevelSceneNode::linkFrame`, TB's group transformation) instead of quarter turns and a mirror. `transformLevelMapSelection` records the one affine map it applied and the records it moved (`LevelMapUndoCommand::hasAffine`), so a closed member moved, rotated at any angle about any axis, mirrored, resized or sheared whole changes only its frame. Any edit inside an open member is a content edit, even one that moves all of it, as in TB. Propagation duplicates the source content and places it with `transformLevelMapSelectionAffine` (target frame after the inverse source frame), retrying without texture lock where a classic projection cannot express the frame. Copies, pastes and prefab placement move frames with their content; prefabs with linked groups now take any rotation.
- Nesting: groups nest in linked groups and are linked with their copies; sets update outermost first, nested sets skip groups an outer update just made, and a group cannot hold a member of its own set.
- Conflicts and locks: members changed in different ways by one edit keep their edits and are marked out of sync (`linkOutOfSync`, saved) with a notice (`LevelMapDocument::linkedGroupNotices`) shown in the status bar and Activity and printed by the CLI; TB refuses such an edit. Members changed the same way agree by object identity within 0.01 units. Locked members are passed by and marked; saving warns about members still out of sync.
- **Separate Linked Groups** keeps several chosen members of one set linked with each other (TB's relink); **Select Linked Groups** and `editor scene select-linked-groups` select a whole set; the 2D views draw the boxes, names and arrows of the sets in use, out-of-sync members dash-dotted, and **Show > Linked Groups** draws every linked group. The map views' right-click menu gains a **Groups** submenu (open/close group and every linked-group command), as TB's map view menu has.
- The Levels 3D camera draws the same boxes, names and arrows over its frame (`model_viewport_linked_groups.cpp`), with the boxes the main plan view solved and the rules, pen styles, labels and accessible text shared through `app/level_linked_group_boxes.h`; it follows **Show > Linked Groups**, clips edges to the near plane, and moves a name up to two lines out, or leaves it out, rather than cover the heads-up readouts, the selection's resize handles or another name. A closed group's box is meant to follow a camera drag of the selection as in 2D; no test covers that yet. `level-sidebar-ui-smoke` checks its description and drawing at 100% text and at 200% text in high contrast, right to left.
- Scene metadata version 6 stores frames and out-of-sync marks; versions 1 to 5 still read, legacy frames placed at each group's bounds centre. CLI operations take TB's names (`create-linked-duplicate`, `select-linked-groups`, `update-linked-groups`, `separate-linked-groups`, `extract-linked-groups`); `link`, `update-links`, `unlink` and `extract-linked` remain aliases.
- Tests: new `level-linked-sets-smoke` (30-degree turn and scaling with edits arriving in frame, open-group whole-content edits, three-level nesting with a child ahead of its parent in scene order, conflicts, locks, multi-member separation, selection, version 5 frames); updated `level-linked-groups-smoke`, `level-linked-objects-smoke`, `level-protected-properties-smoke`, `level-linked-extraction-smoke`, `level-group-transfer-smoke` and `level-sidebar-ui-smoke` (closed resize scales alone, open resize is shared, the plan view's accessible description names the arrows). Duplicate on a turned linked group is covered too: its frame moves with it and later edits land turned in place. All 214 level-related suites passed on 2026-10-23 (six UI suites needed a rerun with less parallel load). A mutation that drops the parent-first ordering of nested groups fails the new test. Related level, scene, prefab, clipboard, sweep, repeat, component, semantics and profile suites pass.
- Limits left: extraction refuses nested groups; TrenchBroom's `_tb_linked_group_id`, `_tb_transformation` and `_tb_protected_properties` keys are neither read nor written; Select Linked Groups selects members nested in other closed groups as their outer group; native physical input, assistive technology and macOS/Linux acceptance remain open. VibeStudio is pre-alpha and this is largely untested in real projects.

### Package `layer-commands`

Layer commands (lane2, 2026-10-22). Shipped NetRadiant Custom's Expand Selection to Layers and its layers menu, which TrenchBroom offers as a Move to Layer submenu of the view context menu. `src/core/level_scene_layers.{h,cpp}` holds the layer resolver: an object's own membership, else its non-worldspawn owner's, walked up through groups, with root groups in Default. Scene visibility (`level_visibility.cpp`) now uses the same resolver. Expansion adds the visible, unlocked, unfiltered objects of the selected objects' layers through pickLevelSceneObjects in Add mode. Closed groups stay whole or are left out and counted, an open group limits the result, and brush entities join through their brushes. It runs on the cancellable worker (`LevelObjectSelectionMode::Layers`) and records a selection step only in the TrenchBroom profile. Move to Layer is one scene undo step. Closed groups move whole, a brush takes its ungrouped brush entity along and drops its pinned membership, locks, open groups, stale metadata and non-layer targets are refused with reasons, objects moved into a hidden layer are deselected, and a no-op succeeds with no step. Commands `map.expandSelectionToLayers` and `map.moveToLayer` appear in the Edit > Layers menu, the Select menu above the views, the Tools tab, command search, the right-click menu and the popup at the pointer. The NetRadiant Custom profile binds Shift+Q and Ctrl+Shift+Q (NRC's Ctrl+Q is Quit). The CLI adds `editor scene select --expand-layers` and `editor scene move-to-layer`. Tests: level-scene-layers-smoke (core and CLI) and level-scene-layers-ui-smoke (three profiles at 100% and 200% with high contrast, expanded translator and RTL, plus Qt Test key presses) pass. Related scene, group-context, object-selection, entity/type-selection, visibility, sidebar, semantics and profile tests pass; level-visibility-ui-smoke needed 275 s under load against its 240 s limit. Limits left: VibeStudio layers hold nested groups rather than NRC's nested layers; Doom lines and vertices join a layer only when assigned explicitly; there is no per-layer colour or New Layer entry in the popup; native physical input and macOS/Linux acceptance remain open.

Remaining: Doom lines and vertices do not yet take their sectors' layer for expansion, as the open-group context already does. The popup has no **New Layer…** entry that creates a layer and moves into it in one undo step, and groups are not offered as Move destinations. Native physical-input and macOS/Linux acceptance remain open.

### Package `directional-nudge-commands`

Directional and depth nudges (work package directional-nudge-commands, lane 1). New core module src/core/level_nudge.{h,cpp} turns eight commands into a unit world axis. In plan views it uses screen right and up, and the depth axis towards the viewer (Top +Z, Front -Y, Side +X). In the camera it follows TrenchBroom's MapView3D::moveDirection: Forward is the nearest horizontal axis, or the camera's up axis when looking straight down or up; Right is the snapped right axis, turned a quarter about Z if it lands on Forward; depth and Z are world Z. Doom maps are limited to X and Y. Radiant-family profiles nudge from the camera in the last plan view's axes, as NetRadiant Custom does. src/app/level_nudge_actions.cpp registers map.nudgeLeft/Right/Forward/Back, map.nudgeDepthUp/Down and map.nudgeWorldUp/Down. They appear in Edit > Nudge, the Tools tab and the palette, and are kept enabled only while they have something to move. A nudge moves the first active target, in TrenchBroom's order: sweep destination, component pivot (Rotate Components / Move Component Pivot), picked components (the plan view's own arrow path with split and extrusion, or Transform Components), then the selection. Each map edit is one undo step through the normal services, so scene locks, open-group context, Texture Lock and component UV lock apply. The views claim nudge keys through StudioCommandRegistry::setShortcutScopeClaims and ApplicationShell::handleLevelNudgeKey. Go Back keeps Alt+Left outside the views, Doom maps keep Page Up/Down for Raise and Lower Floor, the views keep their pan, drag and camera fly/drive keys, and arrows still pan when nothing is selected. Profile keys: TrenchBroom has the arrows and PgUp/PgDn; the Radiant family (NRC, GtkRadiant 1.4-1.6, NetRadiant, Q3Radiant, QeRadiant, DarkRadiant, DoomEdit) has Alt+arrows and keypad +/-; every other profile has the bare arrows. Shift gives eight steps where that chord is free. `map nudge` uses the same service, resolving directions for a --view or a --camera-yaw/--camera-pitch frame, with --steps/--grid, --object or --set, texture/UV lock and --context; it writes only with --output. Tests: level-nudge-smoke and level-nudge-ui-smoke both pass, as do editor-profiles-smoke (expectation updated for the default profile's arrows), studio-semantics-smoke, level-profiles-ui-smoke, level-sweep-ui-smoke, level-component-transform-ui-smoke, level-geometry-tools-ui-smoke and level-transform-ui-smoke. shell-interaction-smoke times out at 600 s under load; run directly it completes with only the unrelated checkPaletteRecents 'Models' ranking failures. Limits left: - The Rotate Selection dialog's pivot is not nudged; TB's rotate-tool centre maps only to the component pivot. - Split and extrusion component nudges stay in a plan view's plane and are refused from the camera. - UI coverage uses synthetic offscreen key events, with camera focus simulated because the offscreen camera has no GPU backend. - Physical-keyboard (keypad, Alt chords) and macOS/Linux acceptance remain open.

Remaining: 1. Nudges do not move the pivot in the Rotate Selection dialog; only the component pivot stands in for TrenchBroom's rotate-tool centre. 2. Split and extrusion component nudges along a view's depth axis, or from the camera, are refused with a message rather than applied. 3. No acceptance has been done with physical keypad and Alt/Option chords, or on macOS or Linux.

### Package `filter-commands-and-invert`

Filter commands and Invert Filters (lane3, 2026-10-22). What shipped: src/core/level_view_filters.* now offers the full kind list (`levelViewFilterKinds`) with command ids (`levelViewFilterCommandId`). Radiant's world-class rule (`levelViewFilterWorldClass`) covers worldspawn, func_group and func_detail*. New Entities, Brushes and Func Groups kinds were added, and Brush Entities no longer includes func_group and func_detail. Point Entities leaves out lights and models. An entity counts as a brush entity when it owns brushes or patches or when the built-in Quake, Quake II or Quake III catalogue defines its class as a brush class. Invert is `levelViewFiltersInverted`, which keeps kinds saved for other formats, and the saved-list helpers sit beside it. src/app/level_view_filter_actions.cpp registers 20 checkable map.filter<Kind> commands plus map.invertFilters and map.resetFilters. It keeps them in sync with the View tab check boxes and the saved levelView/filtersOff list, and reapplies the filters to every plan view and the camera, with status-bar feedback. The View tab's Show Every Kind and Invert Filters buttons are those commands. The view bar's Select menu gains a Filters submenu. studio_semantics has 22 palette entries. The NetRadiant Custom and NetRadiant profiles bind Radiant's filter keys, audited against NRC 449778b3 and NetRadiant b4b295d7; Ctrl+P stays Go to File. `map filters` (src/cli/level_view_filters.*) is a read-only query over the same service. Docs updated: docs/manual/levels.md, docs/manual/cli.md, docs/CLI_STRATEGY.md, docs/LEVEL_EDITOR.md and the docs/CREDITS.md section 'View filter commands and Radiant's world rule'. Tests: the new level-view-filters-smoke and level-view-filters-ui-smoke pass. Related tests also pass in builddir-lvl-c: studio-semantics-smoke, editor-profiles-smoke, level-palette-coverage-ui-smoke, level-profiles-ui-smoke, level-visibility-smoke, level-visibility-ui-smoke, level-sidebar-smoke, level-sidebar-ui-smoke, level-view-workspace-ui-smoke and level-scene-layers-ui-smoke. level-profiles-ui-smoke needed a rerun after one timeout under load. The source-layout, CLI-docs and docs validators pass. Limits left: - Radiant kinds VibeStudio does not filter yet: areaportals/visportals, translucent, clusterportals, structural, lightgrid, botclips and decals. - The GtkRadiant 1.4/1.5 profiles have no filter keys; they were not audited. - Point and brush classes come from the built-in catalogues, not from a project's .def, .fgd or .ent files. - Objects that are already selected stay selected when their kind is filtered. - Native physical-input and macOS/Linux acceptance remain open.

Remaining: 1. Radiant filter kinds VibeStudio does not offer yet: Areaportals/Visportals (Alt+3), Translucent (Alt+4), Clusterportals (Alt+9), Structural (Ctrl+Shift+D), Lightgrid, Botclips (Alt+M) and Decals (Shift+D); NRC's filter toolbar and its context options are also missing. 2. The GtkRadiant 1.4/1.5 profiles have no filter keys. 3. Point and brush classes come only from the built-in catalogues, not from the project's loaded definitions. 4. Filtering a kind does not deselect objects that are already selected. 5. Native physical-input and macOS/Linux acceptance remain open.

### Package `reveal-material-and-find-replace`

Reveal material and Find and Replace Textures (lane2, 2026-10-22). Shipped: - Core module: src/core/level_material_find.{h,cpp}. - levelSelectionMaterial reads picked faces and query-picked patches first, otherwise the selection's brushes, patches, entities, Doom lines and sectors, and stops at the second material. - previewLevelMaterialReplace and replaceLevelMapMaterial work over Map, Selection or Picks scopes. Hidden, view-filtered, locked and out-of-context uses are skipped and counted; closed-group members are still replaced. - Edits go through the new replaceLevelMapTextureUses, which shares its undo and validation routine with replaceLevelMapTexture. levelMapTextureSurfaces exposes the existing scope traversal. - LevelTexturedSelectionRequest::withinSelection backs Find within the selection. - **Reveal Material** (map.revealMaterial; palette, Edit, Tools > Surfaces, the Surfaces menu and the object menu) shows the Textures tab, clears a filter that hides the tile, then selects, scrolls to and makes current the shared material. A mixed selection disables it, and the status tip, tooltip and accessible description say why. - **Find and Replace Textures…** (map.replaceTexture) is a modeless LevelFindReplaceDialog: - Picks on the Textures tab, the Textures surface or from Reveal fill whichever field last had focus. - While it is open, tile activation fills that field instead of applying the texture. - It has a three-way scope and a live preview of matches, changes and skips. - **Find** reuses the Select Textured service; Doom maps select lines and sectors. - **Replace** is one undo step and the window stays open. - No profile key is bound, because TrenchBroom and NetRadiant Custom define none. - CLI: `map replace-texture` gained `--face`/`--context` and reports `scope`, `targets` and `skipped`. Hidden and locked uses are now skipped rather than failing the command. `map select-textured --within-selection` was added. Tests: - New level-material-find-smoke (core and CLI) and level-material-find-ui-smoke (3 profiles, 100%/200%, high contrast, expanded right-to-left) pass. - All seven mutants were killed and both controls passed. - These related tests pass: level-textured-selection-smoke/-ui-smoke, level-face-transform-smoke, level-map-smoke, level-scene-locks-smoke, level-material-paint-smoke/-ui-smoke, level-palette-coverage-ui-smoke, studio-semantics-smoke and editor-profiles-smoke. - shell-interaction-smoke: the updated checkReplaceTexture passes. Its remaining failures are checkPaletteRecents, which fails the same way with checkReplaceTexture excluded, and one intermittent modal-dialog timing check under load. Limits left: - No CLI for Reveal Material, which changes only the view. - TrenchBroom's camera context-menu "Reveal <material> in Material Browser" / "Copy Material Name" entries for the face under the pointer were not added. - The dialog's lists hold only the map's textures plus free text; other materials come from Textures picks. - Native physical input, screen readers and macOS/Linux acceptance remain open.

Remaining: 1. TrenchBroom's camera right-click entries for the face under the pointer (Reveal in Material Browser, Copy Material Name) are not implemented; Reveal is reachable from the plan view's object menu, Edit, Tools and the palette. 2. Reveal Material has no CLI, because it is a view operation. 3. Native physical input, screen-reader and macOS/Linux acceptance remain open.

### Package `view-relative-rotate-flip`

View-relative turns and flips (package view-relative-rotate-flip, lane 1, 2026-10-22). What shipped: - `src/core/level_quick_transform.*` now resolves TrenchBroom's Roll, Yaw and Pitch (clockwise and counter-clockwise) and Flip Horizontal/Vertical to one world axis for a plan view or a camera. It builds on the nudge axes, with the turn directions of TB's MapViewBase::rotationAxis and the flip axes of MapView2D/3D::flipAxis at 4c756be1. Plan-view results match the old Rotate 90° and flip tables exactly. - Four new commands: map.rollClockwise, map.rollCounterclockwise, map.pitchClockwise and map.pitchCounterclockwise, in the palette, the Transform menu and the Tools tab. - Rotate 90° Left/Right (yaw) and Flip Horizontal/Vertical now use the camera's frame when it has focus or is the only view. Radiant-family profiles keep the last plan view's axes. - While Sweep is on, a turn rotates the sweep destination by the rotation step (LevelSweepGizmo::rotateDestination) and flips are disabled. - Turns keep the grid quick-turn pivot. Flips gain TrenchBroom's half-grid pivot: `flipLevelMapSelection` with a pivot, `LevelPlacementRequest::mirrorPivot`, and the `LevelEditorControls::flipSnapsPivot` policy, also exposed as the `editing.flipSnapsPivot` gesture setting, a Controls row and the `flipPivot` field of `editor controls`. - The TrenchBroom profile binds Alt+Up/Down, Alt+Left/Right, Alt+PgUp/PgDown, Ctrl+F and Ctrl+Alt+F. The views claim them only while the command can act, so Go Back, Go Forward and Find keep them elsewhere. - New CLI command `map view-transform`, with --view or a camera, and --pivot or --pivot-grid. - Every edit is one undo step through the normal Rotate and Mirror placements, so texture lock, Repeat, scene locks, hidden objects and open-group context apply. Tests and results: - New tests level-view-transform-smoke (core and CLI, byte-exact against the shared service) and level-view-transform-ui-smoke (TB, NRC and VibeStudio profiles at 100% and at 200% with high contrast, expanded translations and RTL) both pass. - 27 related tests pass: axis-transform, nudge, transform, rotation, doom-mirror, udmf-transform, repeat, gestures, profiles, palette coverage, sweep, component-transform, editor-profiles, semantics, map and placement, plus a shell-interaction subset. - level-gestures-smoke needed its two field counts updated to 102 for the concurrent field additions. - validate_source_layout, validate_cli_docs and validate_docs pass. Limits left: - No counterpart to TrenchBroom's Rotate-tool angle or centre; VibeStudio has no object rotate tool. - Flips are disabled only during Sweep rather than for every modal tool, and component mode flips the picked components. - With nothing selected, Alt+Left and Ctrl+F in a view still reach Go Back and Find. - The keys of these commands now act only while a Levels view has focus, which also affects Hammer/J.A.C.K. keys for them. - NetRadiant Custom's full-grid flip pivot is not mirrored. - The CLI handles objects only; components go through `map transform-components`. - Native physical input and macOS/Linux acceptance remain open.

Remaining: 1. TrenchBroom's Rotate-tool angle and centre for roll, yaw and pitch: VibeStudio has no object rotate tool, so quick turns are always 90°; component mode honours a custom component pivot. 2. TrenchBroom disables flips while any modal tool is active; VibeStudio disables them only while Sweep is on. 3. NetRadiant Custom's flip pivot snaps to the full snap grid; the NRC profile still flips about the exact centre. 4. `map view-transform` has no component `--set` option; components go through `map transform-components`. 5. With nothing selected, Alt+Left, Alt+Right and Ctrl+F in a TrenchBroom-profile view reach Go Back, Go Forward and Find; TrenchBroom does nothing there. 6. Native physical-input and macOS/Linux acceptance remain open.

### Package `camera-focus-commands`

Camera focus commands (2026-10-22, lane3). The shared service `src/core/level_camera_focus.*` provides three things: levelling (nearest 22.5-degree heading, zero pitch); a frustum box fit along the current view (all eight corners inside the side planes for the camera's vertical field of view and aspect, at least 64 units from the box centre, with an 8-unit near clearance); and a geometric wheel offset (2^(-steps/8), ±48 notches). Only existing, visible objects count towards the box. `src/app/level_camera_focus_actions.cpp` registers **Level Camera**, **Focus Camera on Selection** and **Reset Camera Zoom** under **View** > **Camera**, in the **Tools** tab's **Navigate** group and in the palette (`map.level-camera`, `map.focus-camera-on-selection`, `map.reset-camera-zoom`). They move only the level camera: plan views, map bytes, revision and undo history stay unchanged. With nothing selected, focus falls back to the last selection of the current map load, filtered by the open group. Status messages, which are announced to screen readers, report each result. The held free-focus key (`CameraViewControls::focusHoldKey`, gesture field `camera.focusHoldKey` on the **Camera Keys** tab) is handled in the shell's event filter. It acts only during mouse look; it re-places the camera on every view change, takes over the wheel, and ends on release, leaving mouse look, focus loss or hide. That is why Tab is safe for it (`normalizeCameraFocusKey()`), while Tab keeps moving keyboard focus everywhere else. Profile keys: End in netradiant-custom, netradiant, gtkradiant-1-4/1-5/1-6, q3radiant and doomedit; QeRadiant and DarkRadiant are unbound pending an audit. Tab is the free-focus key in netradiant-custom and netradiant, and Ctrl+Alt+Z is Reset Camera Zoom in trenchbroom. The query `map camera-focus` (`src/cli/level_camera_focus.*`) exposes the same service and refuses hidden objects. Tests: level-camera-focus-smoke covers the core rules, keys, profiles and CLI. level-camera-focus-ui-smoke covers the real shell under NRC and TB at 100% and at 200% with high contrast, RTL and expanded text. Both pass. Related suites pass: level-gestures, editor-profiles, studio-semantics, large-grid, navigation, nudge, gestures-ui, palette-coverage-ui, profiles-ui, view-links-ui, views-ui and sidebar-ui. In shell-interaction-smoke only the three checkPaletteRecents checks fail, and they were already failing before this package. Limits left: Focus Camera on Selection has no default key (a window-wide Tab would break keyboard navigation). The free-focus offset is geometric and never passes through the box centre, unlike NRC's piecewise offset. The focused box is measured once when the key goes down. Reset Camera Zoom goes through `restoreNavigationState()`, so mouse look is ended and restarted. Physical-input, screen-reader and macOS/Linux acceptance remain open.

Remaining: **Focus Camera on Selection** has no default key in the NRC profile, because a window-wide Tab would break keyboard navigation; Tab only focuses during mouse look. The free-focus wheel uses a geometric curve and never passes through the box centre, unlike NRC's piecewise offset. The focused box is measured once when the key goes down. QeRadiant and DarkRadiant have no End key until their sources are audited. **Reset Camera Zoom** restarts mouse look, because it goes through the modeller-owned viewport's navigation restore. Native physical keyboard and mouse, screen-reader and macOS/Linux acceptance remain open.

### Package `radiant-texture-step-commands`

Radiant texture step commands (2026-10-22, lane 2). What shipped: - Core: `RadiantScale`, `RadiantRotate` and `levelSurfaceRadiantScaleStep()` in core/level_surface.{h,cpp}, following NRC 449778b3's Texdef_Scale/Texdef_Rotate, Valve220_rotate, BPTexdef_Scale/BPTexdef_Rotate and Patch_convertScale (original code). - Classic and Valve 220 scale values take the increment and flip sign at zero. - Brush-primitive rows read a signed scale in texels and are rescaled by old over new. - Patches multiply by a positive step or divide by a negative one, about 0,0. - The texture-origin turn adds to the stored rotation, turns Valve 220 axes about their own normal, and turns brush-primitive plane coordinates. Patches turn in texels about 0,0. - The centre-anchored Rotate (quick buttons, Surface Alignment, `map align-textures --degrees`) is now the same turn plus centre re-anchoring. Valve 220 and brush-primitive faces therefore turn in NRC's direction, and brush-primitive turns no longer need an image size. - Surfaces tab: **Rotate about** (Face centre / Texture origin) and **Scale step** with **Scale U/V −/+**, plus four palette commands, `map.surfaceScaleStep{Left,Right,Down,Up}`. - Profiles: `LevelEditorControls::surfaceRotationStep` and `surfaceRotateAboutOrigin` (NRC: 45° about the origin) seed the tab at startup and on each profile change. - The NRC profile binds Shift+PgDown and Shift+PgUp to Rotate Texture − and +, and Ctrl+arrows to the scale steps, ahead of 2D-view Ctrl+arrow panning whenever a target enables them. - The netradiant baseline drops these keys. - The controls reference shows **Texture rotation step**, and `editor controls` reports `surfaceRotationStep` and `surfaceRotationPivot`. - CLI: `map align-textures --scale-step U,V` and `--degrees N --anchor centre|origin`. Tests: - New: level-surface-step-smoke and level-surface-step-ui-smoke, both passing. - Existing related tests, all passing: level-surface-smoke, level-surface-batch-smoke, level-patch-tools-smoke, level-surface-quick-ui-smoke, level-surface-ui-smoke, level-component-surfaces-ui-smoke, level-palette-coverage-ui-smoke, editor-profiles-smoke, studio-semantics-smoke, level-gestures-smoke, level-view-transform-smoke, level-axis-transform-smoke, level-large-grid-smoke and level-scene-locks-smoke. - Validators: source layout, CLI docs, docs and translation extraction checks all pass. Limits left: - One Scale step and one Shift step cover both axes; NRC has separate U and V increments. - The steps are session values seeded by the profile. They are not persisted and cannot be overridden in Customize Gestures. - Patch turns use texels rather than NRC's normalised coordinates, so non-square images are not sheared as they are in NRC. - The Surface Alignment dialog offers only the centre turn. - NetRadiant b4b295d7's turn sign is unaudited. - Native physical-input and macOS/Linux acceptance remain open.

Remaining: 1. NRC has separate horizontal and vertical increments for scale and shift; VibeStudio uses one **Scale step** and one **Shift step** for both axes. 2. The Rotation step, Rotate about choice and Scale step are Surfaces tab session values seeded by the profile, not persisted and not in Customize Gestures or `editor gestures`. 3. The Surface Alignment dialog offers only the centre turn, with no origin anchor and no scale step. 4. Patch turns use texels about 0,0 rather than NRC's normalised coordinates, which shear non-square images. 5. Xonotic NetRadiant's turn sign was not audited, so the netradiant profile has no texture step keys. 6. NRC's TexShift Shift+arrow keys are still unbound in the NRC profile. 7. Native physical-input and macOS/Linux keyboard acceptance remain open. Independent verification passed with two minor notes: the UI suite runs close to its 300 s limit under load, and its locked-brush check depends on the command being enabled (the core test covers the refusal).

### Package `plan-pane-visibility`

Plan pane visibility (2026-10-22, lane3). This adds NetRadiant Custom's floating-window XY/XZ/YZ toggles (ToggleView, ToggleFrontView, ToggleSideView at 449778b3) as **Show Top View**, **Show Front View** and **Show Side View**, applied to VibeStudio's fixed splitter panes. What shipped: - **Core.** `LevelPlanPanes` in `src/core/level_editor_controls.*` holds the choice and refuses to hide the last shown pane. It also provides the pane id parser and the per-combination splitter key (the key for all three panes is unchanged). `StudioSettings::levelPlanPanes()` and `setLevelPlanPanes()` store it as `levels/planPanes`; an unreadable value shows every pane. - **GUI.** `src/app/level_plan_pane_actions.cpp` registers the three checkable commands (View menu, Layout menu, command search, and check boxes in the View tab's Layout section; no default keys, as upstream). Before the panes change it restores a maximised view and saves the current splitter sizes. The active plan and the last focused pane move to a shown pane, a row or column left empty hides, and each combination keeps its own proportions. Top/Front/Side View and the projection list reveal a hidden pane; Next 2D View and Cycle Map View skip hidden panes. Restoring a saved view with a hidden active plan activates the next shown pane. - **CLI.** `editor layout --panes top,front,side|all` validates the layout id and pane list before writing either, and reports `planPanes` and `planPanesApply`. Tests: - New `level-plan-panes-smoke` (core and CLI) and `level-plan-panes-ui-smoke` (NetRadiant Custom and TrenchBroom profiles, at 100% and at 200% high-contrast RTL with expanded text) both pass. - These related tests pass: level-views-ui-smoke, level-view-workspace-ui-smoke, level-navigation-smoke, studio-semantics-smoke and editor-profiles-smoke. - level-view-links-ui-smoke, level-bookmarks-ui-smoke, level-gestures-ui-smoke and level-palette-coverage-ui-smoke timed out (bookmarks also failed once on its 30-second camera wait) while other lanes were compiling, and all passed when rerun alone. - validate_source_layout, validate_docs and validate_cli_docs pass. Limits left: - NRC's floating windows are not reproduced. - Saved views do not record the pane choice. - `editor layout --panes` reaches a running GUI only at its next preferences application or restart. - Native physical input, screen readers and macOS/Linux acceptance remain open.

Remaining: NRC's floating-window style is not reproduced; panes stay in splitters. Bookmarks do not record the pane choice. A change made with `editor layout --panes` while the GUI is open applies at the next preferences application or restart. With a read-only settings file that already hides a pane, the pane cannot be shown again in that session. Native physical input, screen readers and macOS/Linux remain open.

### Package `duplicate-move-and-repeat`

duplicate-move-and-repeat (Partial): Six Duplicate and Move commands (map.duplicateMoveLeft/Right/Forward/Back/DepthUp/DepthDown, core/level_nudge.h levelDuplicateMoveCommands) resolve the nudge axis for the plan view or camera and send one Duplicate placement at one grid step, so each press is one undo step and one Repeat Last Commands entry with the copies selected; the TrenchBroom profile binds TB's Ctrl+arrows and Ctrl+Page Up/Down, claimed by the views only while a copy can be made (plan pan and Doom Raise/Lower Ceiling keep the keys otherwise). NetRadiant Custom's Repeat Transforms (map.repeatTransforms, Ctrl+R in the NRC profile) and Reset Repeat Transforms keep LevelRepeatTransforms (last translation, rotation, scale, skew) updated by every top-level object or component transform, surviving selection changes and undo; replay copies the selection and applies scale, skew, rotation about the copies' centre and translation as one undo step (LevelPlacementOperation::RepeatTransforms), or one Affine component transform in a component mode. Move to Camera (map.moveToCamera, Ctrl+Shift+V in the NRC profile) applies Paste to Camera's snapped centre rule to the live selection as one Move placement (LevelPlacementOperation::MoveToCamera), keeping identities and source spans. CLI: map nudge --duplicate, map move --camera, map repeat-transforms (src/cli/level_move_repeat.*). Tests: level-duplicate-move-smoke and level-duplicate-move-ui-smoke (TB, NRC and VibeStudio profiles at 100%/200%, high contrast, expanded translations, RTL) pass in builddir-lvl-b with werror, as do 24 related level tests (two UI suites needed a rerun alone after load timeouts). Limits: the object-mode Repeat Transforms pivot is the copies' centre (or --pivot), not a user manipulator pivot; shear and rotation compose in a fixed order; Repeat Transforms and Move to Camera are Quake-family only; NRC's Alt+R is not bound; native physical input and macOS/Linux acceptance remain open.

Remaining: 1. Object-mode Repeat Transforms pivots about the copies' bounds centre (or an explicit --pivot); there is no user-placed object manipulator pivot to honour yet. 2. NetRadiant Custom adds the skew term into the rotation matrix; VibeStudio applies scale, skew, rotation, translation in order, which differs only when both a skew and a rotation are recorded. 3. NRC's Alt+R (Reset Transforms) is not bound in the NetRadiant Custom profile (menu-bar mnemonic chord); Reset Repeat Transforms is in Edit > Repeat and the palette. 4. Repeat Transforms and Move to Camera are Quake-family text maps only; Doom maps use Repeat Last Commands and Paste/Move Selection. 5. Native physical-input and macOS/Linux acceptance remain open for all eight commands. The package was finished by a second agent that reviewed, rebuilt and re-tested the first agent's work.

## 2026-10-24: Parity wave 2

### Package `arbitrary-rotate-scale-dialogs`

### Package arbitrary-rotate-scale-dialogs **What shipped.** NetRadiant Custom's Arbitrary Rotation and Arbitrary Scale (select.cpp RotateDialog, ScaleDialog and the "ArbitraryRotation"/"ArbitraryScale" registrations at 449778b3) are now in the Levels editor. - **Euler rotation.** `LevelMapRotationRequest::euler` turns the selection about world X, then Y, then Z, about one pivot, following the order of `matrix4_rotation_for_euler_xyz`. It reuses the single-axis service once per axis on a guarded candidate and folds the turns into one undo step. One nonzero angle is exactly the old single-axis rotation. - **Repeat Transforms.** `LevelRepeatTransforms` gained an Euler slot, so Repeat Transforms repeats the whole turn. - **Scaling.** `scaleLevelMapSelection()` scales by X/Y/Z factors about the centre or a pivot. Negative factors mirror with winding repair and reflect headings as Flip does. It honours Texture Scale Lock and Valve 220 policy, scene locks and open groups. Doom maps scale in the top view only. - **Windows.** - **Rotate Selection…** and the new **Scale Selection…** share `LevelTransformDialog`: a modeless window with a pivot choice, texture options, a cancellable background preview, and OK / Apply / Close. - Apply commits one undo step and keeps the window open. The shell refresh hands the window the edited map, so the next preview and Apply use the refreshed selection; a stale commit is refused. - In component mode the windows preview and apply to the picked components through the Affine and Scale component transforms. - **Keys.** The NetRadiant Custom profile gets Shift+R and Ctrl+Shift+S. NRC's Save Map As has no key, and the studio's stays on Ctrl+Alt+S. - **CLI.** `map rotate --euler` and `map resize --factors [--pivot]` share the services. **Tests.** - New: `level-arbitrary-transform-smoke` (core, Repeat Transforms, locks, hidden objects, Doom and CLI) and `level-arbitrary-transform-ui-smoke` (three profiles at 100% and 200% with high contrast, expanded translation and RTL; modeless Apply/OK, selection following, component mode, key ownership, accessibility). - Adapted to the modeless window: `level-rotation-ui-smoke`, `level-transform-ui-smoke`, `level-udmf-transform-ui-smoke` and `level-component-transform-ui-smoke`. - All pass, together with the related core, CLI, profile, semantics, palette-coverage, repeat, axis-transform, placement, prefab and UDMF suites in builddir-lvl-a. **Limits left.** - The window's pivot fields show the object selection's centre even when components are picked; the component pivot or centre is what is used. - `map transform-components` and `map repeat-transforms --rotate` take one axis, so there is no Euler route for components or for seeding Repeat Transforms from the CLI. - Component factors stay within ±1024. - Native physical input and macOS/Linux acceptance are still open.

Remaining: 1. With components picked, the window's pivot fields show the object selection's centre rather than the picked components' centre or component pivot; the transform itself uses the correct component pivot. 2. `map transform-components` (one axis per rotate) and `map repeat-transforms --rotate axis,degrees` have no Euler form, so the CLI cannot turn picked components about several axes in one step, or seed an Euler Repeat Transforms recording. 3. Doom maps refuse turns about X or Y and Z factors other than 1. This is by design: Doom geometry lives in the plan. 4. Native physical input and macOS/Linux acceptance of the modeless windows and the Shift+R / Ctrl+Shift+S keys are untested.

### Package `make-room-options`

NRC CSG_MakeRoom caulk and open faces. **What shipped.** `core/level_hollow.*` adds wall options to `hollowLevelMapSelection` through `LevelMapHollowOptions` and `LevelMapHollowReport`. - `caulkTexture` renames every wall face except the room-facing one. Faces in an open face's plane keep its material, as NRC's CaulkFace exclusion does. - `openFaces` get no wall, and the walls beside them stop flush. This works in the overlap, touch and room modes. - `levelMapCaulkTexture` gives `common/caulk` for Quake III (`textures/common/caulk` for brushDef3), `skip` for Quake, and nothing for Quake II or Doom. - Each edit is one undo step and stays behind the scene-lock guard. A bad caulk name or an out-of-range face fails without changing anything. A brush with every face open is skipped with its reason. **GUI.** **Make Room** reads `LevelRoomPreferences` from `levels/makeRoom/*`. NRC's behaviour is the default: caulk on, picked faces open. Open faces come from `levelPickedSurfaceFaces()`: face mode, camera picks and Select Textured. The status bar names the caulk material and the number of openings. The new **Make Room Options…** dialog (palette `map.make-room-options`, the Tools tab's Brush section, the Geometry menu) has: - accessible controls and a summary of the material in use on the open map; - inline validation through the shared `levelRoomPreferencesProblem`; - **Restore Defaults**. **CLI.** `map hollow` takes `--caulk`, `--caulk-texture NAME` and repeated `--open-face face:brushId:faceNumber`. JSON adds `caulkTexture`, `caulkedFaces`, `wallCount`, `openFaces` and `openFaceCount`. The new `editor make-room` reads or changes the saved options with `--set`, `--reset` and `--dry-run`. The dialog, the CLI and the settings reader share `updateLevelRoomPreferences` and `levelRoomPreferencesProblem`; a hand-edited bad name reads as the game's caulk. **Tests.** `level-make-room-smoke` and `level-make-room-ui-smoke` are new; the UI test runs the NRC and TrenchBroom profiles at 100% and at 200% with high contrast, an expanded translation and RTL. Both pass in builddir-lvl-a. Related suites also pass: level-brush-tools-smoke, level-geometry-tools-ui-smoke, level-palette-coverage-ui-smoke, level-map-smoke, level-scene-locks-smoke, studio-semantics-smoke, editor-profiles-smoke, settings-smoke and level-clipper-options-smoke. validate_source_layout, validate_cli_docs (319 commands) and validate_docs pass. **Limits left.** - VibeStudio fills the outer corners, which NRC leaves open. - The GUI **Hollow…** and **Hollow Touching…** do not offer the caulk and open-face options; only `map hollow` flags apply them in those modes. - NRC's separate CSG Tool (-faces/+faces/-proj/-cam exclusion, Remove Inner) is not covered. - Games without a caulk material need a named one. - Native physical input and macOS/Linux acceptance remain open.

Remaining: 1. VibeStudio fills the outer corners of a room, where NetRadiant Custom leaves them open. 2. The GUI **Hollow…** and **Hollow Touching…** commands do not offer the caulk and open-face options. In those modes they are available only through the `map hollow` flags. 3. NetRadiant Custom's separate CSG Tool dialog is not implemented: its -faces/+faces/-proj/-cam exclusion modes and its Remove Inner toggle are missing. 4. Games without a caulk material, such as Quake II, need a named material. 5. Native physical input and macOS/Linux acceptance remain open.

### Package `csg-tool-panel`

csg-tool-panel (lane1, wave 2) shipped NetRadiant Custom's CSG Tool. Core: `LevelMapHollowMode::Diagonal` (mitred walls built as the part of the grown shell furthest outside each face, which tiles the shell exactly), `LevelMapHollowMode::Extrude` (prisms with sides square to the face), the `wrap`/`pull` names, `LevelMapHollowOptions::keepInner` (inner brush or core keeps the brush id; caulk moves to the faces between walls and inner brush), hidden-brush skipping in `hollowLevelMapSelection`, the face-policy helpers `levelCsgExcludedFaces` and `levelMapFacesFacingAxis`, and `offsetLevelMapSelectionFaces` for Shrink/Expand (one batch undo step through `replaceLevelMapComponentGeometry`), all in `core/level_hollow.*` and `core/level_map.cpp`. GUI: the Tools tab's `tools.csg` section (`app/level_csg_tool_actions.cpp`), whose options are saved under `levels/csgTool/*`, plus seven commands (`map.csgTool`, `map.csgShrink`, `map.csgExpand`, `map.csgHollowDiagonal|Wrap|Extrude|Pull`) with palette entries and Geometry-menu items. CLI: the new `map hollow` modes and flags, and `map shrink-brushes`/`map expand-brushes`. Tests: the new level-csg-tool-smoke (core and CLI) and level-csg-tool-ui-smoke (two profiles at 100% and at 200% with high contrast, translation and RTL) pass. So do level-brush-tools-smoke, level-make-room-smoke, level-make-room-ui-smoke, level-geometry-tools-ui-smoke, level-sidebar-smoke, level-sidebar-ui-smoke, studio-semantics-smoke and editor-profiles-smoke. validate_source_layout, validate_cli_docs (321 commands) and validate_docs pass. Limits left: pull keeps Make Room's filled corners where NRC leaves them open; Extrude sides take the face's own material, or caulk, rather than the texture browser's current material; caulk follows VibeStudio's room-or-solid rule rather than NRC's per-mode quirks; Shrink/Expand have no texture lock; the saved panel options have no `editor` CLI command; no default keys (NRC and TrenchBroom ship none); native physical input and macOS/Linux acceptance remain open.

Remaining: The pull hollow keeps Make Room's filled outer corners, where NetRadiant Custom leaves them open. Extrude side faces take the face's own material, or caulk, instead of the texture browser's current material. Caulk follows VibeStudio's single room-or-solid rule rather than NetRadiant Custom's per-mode differences. Shrink and Expand keep textures in world space and offer no texture lock, so a sloped face in the standard (paraxial) format can appear to slide. The CSG Tool's saved panel options have no `editor` CLI command; `map hollow`, `map shrink-brushes` and `map expand-brushes` take them as flags. No default key bindings, because NetRadiant Custom and TrenchBroom ship none. The tool is a sidebar section rather than a floating window. Native physical input and macOS/Linux acceptance remain open.

### Package `camera-relative-texture-moves`

Camera-relative texture moves and flips (package camera-relative-texture-moves, lane 2, 2026-10-24). What shipped: - **Core:** `src/core/level_camera_texture.*` holds the command table (map.moveTexturesUp/Down/Left/Right, their Coarse and Fine forms, map.flipTexturesHorizontally/Vertically), TrenchBroom's distances (grid size, twice it, one texel), request building and the per-face axis choice. Each face's texture gradients are laid into its plane and compared with the camera's right and up vectors; the better-aligned pairing wins, ties going to the axis nearer the world horizontal, then U. - **Surface service:** `LevelSurfaceOperation::CameraShift` and `CameraFlip` in `core/level_surface.*` resolve each face, or each patch (by its texture direction at the middle control point), to its own Shift or a Scale -1 mirror about the face middle. The shared worker, size resolution, 64-step bursts, cancellation, scene locks, linked groups and single undo step therefore apply. - **GUI:** Surfaces menu > **Move Textures from Camera**, a Tools tab group, an Edit menu section and 14 palette entries. Targets are the picked faces (TB face selection), otherwise the Surfaces target. - **Keys:** the TrenchBroom profile binds TB's defaults: arrows, Shift+arrows, Ctrl+arrows, Ctrl+F, Ctrl+Alt+F. `StudioCommandRegistry` claim layers let these camera-scoped commands share the nudge, Duplicate-and-Move and object-flip keys. `handleLevelNudgeKey` routes the key to the texture command in the camera while faces are picked outside component mode, and to the object command otherwise. Profile conflict checks treat the two as separate surfaces. - **CLI:** `map align-textures --camera-shift RIGHT,UP` and `--camera-flip horizontal|vertical` with `--camera-yaw`/`--camera-pitch`. Tests and results: - New tests: level-camera-texture-smoke (core and CLI, byte-exact against the service) and level-camera-texture-ui-smoke (TB at 100% and at 200% with high contrast, expanded text and RTL, plus NRC; real camera key presses) both pass. - Directly related suites pass: surface, batch, step, patch tools, nudge, view transform, duplicate move, editor profiles, semantics, gestures, camera focus, core, and the step, quick, surface, component-surfaces, nudge, view-transform, duplicate-move, palette-coverage, face-clipboard, profiles and entity-create UI suites. - shell-interaction-smoke, run alone, fails only its three baseline checkPaletteRecents checks. - validate_source_layout, validate_cli_docs, validate_docs and the docs site build pass. Limits left: - TrenchBroom's own per-face axis choice and mirror anchor live in its model library, which the pinned checkout lacks, so they were not compared. Flips mirror about each face's middle, as Flip U/V do. - Patches follow their texture direction at the middle control point. - Only the TrenchBroom profile has keys. - Moves are refused while a geometry tool is busy. - Native physical input, screen readers and macOS/Linux have not been accepted. Status: Partial; VibeStudio remains pre-alpha.

Remaining: 1. TrenchBroom's per-face UV axis choice (`mdl::translateUV`) and mirror anchor (`mdl::flipUV`) are in its model library, which the pinned checkout (TbUiLib only) does not include, so they were not compared. VibeStudio picks the axis by alignment with the camera's right and up vectors, breaking ties towards the world horizontal and then U, and flips mirror about each face's middle. A texture running diagonally to the view, or the offset after a flip, can therefore differ from TrenchBroom. 2. A patch follows its texture direction at its middle control point, so a strongly curved patch moves along that local direction. 3. Only the TrenchBroom profile binds keys; NetRadiant Custom has no camera-relative equivalent, and other profiles leave the commands unbound. 4. TrenchBroom allows texture moves with any tool active; VibeStudio refuses them while a geometry tool is busy (clip, brush drawing, dragging), as it does for all quick surface edits. 5. The commands cover Quake-family maps only; Doom walls keep the separate crosshair texture nudges. 6. Native physical keyboard input, screen readers and macOS/Linux have not been accepted; the camera key checks use synthetic offscreen events.

### Package `tb-texture-rotate-steps`

### Package tb-texture-rotate-steps **What shipped.** The camera texture table (`core/level_camera_texture`) gains six fixed-step turns: `map.rotateTexturesClockwise` and `map.rotateTexturesCounterclockwise`, each with `Coarse` and `Fine` forms. They turn 15° (TrenchBroom's fixed grid angle), 90° and 1°. They have palette entries, a **Rotate Textures** submenu under the Levels **Surfaces** menu and a Tools tab group. The TrenchBroom profile binds Page Up and Page Down in the camera, with Shift for 90° and Ctrl for 1°, in the existing `camera-faces` claim layer. They act on the picked faces, or on the Surfaces target when no face is picked; with no face picked the keys still nudge and copy along the depth axis. No camera is needed. The edit is the new `LevelSurfaceOperation::ClockwiseRotate` (`core/level_surface`). It is NetRadiant Custom's turn about the texture origin, which keeps the offsets, with the sign chosen per face. A positive Radiant turn goes different ways on screen depending on the face: classic faces turn about their projection axis whatever their scale signs, Valve 220 faces about their stored axes, brush-primitive faces always anticlockwise, and patches in texture space. So the service turns a copy a quarter turn first and reads which way it went with `levelTextureTurnSense()`. Collapsed mappings are refused. It goes through the shared surface queue, so it is one undo step with scene locks, cancellation, hidden-object and target rules. `map align-textures --clockwise N` is the CLI route; JSON reports `operation` `clockwise`, `anchor` `origin` and `clockwiseDegrees`. **Tests.** `level-camera-texture-smoke` measures the turn on screen from the picture's own movement, independently of the service. It covers classic, Valve 220, brushDef and brushDef3 faces, each the right way round and mirrored, at 15°, 90°, 1° and −15°, plus a patch both ways round. It also checks that offsets stay unchanged, that patch coordinates turn about 0,0, exact undo and redo, that a whole turn changes nothing, locked-layer refusal, and that `--clockwise` writes byte for byte what the service writes. `level-camera-texture-ui-smoke` covers the six keys with a picked face, Page Up nudging the brush when no face is picked, the menu routes on the Surfaces target and on a picked face, the submenu, the Tools tiles and the keys across scale, high-contrast, translator, RTL and NetRadiant Custom passes. Results: - Pass: both of those suites, `level-surface`, `level-surface-step` (core and UI), `level-surface-quick-ui`, `level-surface-batch`, `level-patch-tools`, `level-nudge` (core and UI), `editor-profiles`, `studio-semantics`, `level-sidebar` (core and UI). - Pass: the source-layout, CLI-docs and docs validators, and the docs site build. **Limits left.** - TrenchBroom's `mdl::rotateUV` is not in the pinned checkout, so its own on-screen direction per face format was not compared. VibeStudio guarantees the direction as seen from each face's front instead. - In vertex, edge and face editing the keys stay with the component nudges. - Patches need the image size. - Native physical input and macOS/Linux acceptance remain open.

Remaining: 1. TrenchBroom's mdl::rotateUV (its model library) is not in the pinned v2026.2 checkout, so its exact on-screen direction per face format and its rotation-value arithmetic were not compared. VibeStudio guarantees the direction as seen from each face's front instead, and stores the result as NetRadiant Custom's turn about the texture origin. 2. TrenchBroom's keys work with any tool active. In VibeStudio's vertex, edge and face editing modes, Page Up and Page Down stay with the component nudges. 3. Turning a patch needs the material's image size. 4. Native physical input and macOS/Linux acceptance remain open.

### Package `texture-reset-commands`

### Package texture-reset-commands **Resets.** Two new operations, `Reset` and `RadiantReset` (`src/core/level_surface.*`), reset any mix of face formats in one pass and one undo step at the format's default scale, with no shift or rotation. A sloped classic face, which Align to Face refuses, is included. - `Reset` (TrenchBroom): classic faces go to their natural numbers, Valve 220 and brush-primitive faces to Align to Face, patches to their natural spread. - `RadiantReset` (NetRadiant Custom): Valve 220 and brush-primitive faces take their plane's own basis through the new `levelPlaneBasisTextureProjection()` in `src/core/level_texture_mapping.*` (q3map2's ComputeAxisBase; a unit matrix over it for brush primitives). Patches get the planar cap. Both are quick surface commands that act on the picked faces first, otherwise on the Surfaces target: - **Reset Texture Alignment**: `map.surfaceReset`, Shift+R in the TrenchBroom profile. - **Radiant Texture Reset**: `map.surfaceRadiantReset`, Shift+N in the NetRadiant Custom profile. - The TrenchBroom profile also binds Shift+Alt+R to Natural Texture Mapping. They appear as Surfaces tab buttons and in the Surfaces menu, the Tools tab and the palette. The CLI equivalents are `map align-textures --reset-alignment U,V` and `--radiant-reset U,V`. **Reset Textures…** (bobToolz) is a core service: `LevelTextureResetRequest`, `levelTextureResetTargets()` and `prepareLevelTextureReset()`. - **Scope:** the selection, or the whole map's visible, unlocked surfaces in the open group, leaving out objects the views hide. - **Filter:** optional, by texture name, ignoring case and a leading `textures/`. - **Rename:** optional. - **Values:** absolute U/V scale, U/V shift and rotation for the ticked fields, each face in its own format. Brush-primitive faces need the image size. Patches take only the name. It commits through `commitLevelSurfaceEdit`. That function now: - checks renamed faces; - keeps a rename-only face's numbers exactly as written; - records an `edit-brush` step, which rebuilds the texture list, when names change. The GUI dialog (`src/app/level_texture_reset_dialog.*`) runs the reset on the surface worker. The CLI is a Reset Textures mode of `map align-textures`. **Fix.** Hide and Show All no longer stay disabled after a surface batch that changes nothing. **Tests.** The new `level-texture-reset-smoke` and `level-texture-reset-ui-smoke` pass. Of the existing surface, profile, palette, visibility and geometry suites run, all pass. `level-palette-coverage-ui-smoke` needed a rerun alone because the machine was loaded. Validators pass. **Limits left.** - The default scale is the format's, not a per-game configuration value. - TrenchBroom's `mdl::resetAll` is not in the pinned checkout, so its per-format axes were not compared. - The Reset Textures window has no live preview. - At most 262,144 faces and 65,536 patches per reset. - Native physical-input and macOS/Linux acceptance remain open.

Remaining: 1. **Default scale:** Reset, RadiantReset and Natural use the map format's scale (0.5 Quake III, 1 otherwise), not a per-game configuration value. 2. **TrenchBroom axes not compared:** TrenchBroom's mdl::resetAll is not in the pinned checkout, so its per-format UV axes were not compared. The VibeStudio reset uses Align to Face's in-plane axes for Valve 220 and brush-primitive faces. 3. **Patches under Reset Texture Alignment:** they get their natural spread, though TrenchBroom's reset leaves patches alone. 4. **Picked faces:** with faces picked, the resets act only on those faces, whereas NetRadiant Custom outside component mode also resets every face of the selected brushes. Natural (TrenchBroom's Shift+Alt+R) acts on the Surfaces target rather than first on picked faces. 5. **Reset Textures window:** it has no live preview, brush-primitive faces need the material's image size, patches take only the new name, and one reset handles at most 262,144 faces and 65,536 patches. 6. **Acceptance:** native physical-input and macOS/Linux acceptance remain open for all four commands.

### Package `filter-texture-kinds`

Texture filter kinds (package filter-texture-kinds, lane3, 2026-10-24). What shipped: src/core/level_view_filters.* gains six kinds, matched on texture names and on the content bits the map carries: - Area Portals: any areaportal face or CONTENTS_AREAPORTAL 0x8000, plus Quake II func_areaportal entities. - Cluster Portals: every face clusterportal, or Quake III's 0x100000. - Light Grid: every face lightgrid. - Bot Clip: every face botclip or donotenter*, or Quake III's 0x400000 or 0x200000. - Decals: any face under decals/, with or without a leading textures/. - Structural: the inverse of Detail. On Quake II and III maps, every brush with no detail face outside func_detail*. On Quake maps (builtinEntityGameForMap), worldspawn's brushes and every brush entity but func_detail*, as NRC's filter_entity_not_func_detail. Cluster Portals, Light Grid, Bot Clip and Decals are offered only on Quake III maps. On those maps bot clip leaves Clip. On Quake maps, skip moves from Hint and Skip to Caulk. Caulk also hides faces one at a time: levelViewFilterFaceMatches and levelViewFilteredFaces list the caulk faces of brushes that are not caulked all over. ApplicationShell::refreshLevelMap3D passes them to the camera through LevelPreviewRequest::hiddenFaces and LevelMapPreviewMeshOptions::hiddenFaces, so the camera neither draws nor picks them. The status bar counts them. The plan views keep the whole outline, as NRC's wireframes do. The kinds become map.filter<Kind> checkable commands automatically: View > Filters, the view bar's Select > Filters, command search and View tab check boxes with counts. Six palette entries were added. The NetRadiant Custom and NetRadiant profiles bind Alt+3, Alt+9, Ctrl+Shift+D, Alt+M and Shift+D, audited against NRC 449778b3 and NetRadiant b4b295d7. The NRC profile's dead Toggle Caulk Filter placeholder was removed, so its Caulk binding is the real map.filterCaulk on Alt+6. `map filters` reports a per-kind faceCount, --objects faces, and filteredFaces/filteredFaceCount as face:brushId:faceNumber selectors. Docs updated: docs/manual/levels.md, docs/manual/cli.md, docs/CLI_STRATEGY.md, docs/LEVEL_EDITOR.md, and docs/CREDITS.md (an addendum to 'View filter commands and Radiant's world rule'). Tests: - New level-filter-texture-kinds-smoke covers Quake, Quake II and Quake III fixtures, caulk faces, the camera mesh, keys and the CLI. It passes. - New level-filter-texture-kinds-ui-smoke runs NRC and TB at 100% and at 200% with high contrast, expanded text and RTL. It checks commands, check boxes, keys pressed with Qt Test, the camera without caulk faces and Quake enablement. It passes. - Updated level-view-filters-smoke and level-view-filters-ui-smoke pass. - Related tests pass: studio-semantics-smoke, level-palette-coverage-ui-smoke, level-visibility-smoke, level-sidebar-ui-smoke, level-geometry-cache-ui-smoke, map-geometry-cache-smoke, level-materials-smoke, level-materials-ui-smoke and doom-preview-ui-smoke. level-visibility-ui-smoke passed when rerun alone after one timeout under load. - editor-profiles-smoke and level-profiles-ui-smoke fail only on another lane's TrenchBroom texture-move key clashes. - shell-interaction-smoke fails only its three known checkPaletteRecents checks. - validate_source_layout, validate_cli_docs and validate_docs pass. Limits left: - The plan views keep the outline of brushes with hidden caulk faces. Plan-view face component mode, Select Textured and the texture tools still reach those faces, while NRC's face selection skips them. - Kinds are matched by texture name and the map's content bits, not by shader surfaceparms. - Structural follows VibeStudio's any-face detail rule, where NRC needs every face. Bot Clip accepts a brush mixing botclip and donotenter faces. - Area Portals is also offered on Quake maps, which have none. - Translucent (Alt+4) is still missing. - Native physical input, screen readers and macOS/Linux acceptance remain open.

Remaining: While Caulk is off, the plan views keep the whole outline of a brush with hidden caulk faces, as NetRadiant Custom's 2D wireframes do. Plan-view face component mode, Select Textured and the texture tools can still pick or change those hidden faces, while NRC's face selection skips filtered faces. 2. Kinds are recognised by texture name and by the content flags written in the map, not by shader surfaceparms (for example a shader marked areaportal, clusterportal, lightgrid or detail under another name). 3. Structural follows VibeStudio's Detail rule, where any detail face makes a brush detail; NRC needs every face. Bot Clip accepts a brush mixing botclip and donotenter faces, where NRC needs one texture on every face. 4. Area Portals is offered on Quake maps too, where it never matches anything; Doom 3 visportals stay out of scope. 5. NRC's Translucent kind (Alt+4) is still not offered. 6. Native physical input, screen readers and macOS/Linux acceptance remain open.

### Package `filter-material-library-kinds`

Package filter-material-library-kinds (wave 2, lane 3). What shipped: - The Levels view filters can now tell what a surface is made of, not just what it is called. - src/core/level_view_filters.* gains LevelViewFilterShader, LevelViewFilterMaterials, levelViewFilterShader(), levelViewFilterMaterials(), scanLevelViewFilterMaterials() and levelViewFilterReadsMaterials(). These read Quake III scripts and textures/ images through the existing material library (scanMaterialLibrary, Quake III only). The winning definition of each name sets Radiant's editor flags, following NRC plugins/shaders/shaders.cpp: qer_trans, surfaceparm fog and fogParms are translucent; qer_alphafunc and a stage's alphaFunc are alpha-tested; water, slime and lava are liquid; skyParms or surfaceparm sky is sky; playerclip, monsterclip, botclip, donotenter and nodraw set their own kinds. - Every filter query takes these materials. On Quake III maps, a face the package resolves goes by its shader: Sky and Liquids by the shader alone, keeping NRC's textures/liquids/ prefix. Unresolved faces keep the name rules. - New kinds: Translucent (any face, brushes and patches; Quake II SURF_TRANS33/66; Quake `{` fences), Player Clip, Monster Clip and Weapon Clip (every face, patches included) and Nodraw (every face: SURF_NODRAW, surfaceparm nodraw or a nodraw texture). Clip and its parts take clip-shaded patches, following NRC PatchFilters_construct. - The shell reads the shaders on a cancellable QThread from the same package snapshot the camera uses, staged edits included. The read is keyed by packageViewKey(), so a new map, package, staged edit, **Reload Shaders** or the camera's **Reload** starts a fresh read. Counts, plan views and the camera update when it finishes. - An accessible status line in the **Filters** section says how many shaders were read, or how the kinds fall back without a package. - map.filterTranslucent, map.filterNodraw, map.filterPlayerClip, map.filterMonsterClip and map.filterWeaponClip are checkable View > Filters commands with palette entries. Alt+4 runs Translucent in the NetRadiant Custom and NetRadiant profiles. - `map filters --package` runs the same scan and reports a `shaders` block and per-kind `readsShaders`. Tests: level-filter-material-kinds-smoke covers shader flags, the scan with shadowing and cancellation, classification on Quake III, Quake II and Quake maps with and without shaders, keys, palette entries and the CLI. level-filter-material-kinds-ui-smoke covers the worker and status line, counts, commands, Alt+4, the camera, Reload and the fallbacks in the NetRadiant Custom and TrenchBroom profiles at 100% and 200%, with high contrast, expanded translation and right-to-left layout. It also checks that the map, selection and undo never change. Both pass. The related suites pass: level-view-filters smoke and UI, level-filter-texture-kinds smoke and UI, studio-semantics, editor-profiles, level-visibility smoke and UI, level-sidebar smoke and UI, level-view-workspace UI, level-profiles UI and level-palette-coverage UI. shell-interaction-smoke fails only its three pre-existing checkPaletteRecents checks. Limits left: - Shaders come only from the open package or folder, the same source the camera uses. pk3 archives inside an opened folder and other game search paths are not stacked; those faces fall back to name rules. - Weapon Clip is judged by texture names, because Quake III has no weapon clip surfaceparm. - Clip, Nodraw and the clip parts need every face, where NRC's UFO:AI toggles judge by the last face. - Native physical input and macOS/Linux acceptance remain open.

Remaining: 1. Shader scripts come only from the open package or folder, with its staged edits; this is the same source the camera uses. pk3 archives inside an opened folder and other game search paths are not stacked, so those faces fall back to the name rules. Without a package, Translucent finds nothing on a Quake III map. 2. Weapon Clip is judged by texture names only, because Quake III has no weapon clip surfaceparm. 3. Clip, its parts, Nodraw and Bot Clip need every face, where NRC's UFO:AI toggles judge by the last face; there is no UFO:AI game profile. 4. The material kinds hide whole brushes and patches, never single faces; only Caulk hides faces one at a time. 5. In Quake maps the parser reads `{` as a brace, so a `{` fence texture name must be quoted to count as translucent. This is a parser limit, not new behaviour. 6. Native physical input and macOS/Linux acceptance remain open.

### Package `camera-face-render-modes`

### Package camera-face-render-modes **What shipped** - `src/core/level_camera_render.*` holds the camera display as three saved choices: textures, faces hidden and edges. - It gives TrenchBroom's face modes (Show Textures, Hide Textures, Hide Faces) and NetRadiant Custom's wrapping four-mode cycle (Wireframe, Flat Shaded, Textured, Textured with Edges) with translated names and announcements. - `src/app/level_camera_render_actions.cpp` registers these View > Camera Display commands: - map.previewTextures: the old unregistered toggle, now one command with the status-strip and View tab boxes, synced both ways. - map.previewEdges: new, with a View tab box. - map.showCameraTextures, map.hideCameraTextures and map.hideCameraFaces. - map.nextCameraMode and map.previousCameraMode. - map.previewWireframe now toggles the saved hidden-faces choice, and the camera re-applies the saved display after every preview rebuild. Wireframe outlasts edits, undo and Reload, and turning it off restores textured or flat faces with their edges. - The choices are kept in the shell layout state. - The NetRadiant Custom profile binds Shift+] and Shift+[; the NetRadiant baseline and TrenchBroom leave them unbound, as upstream does. - `ModelViewport::setPolygonEdges` (level camera only) marks fan diagonals per triangle. The surface shader's edge overlay and the wireframe stroke list skip them, so brush faces and patch quads are outlined rather than triangulated. **Tests** - level-camera-render-smoke and level-camera-render-ui-smoke pass. The UI test covers NRC and TB at 100% and 200% with high contrast, RTL and expanded text, persistence across shells, and GPU pixel checks of the outline and missing diagonal. - Related suites pass: studio-semantics, editor-profiles, render-device, model-viewport, model-editor-ui, level-materials-ui, level-sidebar-ui (passes alone), level-camera-focus-ui, level-view-filters-ui and doom-preview-ui. - shell-interaction-smoke fails only its three known baseline checkPaletteRecents checks. **Limits left** - Doom 3's lighting mode is not in the cycle. - Placed models are still outlined triangle by triangle. - The edge colour follows the theme, with no separate preference. - TrenchBroom's Shade Faces and Use Fog toggles are not built. - Level dialog previews still draw triangle edges. - Physical-keyboard and macOS/Linux acceptance remain open.

Remaining: 1. Doom 3's lighting camera mode is not part of the cycle, because the camera has no lighting renderer. 2. Placed models and other triangle-by-triangle surfaces still show their triangle edges under Edges in Camera and in wireframe. 3. Edges in Camera uses the theme's edge colour, with no separate colour preference, and is off by default (TrenchBroom defaults it on). 4. Hidden faces always show the edges; TrenchBroom's hidden-faces-with-edges-off state (an empty camera) is not offered. 5. TrenchBroom's Shade Faces and Use Fog view toggles are not implemented. 6. Level dialog previews (brush editor, primitive, prefab and similar) still draw triangle edges. 7. Physical-keyboard acceptance of Shift+] and Shift+[ on non-US layouts, and macOS/Linux acceptance, remain open.

### Package `nrc-prism-primitives`

nrc-prism-primitives (lane1, wave 3) shipped NetRadiant Custom's Brush > Prism…, Cone… and Sphere… and its Brush3Sided to Brush9Sided commands.

Core:
- `LevelBrushPrimitiveFit::Circle` (core/level_primitive.*) builds cylinders, cones and spheres as NRC's Brush_ConstructPrism, Brush_ConstructCone and Brush_ConstructSphere do. The corners are computed so that the side planes touch a circle on the larger cross-section extent, with a face square to the first axis. A cone's point sits over the centre of the high end. A sphere is a true sphere on the largest extent, with NRC's longitude/latitude grid and its poles on the depth axis.
- `levelBrushPrimitiveMaxSides` raises the limits to the component editor's 128 faces: 126 for a cylinder and 127 for a cone.
- `levelBrushPrimitivePoints` exposes the corners. `addLevelMapBrushPrimitive` checks a circle-fit brush against the solid it drafted, because it may overhang the box.
- `rebuildLevelMapBrushHulls` (core/level_map.cpp) rebuilds brushes in place through the existing brush replacement. Each brush keeps its id, owner entity, file position, scene membership, comments and `// brush N` marker, face style and most used content flags. The selection is unchanged and the change is one replace-objects undo step.
- `rebuildLevelMapBrushesAsShape` (core/level_shapes.*) applies that to box, wedge, cylinder, cone and sphere requests in each brush's own bounds. It runs inside guardLevelSceneEdit, so locks and the open group stop it, and linked copies follow. It skips hidden, open and too-small brushes and reports why.

GUI (app/level_prism_actions.cpp):
- Make Prism…, Make Cone… and Make Sphere… ask for a side count, remembered under the shell layout state `levelPrismSides`. Spheres use the count for sides and bands, up to 11.
- Make 3- to 9-Sided Prism run with no prompt.
- All of them use the active plan view's depth axis (Z for spheres), the circle fit and the current material, and are enabled only when the selection reaches brushes in a Quake-family map. They appear in the Edit > Brush Shapes submenu, the Geometry menu, a new Brush Shapes section of the Tools tab and the palette (`map.make-prism`, `map.make-cone`, `map.make-sphere`, `map.make3-sided-prism` to `map.make9-sided-prism`).
- The NRC profile binds Ctrl+3 to Ctrl+9. They are scoped, claiming keys on the map views, and `handleLevelPrismKey` accepts them only while a brush is selected, so the window-wide Ctrl+digit mode keys keep working everywhere else.
- The Shapes tab gains a saved Fit choice (Fill the Box / Round on the Larger Side). It and the Add Brush dialog now allow up to 126 sides for a cylinder and 127 for a cone.

CLI: `map add-shape` gains `--fit box|circle` and `--in-place`, with JSON fields inPlace, fit, brushes and skipped. `map add-brush` gains `--fit`.

Continuation verification: level-prism-smoke, level-prism-ui-smoke, level-primitive-ui-smoke passed. The three main brush UI suites exercise NetRadiant Custom, TrenchBroom and VibeStudio profiles at 100%, plus 200% with high contrast, expanded translations and RTL. All 26 targeted brush, grid, scene, linked-group and map suites passed after the focused detail-classifier retry. validate_source_layout.py and validate_docs.py passed. The unrelated shell-interaction baseline was not rerun; historical results are retained separately.

Limits left:
- Side counts stop at the component editor's 128 faces: 126 for a prism and 127 for a cone, against NRC's 1022, and 11 × 11 for a sphere from the prompt, against NRC's 31.
- Every selected brush is rebuilt, not only the last-selected one.
- Surface flags and values reset.
- The Ctrl+digit keys act only in a focused map view with a brush selected.
- Native physical input and macOS/Linux acceptance remain open.

The continuation review also made held prism shortcuts perform only one rebuild, limited Add Brush sides to the chosen primitive, and clarified a missing output-brush programming error. Regression checks cover repeat key events and per-shape side limits.

The initial map-tools failure came from guessing a Quake II game from a material folder. Lane3 changed detail conversion to use headers, classes and flags. Final incremental rebuild and six focused retries passed, superseding the initial failure.

Remaining: - Side counts are capped by the component editor's 128-face, 256-vertex bound: 126 for a prism and 127 for a cone, against NetRadiant Custom's 1022. Make Sphere… stops at 11 × 11 faces (128 faces as sides × bands on the Shapes tab and CLI), against NRC's 31 × 31.
- All selected brushes are rebuilt, where NRC rebuilds only the last-selected one.
- Rebuilt faces keep the brush's most used content flags, but their surface flags and values reset.
- In the NetRadiant Custom profile, Ctrl+3 to Ctrl+9 rebuild brushes only while a 2D view or the camera has focus and a brush is selected. In other panels, or with nothing selected, they keep switching work modes.
- Native physical keyboard input and macOS/Linux acceptance remain open.

### Package `brush-from-points`

Create Brush from Points and Copy Vertex Coordinates bring TrenchBroom's debug Create Brush and Print Vertices (MapWindow::debugCreateBrush and debugPrintVertices at 4c756be1) into VibeStudio as ordinary commands. The shared service is src/core/level_brush_points.{h,cpp}. parseLevelBrushPoints reads x,y,z groups, x y z points and TrenchBroom's (x y z) form, one form per line, ignores // comments, and accepts up to 4,096 points. levelBrushPointsHull refuses points that all lie in one place, on one line or on one plane before calling createLevelBrushEnvelope. addLevelMapBrushFromPoints passes the envelope's corners to addLevelMapBrushHulls, so the brush goes to worldspawn and the active creation node in the map's face dialect, in the given material, selected, as one guarded undo step. Locked destinations refuse it and linked groups propagate it. levelMapVertexListings and levelVertexListingText list whole brushes, faces, edges, picked vertices and patch control points as `// brush:3 face:2 base/metal` lines followed by (x y z) points in the shortest exact decimal, which parse back bit for bit. The shell (app/level_brush_points_actions.cpp, app/level_brush_points_dialog.cpp) registers map.createBrushFromPoints and map.copyVertexCoordinates. They appear in the palette, Edit > Brush Shapes, the Levels Create and Geometry menus and the Tools tab, with no default keys in any profile. The dialog checks and previews the brush on a worker after a pause in typing. It offers Add Selected Corners, starts at the current material, remembers the last points under the shell layout state levelBrushPoints, and publishes only the checked candidate through levelPlacementCommitter. Copy Vertex Coordinates uses component and independent face picks when there are any, and otherwise the selected brushes and brush-entity brushes, minus hidden or filtered ones; it writes to the clipboard and Activity. `map add-brush --points` (repeatable, mutually exclusive with the shape options, checked before the map loads) and multi-brush `map brush-components` (repeated or comma-separated --brush, face positions, full-precision plain text, `brush` kept for the first brush) share the services. Continuation verification: level-brush-points-smoke, level-brush-points-ui-smoke passed. The three main brush UI suites exercise NetRadiant Custom, TrenchBroom and VibeStudio profiles at 100%, plus 200% with high contrast, expanded translations and RTL. All 26 targeted brush, grid, scene, linked-group and map suites passed after the focused detail-classifier retry. validate_source_layout.py and validate_docs.py passed. The unrelated shell-interaction baseline was not rerun; historical results are retained separately.

Limits that remain: this dialog accepts typed or selected corners; the companion Assemble Brush Tool collects clicked points; the brush always goes to the current creation context rather than into a selected brush entity; a face dialect that cannot hold a corner within 0.02 units refuses the brush; the Edit Brush Components dialog still shows ten significant digits; coordinates go to the clipboard and Activity rather than a console; native input, screen readers and macOS/Linux acceptance are open.

The continuation review deferred collecting selection corners until Add Selected Corners is pressed, installed placement cancellation around hull generation, and made counted status text translatable without ambiguous plurals. Tests cover lazy collection and cancellation without map/history mutation.

The initial map-tools failure came from guessing a Quake II game from a material folder. Lane3 changed detail conversion to use headers, classes and flags. Final incremental rebuild and six focused retries passed, superseding the initial failure.

Remaining: This dialog accepts typed points or selection corners; the companion Assemble Brush Tool supplies clicked points in plan and camera views. A brush from points always goes to the current creation context (worldspawn and the open group or active layer), never into a selected brush entity. A face dialect that cannot store a corner within 0.02 units refuses the brush. The Edit Brush Components dialog still shows ten significant digits. Copy Vertex Coordinates writes to the clipboard and Activity, not a console log, and lists at most 4,096 brushes or components at once. Native physical input, screen readers and macOS/Linux acceptance remain open.

### Package `assemble-brush-tool`

TrenchBroom's Brush Tool and its Return action (Create brush, AssembleBrushTool and AssembleBrushToolController3D at 4c756be1) are now the checkable Assemble Brush Tool (map.assembleBrushTool, B in the TrenchBroom profile) and Create Assembled Brush (map.createAssembledBrush). Both are in Edit > Brush Shapes, the Create menu above the views, the Tools tab's Brush Shapes section and the palette. The geometry is in core/level_brush_assemble.h: the outline classifier, face grid points (TB Grid::snap on a plane), grid rectangles (DrawFaceDragDelegate), ray-plane and ray-outline picks, and the snapped extrusion distance (DuplicateFaceDragDelegate). Creation reuses addLevelMapBrushFromPoints from brush-from-points.

In the camera, clicks add grid points on brush faces, or on the Draw Brush construction plane where nothing is rendered under the pointer. A drag from a face adds the grid rectangle on its plane, a double-click adds a face's corners, and Shift-drag extrudes a flat outline, which fills while it can be picked. The 2D views go beyond TB: clicks and empty drags add points or rectangles at the last point's depth or the work zone's floor, a drag on a point moves it and the points under it, and Shift-drag extrudes an outline the view shows side-on.

Every view keeps a copy of the shared points. The shell checks the hull on a cancellable worker against a copy of the map, and Enter publishes the checked copy through levelPlacementCommitter. If the map, selection, creation layer or material changed, it rechecks and then creates. The brush lands in the open group or active layer, selected, as one undo step; a locked layer refuses it with a message, and the points then clear. The tool is exclusive with the clip tool, component modes, shear, region drawing and the camera's Draw Brush, Paint and Sample, and leaves the tool bar showing no tool. A map load turns it off. map add-brush --points --extrude dx,dy,dz scripts the extruded result.

Continuation verification: level-brush-assemble-smoke, level-brush-assemble-ui-smoke, level-brush-points-smoke passed. The three main brush UI suites exercise NetRadiant Custom, TrenchBroom and VibeStudio profiles at 100%, plus 200% with high contrast, expanded translations and RTL. All 26 targeted brush, grid, scene, linked-group and map suites passed after the focused detail-classifier retry. validate_source_layout.py and validate_docs.py passed. The unrelated shell-interaction baseline was not rerun; historical results are retained separately.

Limits left:
- Points cannot be dragged in the camera, as upstream.
- The hull wire is hidden while a new check runs.
- Each check copies the whole map, which is slower on very large maps.
- Native physical input, screen readers and macOS/Linux acceptance remain open.

The continuation review fixed plan-view Shift-drag extrusion: every movement uses the press-time flat outline, rather than the changing live preview. Tests check each movement, shrinking, release, shared views and final single-step commit. The original test checked only release and missed the alternating draft.

The initial map-tools failure came from guessing a Quake II game from a material folder. Lane3 changed detail conversion to use headers, classes and flags. Final incremental rebuild and six focused retries passed, superseding the initial failure.

Remaining: - Points cannot be dragged in the camera, as upstream, and the tool has no numeric entry for a single point; typed points go through Create Brush from Points…
- While a new hull check runs, the views show only the points and any flat outline, not the previous hull.
- Every change is checked against a full copy of the map on a worker, which is slower on very large maps.
- Native physical-input, screen-reader and macOS/Linux acceptance remain open.

### Package `texture-fit-and-projection`

Proportional Fit Width/Height and axial, active-plan-view and camera projection share the surface/patch services, as one undo step, across classic, Valve 220 and brush-primitive faces and patches. Unsupported classic or edge-on projections are counted and skipped. Surfaces, Tools, menus and command search expose the operations; Surface Alignment exposes Keep proportions. The camera action uses its last direction while the pane is hidden. map align-textures provides --keep-proportions, --project, --project-direction and --project-scale, including options before the map path. The dispatcher preserves model tool --fit as a boolean. Core and offscreen UI coverage exercises mapping, one-step undo, guards and profile/accessibility variants. Final Windows /WX build and twelve selected core, CLI and offscreen UI regression suites passed, including the package-specific suites and shared surface, patch, semantic and editor-profile checks. Documentation, source-layout and CLI-documentation validators passed.

Remaining: Partial parity: fixed one-repeat quick fits; projections have no Surface Alignment preview; exact classic projection limitations are reported. Native physical input and macOS/Linux acceptance remain open.

### Package `surface-inspector-toggle-and-flags`

Show Surfaces toggles the Surfaces tab and preserves the previous tab, including when another studio mode is active; NRC and NetRadiant retain S. The Flags section, Clear Face Flags and map face-flags inspect and edit Q2/Q3 content flags, surface flags and value via a shared one-step surface plan. Mixed values, named bits and set/clear masks preserve unrelated bits. Quake II maps without trailing face flags require explicit game-header or Quake II-only classname evidence. The generic classic-map engineFamily and soft texture-folder heuristic do not choose destructive flag/detail semantics; modern Quake maps use folders too. Changed bit 31 words serialize as signed 32-bit values for compiler compatibility. Plain Quake maps decline face flags. Recovered verifier fixes remain present and are included in the final regression run. Final Windows /WX build and twelve selected core, CLI and offscreen UI regression suites passed, including the package-specific suites and shared surface, patch, semantic and editor-profile checks. Documentation, source-layout and CLI-documentation validators passed.

Remaining: Quake has no per-face flags. Unlabelled classic maps with only texture-folder hints decline flag edits; Quake II header/classname evidence or existing flag fields is required. Native physical input and macOS/Linux acceptance remain open.

### Package `uv-editor-view`

UV Editor opens one picked brush face or selected patch over its material image. Dragging a face moves its texture; an anchor permits rotation. Patch points support multiple selection and individual UV edits. Zoom, pan, snap, keyboard nudges, numeric points, local undo/redo/reset and a live embedded 3D material preview provide the authoring workflow. Apply commits one surface-service undo step; Cancel leaves the map unchanged. map edit-uv queries or applies matching offset, rotation, anchor and patch-point edits. The reusable core supports classic, Valve 220, brushDef/brushDef3 and patches, checks bounds and current target geometry, and produces shared surface edit plans. The dialog resolves material data asynchronously, exposes loading/cancellation and keyboard-accessible controls, and follows scale, high-contrast, translation and RTL settings. Hidden, locked and closed-group targets are refused. Apply checks map revision, package source/staging/reload identity and palette; the plan refuses changed face winding or patch definition. Review fixed multi-row point selection, malformed CLI point lists that could otherwise address every point, and stale neighbouring brush-plane changes. Documentation, accessibility/setup notes and upstream licence/credit references are present. Pending surface workers also block opening or applying the editor. Final Windows /WX build and twelve selected core, CLI and offscreen UI regression suites passed, including the package-specific suites and shared surface, patch, semantic and editor-profile checks. Documentation, source-layout and CLI-documentation validators passed.

Remaining: The live camera preview is embedded in the dialog; the main camera updates on Apply. Classic maps approximate turns their format cannot express, or explicitly convert the face to Valve 220. Brush primitives and patches require an image size, with explicit override when no package image is available. Shader effects are not reproduced. No default profile shortcut, matching the source-only upstream plugin. Native physical input and macOS/Linux acceptance remain open.

### Package `data-driven-filter-tags`

Data-defined tags and per-class filters share matching and visibility services across plan views, the camera and CLI. Hint and Skip are separate; Fog reads shader declarations, flags or recognised names. Tag files accept TrenchBroom configuration syntax, including comments, bare names, trailing commas and escaped wildcard strings; fixtures check parse errors and matching. Non-Latin names retain their display text with stable digest command IDs. Mixed hidden face tags combine to hide whole brushes. Class filters reject comma-containing names, include entities created later, and stay session-only. Accessible tag counts and Invert Filters status handle plurals. map filters accepts tags, project and class queries, and map visibility accepts class filters. All 12 related core/CLI/offscreen UI suites and source-layout, documentation, credits and 324-command CLI documentation validators pass. No native input or cross-platform acceptance is claimed.

Remaining: No stock tag files or transparent rendering attributes ship. Surfaceparm tags require loaded Quake III shaders. Individually hidden faces retain plan outlines. Class filters are session-only, are not inverted, and exclude comma-containing classnames. Dynamic commands are not enumerated in static ui semantics. Native input, screen readers and macOS/Linux remain unverified.

### Package `smart-tag-commands`

Loaded game/project tags generate Make &lt;tag&gt; and Make non-&lt;tag&gt; commands in the palette, Edit Smart Tags menu and bindable shortcut registry. Texture/shader tags use matching material choices, with surfaceparm choices verified against loaded shaders; classname tags tie primitives and apply an optional configured texture. Flag tags use explicit format evidence, set or clear only their mask; detail requires whole brushes. Classname disable returns matching owners to worldspawn. The asynchronous placement worker prepares candidates with stale-result rejection and cancellation; existing surface, entity and linked-group services fold into one undo transaction. map smart-tag exposes matching choices and the same guarded edit with safe save/dry-run. Original core/CLI regressions cover individual faces and patches, flag preservation and signed bit 31, classname texture transactions, linked propagation, undo/redo, hidden/locked/context refusals, cancellation, bounded history and package-only raw texture choices. Offscreen UI coverage exercises both familiar profiles at 100% and 200%, high contrast, expanded translation and RTL. Both new suites and 10 related integration suites pass, as do source-layout, documentation, credits and 324-command CLI documentation validators.

Remaining: Texture and surfaceparm tags cannot be disabled. No stock game tags or transparent attribute rendering ship. Surfaceparm tags require loaded Quake III shaders. The GUI refuses a selected brush if any face on it is filtered, including visible face picks on such a brush. Dynamic commands have no default keys and are not enumerated by static ui semantics. Native input, screen readers and macOS/Linux acceptance remain open.

### Package `plan-grid-overlays`

Levels has saved Show > Grid Coordinates and Show > Block Grid choices, matching bindable View commands and View tab check boxes. Coordinate labels follow the drawing plane, keep clear of the status tags, support fractional grids and right-to-left layouts, and describe their range accessibly. The existing viewChanged connection refreshes that description after navigation, fit and resize.

The shared level_block_grid service reads worldspawn _blocksize, blocksize and chopsize with the compiler's one/two/three-number rules and 1024-unit default. Dashed boundaries use actual block sizes, stay inside regions (including flat and point regions), and are drawn through the same grid image in synchronous and worker paths. Enumeration remains bounded for extreme finite coordinates. Doom has no block grid. map render --block-grid exports the same boundaries and structured size/source diagnostics; the normal export stays unchanged.

The level-block-grid-smoke and level-grid-overlays-ui-smoke suites cover key parsing, CLI/SVG output, clipping, worker rendering, accessible descriptions, persistence, profile commands, scaling, contrast and expanded RTL text. The continuation added regressions for degenerate regions, extreme coordinates and navigation/resize descriptions. See the wave 3 integration record for final verification results.

Remaining: Coordinate labels are not exported; the 3D camera has no block overlay. Coordinates default to off, and labels sit along the bottom rather than NRC's top edge. VibeMap2 does not split on _blocksize. Native physical-input, screen-reader and macOS/Linux acceptance remain open.

### Wave 3 integration verification (2026-10-26)

The nine packages above were completed together in the existing Windows
clang-cl debug builds with warnings treated as errors. Across the three lanes,
44 distinct core, CLI and offscreen UI suites passed. This includes linked
groups, nested extraction, protected properties, group transfer, brush tools,
surface editing, UV editing, filter tags, smart tags and grid overlays.
The UI coverage exercises the NetRadiant Custom and TrenchBroom profiles,
100% and 200% text scaling, high contrast, expanded translations and RTL.

Integration found and fixed a format-classification regression: texture folders
alone cannot turn a Quake map into Quake II for flag or detail authoring.
The original map-tools test and new explicit-header/omitted-field Quake II
tests pass. Smart-tag fixture assertions were also corrected to follow the
existing entity service's replacement object identities; their final core/CLI
and UI runs pass.

Source layout, documentation, credits, the 324-command CLI reference, runtime
command routes, build/version smoke checks, branding, changelog and release
artifact checks pass. All 48 translation catalogues were regenerated, including
760 English plural messages, and the offline guide and 22-page HTML manual
were rebuilt. Release checks validate the Windows, macOS and Linux packaging
targets; they do not constitute native macOS/Linux application testing.

The prior session's three palette-recents failures in `shell-interaction-smoke`
were outside this pass and were not rerun. Native physical-input, screen-reader
and cross-platform acceptance remain open. These workflows retain **Partial**
status; the wider level-editor parity roadmap is still unfinished.

## 2026-10-29: Parity wave 4

### Package `object-translate-gizmo`

Object Translate tool (2026-10-29, lane A). Shipped: a Translate button on the Levels tool bar and a checkable Translate Tool command (map.translateTool, palette map.translate-tool; Edit > Manipulators, the Transform menu and the Tools tab). The NetRadiant Custom profile binds W; the camera keeps W for flying in mouse look, and the NetRadiant baseline drops the key. Choosing the command again returns to Select, as NRC's MouseTranslate does. A new core module, src/core/level_object_manipulator, holds the shared LevelObjectTool enum (Select, Translate, Rotate, Scale, Transform). It decides availability: Quake-family maps only, refused with a counted, translated reason for hidden, scene-locked or out-of-group objects, including a selected entity's own brushes and patches. It also holds the pivot (the bounds' centre unless placed) and NRC's snapping as pure functions: grid-rounded deltas, aabb_snap_translation bounds snap, Shift's main axis, and per-axis pivot snap to the bounds' minimum, centre or maximum or the grid. translateLevelMapObjects() moves the whole selection by a delta, or takes the pivot to a destination, through moveLevelMapSelection with Texture Lock, as one undo step that leaves the document untouched on refusal. The shared LevelComponentGizmo gained a Translate tool: X, Y and Z arrows hidden within about 18 degrees of the view, a 12-pixel view-plane square and the pivot point, with arrowheads, labels and a high-contrast filled-and-ringed placed pivot. It also gained an object context (bounds, bounds grid, placed flag), live Shift/Ctrl modifiers and a pivot-click signal. Each plan view and the camera own an object instance beside the component one (objectGizmo()). Plan views preview the moved selection wires and box; the camera reuses its selection-drag triangle offset. Escape, another button, focus loss and context changes cancel. The manipulator replaces the plan resize handles and the camera resize box while Translate is on. The shell (src/app/level_object_gizmo_actions.cpp) keeps m_levelObjectTool and m_levelObjectPivot. A placed pivot is forgotten on load and travels with a translate. The shell publishes the already snapped delta through LevelPlacementOperation::Move, so locks, linked groups, Repeat and undo behave as for any drag. Set Object Pivot... and Reset Object Pivot cover keyboard use. `map move` routes several --object values, --to [--pivot], --snap grid|bounds --grid N and --context through translateLevelMapObjects; a single --object with --delta keeps the older route. Tests: the new level-object-translate-smoke (core and CLI, CLI output byte-identical to the core) passed in 8.9 s. The new level-object-translate-ui-smoke passed in 336 s under load. It covers the NRC, TrenchBroom and default profiles at 100%, and at 200% with high contrast, expanded strings and RTL. It checks real mouse events, a byte-for-byte core match, exact undo, Escape, Shift, pivot drag, click and dialog, a camera Z lift, locked refusal and the W key. Related suites passed: level-component-gizmo, level-component-drag-ui, level-camera-resize-ui, studio-semantics, editor-profiles, level-duplicate-move (core and UI), level-transform, level-scene-cli, and level-palette-coverage-ui (alone with a 3x timeout after one timeout under load). validate_source_layout, validate_cli_docs, validate_docs and validate_credits pass. Limits left: Rotate, Scale and Transform object manipulators are not built yet. Doom maps have no object manipulator. Choosing Translate leaves component modes, where NRC's tool also moves picks. A placed pivot survives a tool change, where NRC resets it. The bounds snap compares plain distances. The camera preview moves triangles without previewing texture-lock UVs. The manipulator itself is pointer-only; keyboard users use nudges, Move Selection... and Set Object Pivot.... Native physical input, screen readers and macOS/Linux acceptance remain open. Status: Partial; VibeStudio remains pre-alpha.

Remaining: Rotate, Scale and Transform object manipulators (packages object-rotate-gizmo, object-scale-gizmo, transform-shear-tool). They should reuse LevelObjectTool, ApplicationShell::m_levelObjectTool/m_levelObjectPivot, setLevelObjectTool() (which currently accepts only Select and Translate), refreshLevelObjectGizmos() (set the matching LevelComponentTool on the object gizmo) and the controller's existing Rotate/Scale/Shear handles with setObjectContext(). They also need an object commit path (rotate/scale/affine placement) and a preview of the transformed selection, because the gizmo emits previewRequested for those tools. Doom maps have no object manipulator (Translate is Quake-family only). NRC's Translate tool also moves picked components; VibeStudio turns component modes off when Translate is chosen (Move Components and its camera handles cover components). A placed pivot survives a tool change (NRC resets it on SetManipulatorMode); the bounds snap compares plain distances rather than NRC's absolute-value comparison. The camera preview moves selected triangles without texture-lock UV preview; the plan preview shows the selection wires and a dashed box. The manipulator is pointer-only; keyboard users rely on nudges, Move Selection… and Set Object Pivot…. GtkRadiant and the NetRadiant baseline leave W unbound (only NRC 449778b3 was audited). Native physical input, screen readers and macOS/Linux acceptance are untested. Coordinator: refresh the i18n catalogues (new tr/Text::tr/QT_TRANSLATE_NOOP strings, several %n plurals) and regenerate docs/OFFLINE_USER_GUIDE.md after the manual edits.

### Package `object-rotate-gizmo`

Object Rotate tool (2026-10-29, lane A). Shipped: a Rotate button on the Levels tool bar and a checkable Rotate Tool command (map.rotateTool, palette map.rotate-tool; Edit > Manipulators, the Transform menu and the Tools tab), bound to R in the NetRadiant Custom and TrenchBroom profiles (the NetRadiant baseline drops it); choosing it again returns to Select. It reuses the Translate package's foundation: LevelObjectTool, the shell's m_levelObjectTool and shared m_levelObjectPivot, and the object instance of LevelComponentGizmo in every plan view and the camera, whose Rotate tool gained an object mode. Handles 0 to 2 are the X, Y and Z rings (a ring within about 2.5 degrees of face-on is left out because the outer ring does its job; edge-on rings in plan views drag along their tangent), 3 an outer ring at 1.15 times the radius that turns about the line of sight (view direction in parallel views, eye to pivot in the camera), 4 the shared pivot point (drag with bounds/grid snapping, click to place or free) and 5 the sphere inside the rings, which turns like NetRadiant Custom's RotateFree trackball (levelObjectSpherePoint and levelObjectArcball). Rings follow the manipulator rotation step while Snap is on and turn freely with it off; Shift snaps any turn, the free one included, to 15 degrees. The controller previews the turn itself as a dashed wire of levelObjectManipulatorEdges() (brush polygons, patch control nets, point-entity boxes, at most 16384 edges, else the bounds), measured once when a drag begins, so object tools need no owner preview or pending state; a spoke from the pivot to the pointer and the status line give the angle. On release the shell builds the numeric request with levelObjectRotation() (one plain turn about a world axis, otherwise the Euler X, Y, Z angles of the Rodrigues rotation, Z taking none at gimbal lock) about the drag's pivot with Texture Lock and Allow Valve 220 Conversion, and runs it as LevelPlacementOperation::Rotate, so locks, linked groups, Repeat and undo behave as for Rotate Selection; a texture-lock refusal leaves the map untouched and says why. rotateLevelMapObjects() applies the same request for map rotate --around x,y,z (or --axis) with --degrees, --pivot, --angle-step and --context, so GUI, CLI and Rotate Selection write identical maps; other map rotate forms keep their route. Hidden, locked and out-of-group selections show no manipulator. The Translate verifier's minor findings were fixed in the same code: pointer targets and markers scale with the text scale (LevelComponentGizmoFrame::handleScale), a mode entered through its own command resets the object tool to Select, every end of a plan-view manipulator drag clears the drag cursor, map move without --delta or --to names the missing option, and the translate smoke test now moves a patch and a point entity. Snap and grid changes now reach the object manipulators immediately. Tests: the new level-object-rotate-smoke (core and CLI, byte-identical to rotateLevelMapSelection and to the CLI) passed in 3 s, and the new level-object-rotate-ui-smoke passed in 64 s (223 s run directly under load) across the NetRadiant Custom, TrenchBroom and default profiles at 100%, and at 200% with high contrast, expanded strings and RTL, with real mouse events, byte-for-byte core matches, exact undo, Escape, focus loss, Shift snapping, a placed pivot, a free camera turn, Clip handing back Select, locked refusal and the R key. Related suites passed: level-object-translate (core and UI), level-component-gizmo, level-component-drag-ui, level-camera-resize-ui, level-rotation (core and UI), level-arbitrary-transform, level-sweep-ui, studio-semantics, editor-profiles and level-palette-coverage-ui. validate_source_layout, validate_cli_docs, validate_docs and validate_credits pass. Limits left: a free turn starts only inside the rings (NetRadiant Custom takes any drag off the handles), and a click there starts a turn rather than a selection; rings are full circles rather than NetRadiant Custom's viewer-facing half rings; the camera previews a dashed wire, not turned surfaces; texture-lock refusals show on release, not during the preview; the angle step is the component manipulator's rotation step setting; TrenchBroom's rotate tool page (typed angle and axis) is covered by Rotate Selection rather than a tool page; Doom maps have no object manipulator; the manipulator is pointer-only (keyboard users use Rotate Selection and the quarter turns); native physical input, screen readers and macOS/Linux acceptance remain open. Status: Partial; VibeStudio remains pre-alpha.

Remaining: Scale and Transform object manipulators (packages object-scale-gizmo and transform-shear-tool): extend setLevelObjectTool() (accepts Translate and Rotate), refreshLevelObjectGizmos() (tool mapping), LevelComponentGizmo::objectPivotHandle()/toolName()/summary()/paint for objects, the activeChanged preview-edge hook in connectLevelObjectGizmo (currently Rotate only) and the commitRequested dispatch; object tools need no pending/preview round trip (m_pending is false for objects). A free turn starts only inside the rings (NRC: any drag off the handles), and a click inside the rings starts a turn rather than a selection. Rings are full circles rather than NetRadiant Custom's viewer-facing half rings. The camera previews a dashed wire rather than turned surfaces; texture-lock refusals (classic faces without Valve 220 conversion) are reported on release, not during the preview. The ring angle step is the component manipulator's rotation step setting; TrenchBroom's rotate tool page (typed angle/axis beside the views) is covered by Rotate Selection…. Doom maps have no object manipulator; the manipulator is pointer-only (keyboard: Rotate Selection…, quarter turns). GtkRadiant and the NetRadiant baseline leave R unbound (only NRC 449778b3 and TB 4c756be1 were audited). Native physical input, screen readers and macOS/Linux acceptance are untested. Coordinator: refresh the i18n catalogues (new tr/Text::tr/QT_TRANSLATE_NOOP strings incl. %n plurals; removed tr("Translate") from MapViewport/ModelViewport) and regenerate docs/OFFLINE_USER_GUIDE.md after the manual edits.

### Package `object-scale-gizmo`

Object Scale tool and TrenchBroom box handles (2026-10-29, lane A). Shipped: a Scale button on the Levels tool bar and a checkable Scale Tool command (map.scaleTool, palette map.scale-tool; Edit > Manipulators, the Transform menu and the Tools tab), bound to T in the TrenchBroom profile and listed without a key in the NetRadiant Custom profile (MouseScale has none); choosing it again returns to Select. Rotate or Scale Tool (map.rotateOrScaleTool, palette map.rotate-or-scale-tool, NRC MouseRotateOrScale/ToggleRotateScaleModes) goes to Scale from Rotate and to Rotate otherwise, or between Rotate and Scale Components while a component mode is on; no profile binds it. The tool reuses the lane's foundation: LevelObjectTool::Scale, the shell's m_levelObjectTool and shared m_levelObjectPivot, and the object LevelComponentGizmo in every plan view and the camera, whose Scale tool gained an object mode laid out like Translate: handles 0 to 2 are X, Y and Z lines ending in squares (left out within about 18 degrees of the view), 3 a dashed view-plane square and 4 the shared pivot. New core functions in core/level_object_manipulator: levelObjectScaleFactors() (NRC ScaleAxis/ScaleFree: each moved axis is (e + d)/e with e the farther bounds side from the pivot and d the grid-snapped travel; Ctrl lands the size on the working grid; Shift gives an axis handle's factor to all axes and the square's larger change to the view's two axes; sizes never drop below the grid or 1 unit), levelObjectScale() (the numeric LevelMapScaleRequest, refusing unchanged, non-positive, >1024 or non-finite factors) and scaleLevelMapObjects() (one undo step through scaleLevelMapSelection on a candidate). The controller previews a dashed wire of levelObjectManipulatorEdges() and the shell commits LevelPlacementOperation::Scale about the drag's pivot with Texture Scale Lock, so locks, linked groups, Repeat and undo behave as for Scale Selection. TrenchBroom's Scale Tool box handles: core/box_resize gained handles 6 to 17 (edge midpoints) and 18 to 25 (corners), boxResizeHandleSides()/boxResizeHandleFor(), BoxResizeModifiers {centre, proportionalAxes} and resizeBoxAlongHandle() (faces snap to the absolute grid and clamp as before; edges and corners slide along the line from the opposite edge or corner, keep their moved axes' ratio and land where the line crosses a grid plane; Shift's proportional axes follow about the centre; Alt anchors at the centre); beginBoxResize() drags any handle in the camera-facing plane through its line. While Scale is on, the camera resize box shows the edge and corner handles (ModelViewport::setSelectionResizeBoxHandles; crowded ones hidden, drawn as diamonds and discs, unlabelled) and the plan views keep their resize handles; a box handle under the pointer outranks the manipulator. Every box-handle drag (plan and camera, any tool) now takes Shift (keep proportions: the view's other axis in a plan view, with a corner held on its diagonal; all other axes in the camera) and Alt (about the centre), applied at once when the keys change. map resize gained a tool route (src/cli/level_move_repeat.cpp runLevelObjectScale) for --drag N with --axis x|y|z or --view top|front|side, --equal, --grid, --size-grid, --pivot, and --factors with --context; other map resize forms keep their route. Rotate verifier follow-ups fixed in the same code: a new profile flag LevelEditorControls::objectRotateFreeTurn (gesture editing.objectRotateFreeTurn, a controls reference row and editor controls JSON rotateToolFreeTurn) is off in the TrenchBroom profile, so its Rotate tool has no sphere and a click or drag inside the rings selects and moves as the view does, as TrenchBroom's RotateToolController passes such input on; the map rotate and map resize CLI summaries are translatable (context LevelTransformCli); the rotate core test's texture-lock refusal check is unconditional. Tests: the new level-object-scale-smoke (core and CLI, byte-identical to scaleLevelMapSelection and to the CLI) passed in 1.7 s and the new level-object-scale-ui-smoke passed in 59 s across the NetRadiant Custom, TrenchBroom and default profiles at 100%, and at 200% with high contrast, expanded strings and RTL, with real mouse events, a byte-for-byte core match, exact undo, Shift, Ctrl, Escape, the Front square, Alt and Shift plan resizes, camera edge and corner handles with Alt, Rotate or Scale Tool for objects and components, locked refusal and the T key; box-resize-smoke gained edge, corner and modifier checks; level-object-rotate-ui-smoke now covers TrenchBroom's click inside the rings. Related suites passed: level-object-rotate and -translate (core and UI), level-camera-resize-ui and -shell, level-component-gizmo, level-component-drag-ui, level-transform (core and UI), level-arbitrary-transform, level-view-transform, level-axis-transform, level-gestures (count updated to 103) and its UI test, level-large-grid, level-surface-step-ui, editor-profiles, studio-semantics and level-palette-coverage-ui. validate_source_layout, validate_cli_docs, validate_docs and validate_credits pass. Limits left: factors stay positive (no mirroring by dragging through zero); Shift on the square follows the larger change; NRC's boundless fallback for origin-only selections is not built; plan views show the box's sides and corners only (no separate end-on edge handles), and Select keeps the face and corner handles; the NRC manipulator and TB box handles show together while Scale is on; the camera previews a dashed wire (manipulator) or stretched triangles (box) rather than re-solved brushes; Doom maps have no Scale tool; the manipulator is pointer-only (keyboard: Resize Selection…, Scale Selection…); native physical input (Alt and the menu bar), screen readers and macOS/Linux acceptance remain open. Status: Partial; VibeStudio remains pre-alpha.

Remaining: Scale factors stay above zero: dragging through zero does not mirror as NetRadiant Custom can (Flip and map resize --factors mirror). Shift on the Scale square follows the larger change; NetRadiant Custom takes the smaller. NRC's boundless fallback for selections with no extent (origin-only point entities) is not built: such an axis keeps its size. Plan views offer the box's sides and corners (TB's 2D edge handles seen end-on); Select also keeps the face and corner handles, so T in the TrenchBroom profile adds the camera's edge and corner handles and NRC's axis manipulator rather than being the only way to resize. NRC's manipulator and TB's box handles show together while Scale is on. There is no TrenchBroom scale tool page with typed factors beside the views; Scale Selection… covers it. The manipulator previews a dashed wire and the camera box handles stretch the selected triangles; texture-lock refusals show on release. Doom maps have no Scale tool (their camera face handles and plan handles remain, with the new Shift/Alt). The manipulator is pointer-only; keyboard users use Resize Selection… and Scale Selection…. Native physical input (Alt held during a drag may reach the menu bar on some platforms), screen readers and macOS/Linux acceptance are untested. Coordinator: refresh the i18n catalogues (new tr()/Text::tr/QCoreApplication::translate strings in contexts LevelComponentGizmo, ApplicationShell, MapViewport, ModelViewport, LevelObjectManipulator, LevelMoveRepeatCli, LevelTransformCli, VibeStudioLevelControls, LevelGestures, VibeStudioSemantics, with %n plurals carrying counts; the map rotate/resize CLI summaries moved from QStringLiteral to LevelTransformCli) and regenerate docs/OFFLINE_USER_GUIDE.md after the manual edits. Changelog wording for the Translate package's Doom map move change and map rotate --context is still the coordinator's.

### Package `face-inspector-panel`

TrenchBroom's Face inspector is now the Face section (surfaces.face) at the top of the Levels Surfaces tab. The new core service core/level_face_attributes reads each brush face's offset, scale and rotation in its own format through levelTextureValues() and writes them through assignLevelTextureValues(); both are now public in core/level_surface and shared with Reset Textures, whose brush-primitive reading of NetRadiant Custom's TexMatToFakeTexCoords moved there unchanged. LevelFaceAttributesEdit sets any of the five values. With keepPivot it then moves the offsets so a pivot keeps its texel, as TrenchBroom's UV rotate and scale tools do, rounding classic and Valve 220 offsets to 1/10000; it then adds a move in texels. levelFaceAttributesSummary() gives the fields' values with mixed flags. prepareLevelFaceAttributesEdit() refuses hidden, locked and out-of-group brushes and builds one LevelSurfaceEditPlan, labelled 'Set texture values', committed by commitLevelSurfaceEdit(); an edit that changes nothing adds no step. LevelUvView gained a handles mode, off in the UV Editor dialog. It draws a pivot (a world point, starting at the face's texture-space lower left, snapping to face points or whole texels), a turn knob on a ring that follows the face's first edge, and square U/V and diamond U+V stretch knobs on the face's bounds. Dragging inside the face or a point moves the texture. Each drag, arrow nudge or [ ] { } turn previews through the service and reports once through handleEdited. A turn divides the drawn angle by the outline's measured turn per degree, so mirrored and unevenly scaled textures follow the pointer. In this mode the view leaves Ctrl+Z, Ctrl+Y and Ctrl+A to the map. The section also has five LTR spin boxes, accessible and translated, with 'Mixed' as their minimum marker, plus notes for unreadable brush-primitive faces and targeted patches. ApplicationShell::refreshLevelFaceInspector() runs from refreshLevelSurfaceTools() and after preview assets load; it uses the same targets as the Flags section (picked faces first). applyLevelFaceAttributesFromUi() commits fields to every target and handles to the shown face, synchronously, up to 16,384 faces, and refuses while surface edits are pending or a geometry tool is busy. Show Face Inspector (map.showFaceInspector, palette map-show-face-inspector, View > Level Sidebars, the Surfaces tool menu and the Tools tab) opens the section, expands it and never hides it; Show Surfaces still remembers the tab it replaced. StudioCommandRegistry gained the kShortcutFocusClaims claim layer: an application event filter gives a claiming command its key while keyboard focus is inside its surfaces, and window-wide commands keep the key everywhere else, in menus and in Keyboard settings. The TrenchBroom profile binds Ctrl+3 this way, so it shows the Face section from anywhere in Levels while Ctrl+3 still opens Models from other modes; NetRadiant Custom keeps Ctrl+3 for its prisms. map face-attributes reads values (per face, plus the section's summary, with mixed values as null) and applies --offset-u/-v, --scale-u/-v, --rotation, --pivot X,Y,Z and --move U,V. It supports --texture-size or --package for brush primitives, --context, --dry-run and --overwrite. Tests: the new level-face-attributes-smoke covers every format, mixed values, undo and redo, pivot turns and stretches (the pivot keeps its texel; U distances double while V stays), moves, no-ops, refusals and the CLI written byte-for-byte as the service writes it. The new level-face-inspector-ui-smoke covers TrenchBroom at 100% and at 200% with high contrast, expanded translation and RTL, and NetRadiant Custom at 100%: registration, the Ctrl+3 focus claim in and outside Levels, fields and Mixed, hidden targets, stretch, turn and move drags, arrow and ] keys, Ctrl+Z from the view, and pivot snapping, each one exact undo step. level-prism-ui-smoke was adapted for TrenchBroom's Ctrl+3. Both new suites and twelve related suites pass in builddir-lvl-b: face flags core and UI, sidebar core and UI, surface quick UI, UV edit and UV Editor UI, texture reset, palette coverage UI, prism UI, editor profiles and studio semantics. The source-layout, docs, CLI-docs (327 commands) and docs-site checks pass. Limits left: brush-primitive faces show Radiant's reading of their matrix, which ties the U scale's sign to the turn, so a turn past 90 degrees can read back as both scales mirrored on the same texture; their values need the image size from the Levels preview. There is no shear handle, and turns and stretch knobs do not snap to face edges or vertices. Field steps are fixed rather than following the grid. Patches stay with the UV Editor. Native physical input and macOS/Linux acceptance remain open.

Remaining: Brush-primitive faces show Radiant's TexMatToFakeTexCoords reading, which ties the U scale's sign to the turn: after a turn past 90 degrees the same texture can read back with both scales mirrored. Their values also need the image size, which the section takes only from the Levels preview cache. No shear handle (TrenchBroom's UVShearTool). The turn knob does not snap to face-edge angles, and the stretch knobs snap to whole texels rather than to face vertices or texture grid lines. Field steps are fixed (offset 1, scale 0.05, rotation 15) and do not follow the grid as TrenchBroom's do. There is no texture-size readout or embedded material browser in the section. Field and handle edits run synchronously on the GUI thread, capped at 16,384 faces. Patches are not edited in the Face section; the UV Editor handles them. Native physical-input, screen-reader and macOS/Linux acceptance remain open.

### Package `camera-uv-tool`

NetRadiant Custom's UV tool (MouseUV, G; UVManipulator in radiant/selection_mtor_uv.cpp at 449778b3) is now the Levels camera's UV tool. The new shared service core/level_uv_tool fits a LevelUvToolFrame to a LevelUvLayout's points and texels by least squares (world = origin + s*alongS + t*alongT). The fit is exact on a brush face and the best plane for a patch, with texelAt(), hit() and turnDegrees(). levelUvToolGrid() places repeat lines at the image size, or 64 by 64 where a classic or Valve 220 face's size is unknown, thinned to whole multiples past 48 lines. levelUvToolEdges() gives a face's outline or a patch's rows and columns. The snapping helpers work in texels with pixel tolerances the camera converts, and with hard snapping for Ctrl. levelUvToolSnapPivot() snaps to lines and corners. levelUvToolMove() moves the whole texture so a corner or the pivot lands on a line, and Shift keeps one axis. levelUvToolPointMove() moves patch points onto lines or the pivot. levelUvToolStretch() brings the held line under the pointer and snaps a corner onto it, never through the pivot. levelUvToolSkew() snaps along edges and from line feet to corners. levelUvToolSnapTurn() snaps to Shift steps or to the nearest edge for either texture axis. previewLevelUvToolEdit() sends a face's move, turn and stretch through applyLevelFaceAttributesEdit() with keepPivot. A turn is the rotation value whose U direction turns as far as asked about the frame's normal, measured and refined because the rate differs by format and mirroring. Classic and Valve 220 scales keep 1/10000, and brush-primitive scales stay exact. A face's skew and every patch edit go through applyLevelUvEdit(), and a classic face's skew needs allowValve220. prepareLevelUvToolEdit() builds one plan through prepareLevelFaceAttributesEdit() after checking that the face still reads as the layout did, or through prepareLevelUvEdit(). Either way commitLevelSurfaceEdit() makes it one undo step. app/level_camera_uv_tool draws the repeat lines that move with the texture during a drag, a face's outline or a patch's lattice, a fixed-screen-size ring with a knob along U, and the pivot, using the new ModelViewport::projectSegmentToView(). It hit-tests the pivot, patch points, rows and columns, the ring, then lines: a line counts only when the pointer is nearer it than the neighbouring repeat's middle, never one through the pivot, both lines where they cross, and Alt for a skew. Otherwise a press inside the surface moves the texture. A press that moves less than 3 px never edits. app/level_camera_uv_tool_actions.cpp filters the camera's events, previews each move live through the new ModelViewport::setTexCoordPreview(), and commits on release. That function rewrites only the target triangles' texture coordinates (faces from the new projection, patches from a fresh tessellation) and bumps the mesh revision without resetting picks, tools or the view; setMesh, clearMesh and mirrorDisplayFrom drop it. Escape cancels, and a map that changed mid-drag is refused. A plain click on another brush face or patch picks it as a face pick, while a closed group still picks as a whole. The tool takes the UV Editor's target, keeps its surface's image size while the camera reloads materials, and is refreshed from refreshLevelFaceInspector(). It is the uv button of the Levels tool bar and leaves when another tool's mode starts. UV Tool (map.uvTool, palette map-uv-tool) appears in Edit > Manipulators, the Surfaces tool menu and the Tools tab. The NetRadiant Custom profile binds G, while the NetRadiant baseline and other profiles leave it unbound. The camera's accessible description gains the tool's gestures and current surface. map edit-uv gained --stretch U,V, --skew-u K, --skew-v K and --pivot U,V through the same service, plus --rows and --columns for patches, with stretch, skew and pivot in its JSON. Fixes from the face-inspector-panel verification: the focus-claim filter now asks a focused line edit, spin box, text edit, key-sequence edit or editable combo through a ShortcutOverride probe whether it takes the key itself, so a remapped printable key still types inside Levels. Face section fields showing Mixed ignore steps. The UV view's turn ring is capped at about a fifth of its shorter side, and its stretch knobs sit 12 px outside the face's bounds. Tests: the new level-uv-tool-smoke and level-camera-uv-tool-ui-smoke pass. The UI suite covers NRC at 100% and at 200% with high contrast, expanded translation and RTL, and TrenchBroom at 100%. Its drags match the service byte for byte and undo exactly. It also checks the live preview, Escape, the classic-face skew refusal, pivot snapping, waiting with a reason, patch point, row and skew drags, and a standalone focus-claim typing check. level-face-inspector-ui-smoke gained a Mixed-step check. Eleven related suites pass in builddir-lvl-b: face attributes, UV edit and UV Editor UI, prism UI, camera texture UI, surface quick UI, translate and rotate UI, model viewport, editor profiles and studio semantics. The source-layout, docs, CLI-docs (328 commands) and docs-site checks pass. Limits left: there are no grid-density handles and no separate pivot-line drags as in NRC. A patch's lines and lattice are drawn on its least-squares plane, so curved patches show an approximation. A face turn on a sloped classic face follows the measured rate, so edge alignment there is approximate. The ring is a fixed screen size. Brush-primitive faces and patches need the camera to have loaded the material's image. Keyboard work goes through the Face section and the UV Editor, since the tool has no keyboard nudges of its own. Native physical input and macOS/Linux acceptance remain open.

Remaining: No grid-density handles (NRC's gridU/gridV points that subdivide the repeat lines 1-16 times) and no separate pivot U/V line drags; Shift on a pivot drag does not constrain it to one axis. A patch's repeat lines and control lattice are drawn on its least-squares plane, as NRC draws them on an average plane; on strongly curved patches the drawn lattice and the visible surface differ. A face's turn sets its rotation value, refined so the texture's U direction turns as far as asked; on a sloped classic face the projection makes the V direction and edge alignment approximate. The ring has a fixed screen size rather than NRC's world-size circle. Brush-primitive faces and patches need their material's image size from the camera's loaded assets; there is no explicit-size override in the tool, unlike the UV Editor. The tool has no keyboard nudges or turns of its own; the Face section and the UV Editor are its keyboard routes. Native physical input, screen readers and macOS/Linux acceptance remain open.

### Package `camera-shading-fog-grid`

camera-shading-fog-grid (lane C, wave 4) shipped TrenchBroom's View Filter Shade faces and Use fog toggles and the grid TrenchBroom draws on brush faces while the grid is visible. core/level_camera_render.* gains LevelCameraShadingState (shading on, fog off with a start and end in whole units, 512 and 4096 at first, kept 16 apart and clamped to 0..131072, face grid off), levelCameraWithFogRange (the end moves out, or for an edited end the start comes in), levelCameraFogAmount (the linear ramp the shader repeats, capped at kLevelCameraFogStrength 0.8 so distant faces keep a fifth of their colour), levelCameraFaceGridSpacing (the grid size only while the choice and Show Grid are on), levelCameraFaceGridTriangles (brush-face triangles of a preview), the saved range text and translated announcements. ModelViewport gains setFaceShading, setDistanceFog, setFaceGrid and setFaceGridTriangles; the model-surface uniform block grows from 208 to 256 bytes (view, fogColor, gridParams), corner info bit 5 marks gridded triangles, the vertex shader uses a light factor of 1 when unshaded and passes the world position and the face's dominant axis, and the fragment shader draws an anti-aliased grid in the plane of the two other world axes (dark lines on light faces, light on dark, fading as lines come within eight pixels) and then the fog (from the eye, or in orthographic views from the front of the geometry's bounds), before the selection hatching, selected edges, edge overlay and hover outline. render_shader_data.inc was regenerated for Vulkan SPIR-V and OpenGL text. The shell registers map.previewShading, map.previewFog and map.previewFaceGrid in View > Camera Display (checkable, always enabled, no default keys in any profile, as neither TrenchBroom nor NetRadiant Custom binds them), saves them under levelPreviewShading, levelPreviewFog, levelPreviewFogRange and levelPreviewFaceGrid, mirrors them as View tab boxes with Fog start and Fog end spin boxes shown while fog is on, and announces each change. Show Grid (map.toggleGrid) is now checkable and, with the grid size, drives the camera's ground grid and face grid. Face-grid marks follow every preview rebuild and the material stroke preview, whose regrouped mesh is marked afresh and restored on cancel. Tests: level-camera-shading-smoke and level-camera-shading-ui-smoke pass (Vulkan pixel checks of shading, grid, Show Grid, grid size and fog in perspective and orthographic cameras); level-camera-render, studio-semantics, editor-profiles, render-device, model-viewport, level-grid-overlays-ui, level-view-filters-ui, level-sidebar-ui, doom-preview-ui, level-material-gestures-ui and level-surface-stroke-ui pass. Limits left: the fog colour is the camera background with a fixed strength and no colour preference; the face grid is off by default, unlike TrenchBroom, and draws on brush faces only, with no major lines or colour preference; 3D Wireframe strokes are not fogged; the camera's ground grid keeps its own adaptive spacing; level dialog previews do not take the choices; OpenGL pixel output was verified by compilation here, not by drawing, because OpenGL does not start under offscreen Windows; physical-keyboard and macOS/Linux acceptance remain open.

Remaining: Fog colour and strength are fixed (camera background, 80%); no preference for either, and the ramp is linear rather than TrenchBroom's exponential one. Grid on Camera Faces is off by default (TrenchBroom draws the face grid whenever the grid is visible) and covers brush faces only; patches, placed models and Doom surfaces stay plain; no major-line emphasis or line colour preference. 3D Wireframe strokes and the painted ground grid are not fogged; the camera's ground grid keeps its own adaptive spacing and only follows Show Grid's visibility. Level dialog previews (brush editor, primitives, prefabs, merge, surface and UV dialogs) do not take the shading, fog or face grid choices. OpenGL drawing of the new uniforms was verified only by compiling both GLSL flavours; the pixel checks ran on Vulkan because OpenGL does not start under offscreen Windows (they run on OpenGL wherever it starts, such as Linux CI). Physical-keyboard and macOS/Linux acceptance remain open.

### Package `camera-far-clip`

camera-far-clip (lane C, wave 4) shipped NetRadiant Custom's ToggleCubicClip, CubicClipZoomIn and CubicClipZoomOut. core/level_camera_render.* gains LevelCameraFarClipState (on/off, step 1 to 23, default 14) with these helpers: levelCameraFarClipDistance (2^((step+7)/2) rounded to whole units, 16 to 32768), levelCameraFarClipPlane (0 while off), levelCameraWithFarClipStep (clamps), levelCameraFarClipStepForDistance (the nearest step, the shorter on a tie), levelCameraFogWithinFarClip, levelCameraPerspectiveDepth (z = scale * depth + offset, so z / w is 1 at the near plane, 0 at the far one and negative beyond; the infinite mapping without a far plane), the settings key levelPreviewFarClip with text 'on,14' or 'off,14', and translated summaries and step announcements. ModelViewport::setFarClip feeds RasterWork::farClip. viewProjection uses the finite reversed depth in perspective and, in the orthographic orbit camera, raises the depth floor to the clip distance behind the front of the geometry, where fog also counts from (shared orthographicFront). With the clip off the matrix is exactly as before. renderGpu computes the matrix first and repeats the plane from its depth row: triangles wholly beyond it are not counted as drawn, and wireframe segments are cut at it. Picking, hover and orbit pivots read the id image, so clipped faces cannot be picked. ModelViewport::clipSegmentToFarClip cuts the painted ground grid, and the HUD shows 'Far clip N units'. The shell registers map.previewFarClip (checkable), map.shorterDrawDistance and map.longerDrawDistance in View > Camera Display, always enabled. It saves the state, applies it in applyLevelCameraShading together with the fog pulled in to the clip, and announces each change; the Fog command notes when the clip ends the fog. The View tab has a Far Clip in Camera box and, while the clip is on, a labelled Draw distance list of the 23 distances. The NetRadiant Custom profile binds Ctrl+\, Ctrl+[ and Ctrl+]; the NetRadiant profile inherits them because b4b295d7 binds the same keys. TrenchBroom has no far clip and gets no keys. editor far-clip (cli/level_camera_far_clip.*) reads or changes the same key: --clip on|off and one of --step, --distance, --shorter or --longer, with --dry-run. Verifier fixes from camera-shading-fog-grid: the shell's command add helper takes a checkable flag, so map.toggleGrid and map.previewWireframe are registered checkable rather than made checkable afterwards. render-device-smoke builds the 256-byte model-surface block with the product's default shading-on values and adds four camera frames (unshaded with face grid and far clip, perspective fog, orthographic fog, all combined); each is checked on every backend and compared OpenGL against Vulkan where both start. Tests: level-camera-far-clip-smoke, level-camera-far-clip-ui-smoke (Vulkan pixels, QTest keys) and render-device-smoke (OpenGL and Vulkan agreed on all four camera frames on this machine) pass. So do level-camera-shading (smoke and UI), level-camera-render (smoke and UI), studio-semantics, editor-profiles, model-viewport, level-sidebar-ui, level-grid-overlays-ui and a shell-interaction subset. Limits left: entity and paste placement at the camera pointer still ray-cast the document and can land on a brush beyond the clip; painted overlays (component handles, gizmos, entity links, linked-group boxes) are not clipped; xray vertex markers in the Mesh Editor are not clipped (it never sets a far clip); level dialog previews and the plan views ignore the clip; the CLI changes the saved choice for the next start, not a running studio; NRC's camera toolbar button has no counterpart beyond the View tab box; the GtkRadiant, Q3Radiant and QuArK profiles leave the commands unbound because their sources were not available to confirm the keys; physical-keyboard and macOS/Linux acceptance remain open.

Remaining: Entity creation and paste at the camera pointer ray-cast the document, not the drawn image, so they can land on a brush beyond the far clip. Painted camera overlays (component handles, object gizmos, entity links, labels and linked-group boxes) are not cut at the far clip; only faces, wireframe strokes and the ground grid are. Level dialog previews and the plan views ignore the far clip; the Mesh Editor never sets one. editor far-clip changes the saved choice for the next studio start; a running studio does not pick it up. NRC's camera toolbar button has no counterpart beyond the View tab box and the menu item; the GtkRadiant, Q3Radiant and QuArK profiles leave the three commands unbound until their sources are checked. The real camera's OpenGL pixels for the new settings are checked through render-device-smoke frames, not the shell, because OpenGL does not start under offscreen Windows; physical-keyboard and macOS/Linux acceptance remain open.

### Package `camera-floor-navigation`

camera-floor-navigation (lane C, wave 4) shipped NetRadiant Custom's UpFloor and DownFloor. core/level_camera_focus.* gains levelCameraFloorStep(document, request) with LevelCameraFloorRequest (camera position, direction, excluded selectors, point-entity definition boxes) and LevelCameraFloorStep (valid/error, moved, position, source standing-height/underside/sector-eye-height/none, object, candidates), plus direction and source ids. Quake-family candidates are visible brushes (solved bounds), patches and point entities (origin plus definition box, or a 16-unit cube) that are selected or whose box holds the camera's x and y strictly inside. Brush entities and worldspawn count through their primitives, and a selected brush entity makes its primitives count. Each offers top + 32 and bottom - 16. Up takes the nearest standing height more than 0.1 above the camera, unless the camera is under the lowest underside, which it takes first. Down takes the nearest below, else the lowest underside when lower (FloorHeightWalker and Cam_ChangeFloor at 449778b3). Doom candidates are visible sectors whose outline (buildDoomSectorOutlines, odd-even) holds the camera, plus the selected ones. They offer floor + 41, kept 4 under the ceiling but never under the floor (DOOM VIEWHEIGHT and P_CalcHeight at a77dfb96); there is no underside. app/level_camera_focus_actions.cpp registers the two commands in View > Camera, enabled while the first-person camera shows the map. They exclude what MapViewport hides, pass the loaded definitions, move only the camera height through setCameraView (heading, pitch and mouse look kept; viewChanged moves the marker and following plan views) and announce the result. The commands are scoped to the camera and plan views with claims in a 'camera-floor' layer. handleLevelCameraFloorKey, called after the nudge claims in the shell's event filter, accepts the override while a command can act and keeps the camera's own fly and drive keys. In a plan view it hands the key to another enabled command that reaches there, which is Raise or Lower Floor on a Doom map with sectors selected. Profiles: NRC and NetRadiant bind Page Up and Page Down. gtkRadiantLevelControls binds them with Ctrl+\, Ctrl+[ and Ctrl+], inherited by GtkRadiant 1.4/1.5, Q3Radiant, QeRadiant and DoomEdit. Q3Radiant (and so DoomEdit and QeRadiant) swaps the steps to Ctrl+[ longer and Ctrl+] shorter, as upstream does; QeRadiant unbinds Ctrl+\ per its manual; DarkRadiant unbinds all five pending an audit. The CLI route is map camera-focus --floor up|down [--definitions], where the --object selectors count as the selection after any focus, and the JSON reports a floor object. Far-clip follow-ups: ModelViewport::farClipPlane, and LevelPastePlacement::farClipPoint/farClipNormal set by levelCursorPlacement. With them, entity placement and paste at the camera pointer ignore brush hits past the clip and pull the 256-unit miss plane in to it. The 13 editor CLI summaries are translatable (EditorCli). Tests: level-camera-floor-smoke and level-camera-floor-ui-smoke are new and pass. level-camera-far-clip-smoke gained placement and profile-key checks and passes. level-camera-focus-smoke passes after its custom key moved from G (now NRC's UV Tool) to J. Also passing: level-camera-focus-ui, studio-semantics, editor-profiles, level-nudge-ui, level-palette-coverage-ui, level-sidebar-ui and the validators. Limits left: model entities count by their definition box, not their model; the orthographic orbit camera has no floor stepping; Doom things, slopes and 3D floors are not considered; a floor key pressed while the camera rebuilds after an edit does nothing until it is ready; painted overlays are still not cut at the far clip; DarkRadiant's own keys are unaudited; physical-keyboard and macOS/Linux acceptance remain open.

Remaining: Point entities with models count by their definition box, not the model's own bounds as NRC's scene AABB does. The VibeStudio Default profile's orthographic orbit camera has no floor stepping; the commands need the first-person camera. On Doom maps only sector floors count: things, UDMF slopes and 3D floors are not considered, and open sectors without an outline are skipped. A floor key pressed while the camera preview rebuilds after an edit does nothing until the camera is ready again (the same readiness rule as Level Camera and Focus Camera on Selection). Painted camera overlays (component handles, gizmos, entity links, linked-group boxes) are still not cut at the far clip. DarkRadiant leaves the floor and far-clip keys unbound until its own bindings are audited. Native physical-keyboard and macOS/Linux acceptance remain open.

### Package `map-file-import-export`

Import Map, Export Selection as Map and Export Map (work package map-file-import-export, lane D, 2026-10-28). New core module src/core/level_map_transfer.{h,cpp}. Import Map follows NetRadiant Custom's ImportMap, Map_ImportFile and MergeMap: readLevelMapImport() reads the other map with loadLevelMap() (cancellable, under the loader's 512 MiB limit instead of the clipboard's 4,194,304 characters, 8,192 blocks and 250,000 parentheses); previewLevelMapImport() counts entities, brushes, patches, groups, linked groups and layers, lists the worldspawn keys left out, and refuses mixed or different brush dialects, patches outside Quake III and empty maps; levelMapImportTargetRenames() gives each targetname or target-key value the open map already uses the next free trailing number (door -> door1), as NRC's UniqueNames does. importLevelMap() renames on an isolated copy and inserts through the new pasteLevelMapDocumentAt(), the second half of pasteLevelMapTextAt() split out as insertPastedLevelMap() inside the same scene-lock guard: worldspawn primitives join the open worldspawn, complete groups and linked groups keep their structure, everything lands in the creation layer or open group and is selected, and the import is one undo step named after the file. The GUI runs it as the new ImportMap placement operation behind LevelPlacementTaskDialog, after LevelMapImportDialog has read and previewed the map on a worker (Cancel, progress, accessible summary, rename option, details). A .bsp is converted first through levelMapImportConversionRequest() (vibemap3-convert, -format map, map_220 or map_bp from the open map's dialect, -o into a temporary folder) with the project's compiler context. Export Selection as Map follows NRC's ExportMap and Map_SaveSelected: levelMapSelectionText() takes LevelMapSelectionTextOptions (worldspawn wrapper, worldspawn keys, file limits of 100,000 objects and 512 MiB; default options give the clipboard text byte for byte as before), a Quake II map keeps its target note, complete groups keep their scene note, and writeLevelMapBytes() in core/level_document shares writeLevelDocument()'s backup, conflict and atomic-commit path and refuses the open map's file; the save dialog's second file type leaves the keys out. Export Map follows TrenchBroom's exportDocumentAsMap: exportLevelMapCopy() refuses the open file, copies the document, clears locks and the link ids of groups in omitted layers on the copy, deletes those layers' objects through deleteLevelMapObjects(), drops the layer nodes and nested groups, restores the remaining locks and writes the copy with writeLevelDocument(); the document, its path and saved state never change. Omit From Export is the layer's _tb_layer_omit_from_export key in LevelSceneNode::trenchBroomProperties, so the scene note (version 7) saves it and TrenchBroom storage writes TrenchBroom's own key; setLevelSceneOmitFromExport() is one scene undo step for Quake-family layers, the Scene panel gains an accessible Omit from export box and an Omitted from export marker, and editor scene list reports omitFromExport. Commands map.importMap, map.exportSelectionAsMap and map.exportMap sit in the File menu, the Levels Create menu, the Tools tab (Clipboard, and Region and Build) and the palette (map.import-map, map.export-selection-as-map, map.export-map); neither upstream binds keys, so no profile does. CLI: map import (--from, --keep-target-names, --context, --workspace-root for .bsp conversion, JSON preview), map export (--omit-layer, --include-omitted, --scene-format), map copy --standalone [--worldspawn-keys] and editor scene omit-from-export, all with --dry-run and --overwrite. Tests: the new level-map-transfer-smoke (core and CLI) and level-map-transfer-ui-smoke (NRC, TB and default at 100%, NRC and TB at 200% with high contrast, expanded translator and RTL) pass, as do studio-semantics, editor-profiles, level-paste (core and UI), level-prefab, level-document (core and UI), level-scene (core and UI), level-scene-layers, level-trenchbroom-groups, level-placement, level-placement-jobs, level-object-clipboard (core and UI) and level-palette-coverage-ui; level-paste-ui and level-palette-coverage-ui needed --timeout-multiplier 2 on the loaded machine. The source-layout, CLI-docs and docs validators pass. Limits left: imported layers are not recreated (NRC merges identical layer trees); different brush formats are refused rather than converted; only targetname and the five target keys are renamed; the .bsp route was tested only to its refusal and command plan because no VibeMap3 build is present; Omit from export has no palette command or layer context-menu toggle, and Doom maps cannot leave layers out; TrenchBroom's strip-TrenchBroom-properties export preference is not built; native physical input and macOS/Linux acceptance remain open. VibeStudio is pre-alpha and this is largely untested in real projects.

Remaining: Import Map does not recreate the imported map's layers (NRC merges them when the layer trees match); objects join the creation layer or open group. Different brush texture formats are refused instead of converted on import (NRC converts face formats). Only targetname and the target, killtarget, pathtarget, combattarget and deathtarget keys are renamed; custom name keys keep their values. The .bsp conversion needs a built or configured VibeMap3; no end-to-end conversion was run here (only refusal and the command plan are tested), and the CLI conversion prints no progress. Omit from export has no palette command or layer context-menu toggle; Doom maps cannot leave layers out of an export. TrenchBroom's strip-TrenchBroom-properties export preference is not built. Native physical input, assistive technology and macOS/Linux acceptance remain open.

### Package `map-obj-export`

Export Map as OBJ and Export Selection as OBJ (work package map-obj-export, lane D, 2026-10-29). New core module src/core/level_map_obj_export.{h,cpp}, following TrenchBroom's Wavefront OBJ export (ObjExportDialog, MapWindow::exportDocumentAsObj) and NetRadiant Custom's brushexport plugin. prepareLevelMapObjExport() reduces the document to the worldspawn entity and the brushes and patches to write: hidden objects (levelMapHiddenObjects) are left out and counted; a whole-map export leaves out the layers levelMapExportOmittedLayers() names (Omit From Export marks, --omit-layer, --include-omitted) with their objects and brush entities' brushes; a selection export keeps selected brushes and patches and the brushes and patches of selected entities. buildLevelMapObj() builds that document with the unchanged buildLevelMapPreviewMesh(), so geometry, patch tessellation and UVs are the camera's; texture sizes come from LevelMapObjMaterialSource (image path, original size, average colour) and classic or Valve 220 faces whose image is unknown use 64 x 64 texels and are reported. Each face's fan is joined back into one polygon, rewound counter-clockwise by comparing its Newell normal with the face's outward normal, and written with four-decimal positions in Y-up axes (x z -y, as both upstreams do; Keep Z-up keeps the map's), deduplicated UVs and normals per object, and optionally welded corners or triangles. Grouping is per brush and patch (entity<id>_brush<id>, entity<id>_patch<id>), per material or one mesh, as o or g records; faces are sorted by material so each usemtl appears once per group; names are unique after NetRadiant Custom's 20-character shortening. Faces whose material's last path part or path end matches the ignore list (by default caulk, nodraw, clip and the clip variants, trigger, hint, hintskip, skip, origin, the portal materials, donotenter, lightgrid) are left out. The MTL holds Kd from the average image colour and map_Kd with the package path, or that path seen from the OBJ folder under the material root (package folder, else the installation's game folder, else the package's folder). exportLevelMapObj() requires a .obj name, checks both destinations (existence without overwrite, links, folders) before writing either, then publishes the MTL and the OBJ through writeLevelMapBytes() with backups; the document never changes. Commands map.exportMapAsObj and map.exportSelectionAsObj sit in the File menu, the Levels Create menu, the Tools tab (Region and Build; Clipboard) and the palette (map.export-map-as-obj, map.export-selection-as-obj); neither upstream binds keys, so no profile does. The options window (LevelMapObjExportDialog: file and Browse, Group faces, objects/groups, weld, triangles, material library, material paths, short names, Z-up, ignored materials with Restore Tool Materials, a replaced-files note) is keyboard and screen-reader labelled and remembers its choices for the session; ApplicationShell::exportLevelMapObjFile() resolves the material images from the open package or staging snapshot with resolveLevelPreviewAssets() and exports on the transfer worker behind the cancellable progress window. CLI: map export-obj with the same options and --package/--palette, routed in src/cli/level_map_transfer.cpp, whose argument parser is now shared with map import and map export. The same change fixed the map-file-import-export review's minor findings: prepareLevelMapImportConversion() adds -game from the BSP header and the project's game (levelMapBspConversionGame(): IBSP 46 quake3 or the project's IBSP 46 game, IBSP 47 wolf, et, etut or quakelive, RBSP ja, jk2 or sof2, FBSP qfusion) and -fs_basepath/-fs_game from the project's installation, refusing Quake and Quake II BSPs before anything runs (Import Map offers *.bsp only in Quake III-family maps; map import gains --game); levelMapImportConversionProblem() names VibeMap3's exit code, as a hexadecimal NTSTATUS on Windows, when it fails without output, and its error lines otherwise; the import preview takes the file size measured on the worker instead of copying the text on the UI thread; and level-map-transfer-ui-smoke covers gtkradiant-1-6 and quark. Tests: the new level-map-obj-export-smoke (core and CLI, classic, Valve 220 and brush-primitive UV and winding checks, patches, every option, writes, conversion fixes, a fake silent VibeMap3) and level-map-obj-export-ui-smoke (NetRadiant Custom and TrenchBroom at 100%, NetRadiant Custom at 200% with high contrast, expanded translator and RTL) pass, as do level-map-transfer (core and UI, seven passes), studio-semantics, editor-profiles and level-palette-coverage-ui; the source-layout, CLI-docs and docs validators pass. Limits left: placed models, point entities and Doom maps are not exported; images inside .pk3/.pak files are referenced, not copied out; export-relative paths assume the game folder holds the images loose; the 64 x 64 fallback distorts classic UVs until the package is open; per-face smoothing groups and vertex normals across faces are not written; no Blender import was run here; the .bsp conversion is still tested with a fake VibeMap3, not a real conversion; native physical input and macOS/Linux acceptance remain open. VibeStudio is pre-alpha and this is largely untested in real projects.

Remaining: Placed models (misc_model and model keys), point entities and Doom maps are not exported to OBJ. Images inside .pk3, .pak or WAD files are named by their package path in the MTL but not copied out; export-relative paths assume the game folder holds them loose. Without the game's package open, classic and Valve 220 UVs assume 64 x 64 texels (reported per material). Smoothing groups and shared vertex normals across faces are not written; every face is flat-shaded. The options window is modal, unlike TrenchBroom's modeless export dialog, and its choices last for the session only. No Blender or other modeller import was run here; the .bsp conversion is tested with a fake VibeMap3, not a real conversion. Native physical input, assistive technology and macOS/Linux acceptance remain open.

### Package `map-info-and-map-inspector`

Map Info and the Map tab inspector (wave 4, lane D, 2026-10-29). Shipped: NetRadiant Custom's Map Info (map.cpp DoMapInfo and CountStuffWalker at 449778b3) as counts in levelMapStatistics(): group entities (worldspawn and entities that own brushes or patches), entities and group entities that reach the game (levelMapEntityReachesGame(): not func_group, _decal or func_detail*, not light*, misc_model or info_null), and a name-sorted LevelMapEntityClassCount table (Doom maps count thing types, all in the game). These appear in the Statistics list, a sortable Class/Count/In game table under it (Enter lists the class in the Outliner) and map inspect (text and JSON). A new Map Info command (map.mapInfo, M in the NetRadiant Custom profile, View > Level Sidebars, the Tools tab, palette map.map-info) opens Statistics with the keyboard in the class table. TrenchBroom's Map inspector (MapInspector, MapPropertiesEditor and ModEditor at 4c756be1) becomes three Map tab sections over the new core/level_map_inspector: Layers (a summary of objects, groups and, in words, hidden, locked, omitted from export and where new objects go; Enter or Manage Layers selects the layer in the Outliner's Layers and Groups), Map Properties (soft bounds from the game, ±4096 for Quake-format maps and ±65536 for Quake III from q3map2's MIN/MAX_WORLD_COORD, a custom box typed as three numbers or one per corner, or none, kept as worldspawn's _tb_soft_map_bounds, with the number of objects outside), and Mods (an ordered list kept as _tb_mod, chosen from the installation's other folders or typed, reordered and disabled). Each change is one named undo step that keeps the selection, goes through the scene guard and makes no step when nothing changes. Show Map (map.sidebar.map) takes TrenchBroom's Ctrl+1 in the focus-claim layer, as Show Face Inspector does with Ctrl+3. The CLI adds the Map Info fields and mapProperties to map inspect --json, and a new map properties command that reports, lists --game-root mod folders and writes soft-bounds and mods changes safely. buildLevelMapInspector()/refreshLevelMapInspector() in app/level_map_inspector_actions.cpp are where later Map tab sections, such as map-inspector-issue-fixes' issue summary, plug in. The same round settled five OBJ export points: view filters do not apply (GUI and CLI, documented, tested), the unit scale is documented, the MTL name has no spaces, folder targets and cancellation are worded for OBJ, and map export-obj takes --engine. Tests: the new level-map-info-smoke (statistics, the in-game rule, soft bounds with exact undo and redo, refusals, malformed values, a locked layer, Doom refusal, mods, available folders, layer summaries, JSON, and map inspect and map properties through the CLI, including dry run, overwrite and exit codes) and level-map-info-ui-smoke (NetRadiant Custom and TrenchBroom at 100%, and TrenchBroom at 200% with high contrast, an expanded translator and RTL: Map Info focus and sorting, class and layer activation, soft-bounds and mods edits with undo and redo, M and Ctrl+1 key presses, Ctrl+1 still opening Workspace outside Levels, and accessible names) pass, as do the extended OBJ suites and the related sidebar, scene, shell, profile, semantics and palette suites. Limits left: the soft bounds are not drawn in the views and nothing outside them is reported on Health (map-inspector-issue-fixes); VibeStudio keeps the mods list but does not load assets from it; the layer summary is read-only; a brush entity with no brushes counts as a point entity, because group status comes from primitives rather than entity definitions; Map Info is a section, not NetRadiant Custom's modal dialog. Native physical input, screen readers and macOS/Linux acceptance remain open. Status: Partial; VibeStudio remains pre-alpha.

Remaining: Soft bounds are not drawn in the 2D views or the camera, and objects outside them are not reported on Health; map-inspector-issue-fixes plans the issue (levelMapObjectsOutsideBounds() is ready for it). VibeStudio keeps and edits the enabled mods (_tb_mod) but does not add the mod folders to asset lookup; rerun-and-launch-args can feed levelMapEnabledMods() to the launch {mod} token. The Map tab's layer summary is read-only; visibility, locks, renaming and new layers stay in the Outliner's Layers and Groups section. Group entities come from primitives (worldspawn or owning brushes or patches), not from entity definitions, so an empty brush entity counts as a point entity; NetRadiant Custom's in-game rule is engine-neutral, so Quake's targeted lights and static torches are counted as dropped. Map Info is the Statistics section rather than NetRadiant Custom's modal dialog. OBJ export has no scale option (documented: one map unit to one OBJ unit). Native physical input, screen readers and macOS/Linux acceptance remain open.

### Package `patch-shape-natural-mapping`

NetRadiant Custom's Patch menu prefabs (radiant/patch.cpp Patch::ConstructPrefab, radiant/patchmanip.cpp Scene_PatchConstructPrefab, PatchCreator_getBounds and the Patch_* commands at 449778b3) are now in the Levels editor. LevelPatchCreateRequest gains mapping (Stretch, the factory default for fixtures; Natural; Cap; Radiant, which picks Cap Texture for a plane and Natural for the rest), imageSize (64 by 64, NRC's notex.png, when empty) and textureScale (0.5). createLevelPatch applies the mapping through the existing naturalLevelPatchTexture and planarLevelPatchTexture. LevelPatchRingStart (Auto, Exact) sets where a round ring starts. With Auto, NRC's fixed prefabs (the four-segment cylinder, the four-segment two-band sphere and every classic cone) start half a turn round, on the negative side of the first axis across, as NRC's pCylIndex layout does. Other densities and the straight cone start at angle 0, as NRC's Exact Cylinder, Cone and Sphere do; Exact forces that. The 9x3 cylinder, square cylinder, 9x5 sphere, end cap and bevel equal NRC's control points on every axis, and the other densities equal its Exact formulas. The new classic-cone is Radiant's Cone: on-curve rows at 1 - v^2 of the radius and middle controls at 1 - a*b, so three rows give full, full and the tip, and more rows split the same quadratic. cone stays NRC's straight Exact Cone taper. LevelShapeRequest gains patchMapping (default Radiant), patchImageSize, patchTextureScale, and columns and rows for patch-plane. levelMapPatchShapeBounds() gives the selection's bounds, widening sides under a unit to the grid. replaceLevelMapSelectionWithPatch() and replaceLevelMapSelectionWithPatchShape() work inside guardLevelSceneEdit: the patch goes into the last selected brush's or patch's entity (or the world), the selected non-hidden brushes and patches are deleted, the two steps collapse into one undo step and the patch is selected. It reports the objects it replaced, which the status messages and map add-shape's JSON use. app/level_patch_prefab_actions.cpp registers map.patchCylinder, map.patchSquareCylinder, map.patchEndCap, map.patchBevel, map.patchClassicCone and map.patchSphere. They appear in Edit > Patch Shapes, the Create menu, the Tools tab's Patches group and the palette. Each works at Radiant's density along levelShapeViewAxis(), in levelBrushMaterial(), with the box from levelPatchCreationBox() (the selection, else the work zone). The image size comes from levelPatchImageSize(): the camera's materials, the surface tools' sizes, else that material resolved from the open package. map.addPatch is NRC's Simple Patch Mesh on Shift+P in the NRC profile, excluded from the NetRadiant baseline. Its draft is a Cap-mapped plane on the box's top in the Textures material. PatchEditorCreateOptions gives the patch editor the box, axis, image sizes and whether the selection can be replaced. Its creating form offers all eight shapes, the axis, columns, rows, segments, bands, sizes, mapping and 'Replace the selected brushes and patches', and builds through levelShapePatchRequest. The Shapes tab's patch shapes read their own Segments, Bands, Columns and Rows fields (defaults 4, 2, 3 and 3). Replace Selected Brushes uses the new service for patch shapes, so patches and brush-entity brushes are taken too. refreshLevelMapSelection() now refreshes the Shapes panel, so that button follows the selection. CLI: map add-patch --shape classic-cone, --mapping radiant|natural|cap|stretch (default radiant) and --texture-size W,H, with mapping in the JSON. map add-shape patch shapes take --mapping, --texture-size and --columns/--rows, and their --object brush:N/patch:N goes through replaceLevelMapSelectionWithPatchShape, with replaced and mapping in the JSON. The same lane fixed the camera UV tool verifier's minors. The resize box is hidden while the tool is on. Shift turns use NRC's fixed 15 degrees. A missing image is told apart from one still loading. ModelViewport::setTexCoordPreview() bumps GeometryKey::texCoords rather than the mesh revision, so the render worker patches only the previewed triangles' uvs into a copy of its last upload; the buffer is still uploaded whole. Tests: the new level-patch-prefab-smoke covers the NRC prefab layouts and Exact layouts on three axes, the open-group rule, the replaced report, every mapping against the shared operations, the classic cone profile, replace/undo/redo, the brush-entity owner, locked and hidden objects, bounds, and both CLI commands byte for byte. The new level-patch-prefab-ui-smoke covers the commands and Shift+P, a byte-identical Patch Cylinder with exact undo, the locked refusal, the Shapes tab defaults, classic cone and plane grid, and Add Patch replacing the selection, across NRC 100%, NRC 200% with high contrast, expanded text and RTL, and TrenchBroom. model-viewport-smoke and level-camera-uv-tool-ui-smoke gained checks for the UV fixes. All pass in builddir-lvl-b, together with level-uv-tool, level-patch-tools, level-shapes, level-patch, patch-editor-ui, level-geometry-tools-ui, level-sidebar-ui, level-prism-ui, level-texture-projection, level-texture-reset, level-uv-edit, level-camera-texture, studio-semantics and editor-profiles. The source-layout, docs, CLI-docs (330 commands) and docs-site checks pass. Limits left: Add Patch… is the full patch editor rather than NRC's small density dialog, and has no Redisperse box (the Rows and Columns menu re-disperses). A material whose image cannot be found is mapped as 64 by 64 texels. An image the camera has not loaded is read from the package on the UI thread, one image only. Selected point entities are kept, where NRC deletes them. The exact (Xact) shapes with their density dialogs belong to exact-patch-density-options. Native physical input and macOS/Linux acceptance remain open.

Remaining: Add Patch… opens the full patch editor rather than NetRadiant Custom's small Patch density dialog, and it has no Redisperse checkbox; the editor's Rows and Columns menu re-disperses. A material whose image cannot be found anywhere is mapped as 64 by 64 texels (NRC's notex.png size); remap with Natural once it loads. An image the camera has not loaded is read from the open package on the UI thread when a patch is created (one image). Selected point entities are kept, where NRC's Scene_PatchConstructPrefab deletes the whole selection. The exact (Xact) cylinder, cone and sphere density dialogs are the next package (exact-patch-density-options); createLevelPatch already lays those densities out as NRC's Exact shapes, and LevelPatchRingStart::Exact makes the nine-column cylinder and 9x5 sphere start at angle 0 for it. Camera UV tool: dragging a repeat line past the pivot still clamps rather than mirroring as NRC's negative stretch does; the texture-coordinate preview avoids the full CPU rebuild but still re-sends the whole vertex buffer to the GPU. Native physical input, screen readers and macOS/Linux acceptance remain open. refreshLevelShapesPanel() runs twice per workbench refresh (once directly, once through refreshLevelMapSelection); it is cheap and the workbench's early-return paths need the direct call.

### Package `exact-patch-density-options`

NetRadiant Custom's Exact Cylinder…, Exact Cone… and Exact Sphere… (radiant/patchmanip.cpp Patch_XactCylinder, Patch_XactCone, Patch_XactSphere and DoNewPatchDlg at 449778b3) are in the Levels editor. LevelPatchCreateRequest::square is the dialog's Square box: once createLevelPatch has applied the mapping it runs Re-disperse Columns and then Rows through applyLevelPatchMatrixOperation, which move only control points, so the texture coordinates stay the round shape's, as Scene_PatchConstructPrefab re-disperses after Patch::ConstructPrefab has mapped. levelPatchRingStartIds(), levelPatchRingStartId() and parseLevelPatchRingStart() name Auto and Exact. LevelShapeRequest gains patchRingStart and patchSquare, and levelShapePatchRequest hands each patch shape only the settings levelShapeParameters() lists for it: columns for a plane, rows for a plane, cylinder, cone and classic cone, and square and ringStart for the round shapes; the square cylinder, bevel and end cap keep Radiant's three rows. app/level_patch_prefab_actions.cpp registers map.patchExactCylinder, map.patchExactCone and map.patchExactSphere in Edit > Patch Shapes, the Create menu, the Tools tab's Patches group and the palette. Each opens a Patch Density dialog (addLevelExactPatchFromUi) with upstream's odd columns from 7 to 31 and rows from 3, a sphere's from 5, to 31, starting at 13x3 or 13x7 and then at the last choice per shape (shell layout state levelPatchDensity), a Square check box and a note saying which bounds the patch fills and how many brushes and patches it replaces. OK builds the shape with patchRingStart Exact, so every density starts at angle 0 as upstream's Exact shapes do, in the box from levelPatchCreationBox() along levelShapeViewAxis(), in the Textures material with Radiant (Natural) mapping, through placeLevelPatchShapeFromUi(), which the six prefab commands now share. The Shapes tab shows Rows for patch cylinders and cones and a Square check box (levelShapePatchSquare, saved with the tab's settings) for the round patch shapes. Add Patch… gains Square and enables each field only for the shapes that read it; its Rows now reach cylinders and cones, which the shape route had ignored. On the command line map add-patch and map add-shape take --square and --ring-start auto|exact (add-shape only for the round patch shapes, and --rows now for patch-cylinder, patch-cone and patch-classic-cone) and report ringStart and square, add-patch also columns and rows. --mapping and --ring-start are registered value options, so an option placed before the map path no longer becomes the path. No profile keys were added, as NetRadiant Custom binds none to these commands. Tests: the new level-patch-density-smoke rebuilds upstream's Exact formulas and its re-disperse independently and checks eleven densities on all three axes, plain and squared, that Square keeps the round shape's Natural UVs and equals Re-disperse Columns then Rows, the per-shape settings, a squared sphere replacing a brush in one step with exact undo, and both CLI commands against the service, including usage errors. The new level-patch-density-ui-smoke drives the three commands and their dialog (ranges, defaults, the remembered choice, accessible names, Cancel, a locked brush), a squared 9x5 Exact cylinder byte-identical to the service with exact undo, the Shapes tab's Rows and Square, and Add Patch… building a squared seven-row cone, under NRC at 100%, NRC at 200% with high contrast, expanded text and right-to-left layout, and TrenchBroom. Both pass in builddir-lvl-b with level-patch-prefab, level-patch-prefab-ui, level-patch-tools, level-patch, level-shapes, level-prism, patch-editor-ui, studio-semantics, editor-profiles, level-sidebar-ui and level-geometry-tools-ui; the source-layout, docs, CLI-docs (330 commands) and docs-site checks pass. Limits left: a material whose image cannot be found is mapped as 64 by 64 texels, and an unloaded image is read from the package on the UI thread. The Shapes tab and Add Patch… keep the Auto ring start, so their four-segment cylinder and sphere start half a turn round as Radiant's prefabs do. Native physical input and macOS/Linux acceptance remain open.

Remaining: A material whose image cannot be found anywhere is mapped as 64 by 64 texels (NRC's notex.png size); an image the camera has not loaded is read from the open package on the UI thread when a patch is created (one image). The Shapes tab and Add Patch… have no ring-start control: they keep Auto, which equals Exact at every density except four segments (Radiant's fixed nine-column cylinder and 9x5 sphere start half a turn round). The Exact commands and --ring-start exact start every density at angle 0. Selected point entities are kept, where NRC's Scene_PatchConstructPrefab deletes the whole selection. refreshLevelShapesPanel() still runs twice per workbench refresh: moving it after refreshLevelMapSelection would let refreshLevelToolShelf read a patch shape the panel resets to Box on non-Quake III maps. Native physical input, screen readers and macOS/Linux acceptance remain open.

### Package `plan-projection-recentre-zoom`

plan-projection-recentre-zoom (lane C, wave 4) shipped NetRadiant Custom's projection, focus and zoom behaviour for the 2D views and TrenchBroom's Reset 2D Cameras. core/level_navigation.* gains levelPlanCentreFor(projection, point), resetLevelPlanNavigation(plans, source) (TB MapView2D::reset2dCameras at 4c756be1: the other plans take the source's zoom and its centre on the axes they share and keep their own remaining axis; validated and atomic) and levelPlanFocusState(projection, mins, maxs, viewSize) with kLevelPlanFocusMinimumHalfExtent and kLevelPlanFocusCameraHalfExtent (NRC XYWnd::FocusOnBounds and GetCenterBbox at 449778b3: the larger half-extent, at least 128, three times over the pane, a 128-unit half box round the camera with nothing selected, clamped to 0.0025-64). PlanViewControls::radiantFraming (gesture plan.radiantFraming, on in netRadiantCustomLevelControls and so in NetRadiant; a 'Frame Selection' controls-reference row) selects the Radiant framing. app/level_plan_framing_actions.cpp holds the shell side: recentreLevelPlanForProjection (called from the projection list's handler in one-plan layouts and from showLevelMapProjection when the plane is unchanged) keeps the pane's zoom and centres on levelPlanFocusPoint (this includes the implicit switch to Top made by Draw Sector, Make Sector Mode and the Shapes sector draw), the selection's bounds or else the camera of this map load (perspective position or orbit target), falling back to the old fit with neither; centreLevelPlanViewsOnFocus recentres every visible plan in three-plan layouts for Radiant framing; frameLevelMapSelectionFromUi (map.frameSelection, the Zoom to Selection button and the map menu item; automatic framing after Find, placement or Scene list picks is unchanged) runs focusLevelPlanViewsRadiant, which frames the visible plans and leaves the camera, with linked zoom taking the smallest scale; zoomLevelPlanViewsActualSize (map.zoomActualSize, Zoom 100%) and resetLevelPlanViews (map.resetPlanViews, Reset 2D Views, from m_levelMapViewport, enabled by levelPlanViewsResettable: three-plan layout, two or more shown panes, centres and zoom not both linked). Both commands are in View, the Tools tab's Navigate group, the Select tool menu and command search (map.zoom-actual-size, map.reset-plan-views); Reset 2D Views is also in the Layout button menu, and link changes refresh its availability. NRC lists Zoom 100% with an empty key slot. TrenchBroom's Ctrl+Shift+U for Reset 2D Cameras is VibeStudio's Read Aloud, which must work in every view, so the TrenchBroom profile leaves Reset 2D Views unbound as a documented adaptation; a key the user gives it is scoped by installShortcutScopes to the plan views in the focus claim layer, so a shared key such as Ctrl+Shift+U keeps Read Aloud elsewhere and while Reset cannot act. level-plan-framing-ui-smoke presses Ctrl+Shift+U in every shipped profile's 2D views and camera and checks it reaches Read Aloud. NRC's Ctrl+Tab already won over the plan view's own Ctrl+Tab through MapViewport::setReservedShortcuts; the new UI test presses it. Floor-navigation follow-ups: `map camera-focus --floor` takes repeatable --selected selectors (selection only, no focus; JSON floor.selection), and UI tests now cover the shell's filter/region exclusion for floor steps and the far-clip handoff into camera-pointer paste. Tests: level-plan-framing-ui-smoke is new and passes (including Read Aloud's key in every profile and view, the opt-in and a Doom Draw Sector switch); level-navigation, level-gestures and level-camera-floor smoke tests and the floor and paste UI tests gained checks and pass; level-view-links-ui, level-views-ui, level-profiles-ui, level-plan-panes-ui, level-view-workspace-ui, level-sidebar-ui, level-palette-coverage-ui, editor-profiles, studio-semantics and a shell-interaction subset pass, with the source-layout, docs and CLI-docs validators. Limits left: no animation; Reset 2D Views has no counterpart in one-plan layouts, whose single pane changes plane instead of switching between separate 2D cameras; Radiant framing is on only for the NRC and NetRadiant profiles (GtkRadiant, Q3Radiant, QeRadiant, DoomEdit and DarkRadiant await audits of their own focus commands; GtkRadiant's Ctrl+Shift+Tab stays the existing Frame Selection); NRC's split layout keeps the active pane, where VibeStudio's Top/Front/Side also move focus; NRC's component-mode selection bounds are not used; physical-keyboard and macOS/Linux acceptance remain open.

Remaining: Reset 2D Views has no default key in the TrenchBroom profile: TrenchBroom's Ctrl+Shift+U stays Read Aloud, and users opt in by remapping. No animation for Reset 2D Views or the recentring. Reset 2D Views has no counterpart in one-plan layouts: their single pane changes plane rather than switching between separate 2D cameras as TrenchBroom's cycling view does. Radiant framing (plan.radiantFraming) is on only for the NetRadiant Custom and NetRadiant profiles; GtkRadiant, Q3Radiant, QeRadiant, DoomEdit and DarkRadiant await audits of their own focus and centre commands. In NRC's split layout Top/Front/Side and Next View only recentre; VibeStudio also moves focus to the matching pane. The focus point uses the selection's object bounds, not NRC's component-mode selection bounds. A user who opts Reset 2D Views into Ctrl+Shift+U loses Read Aloud's key in a 2D view while Reset can act (Stop Reading Aloud stays in the menu and palette). Physical-keyboard (keypad, Ctrl+Tab) and macOS/Linux acceptance remain open.

### Package `transform-shear-tool`

Transform tool and camera Shear Tool (2026-10-29, lane A). Shipped: a Transform button on the Levels tool bar and a checkable Transform Tool command (map.transformTool, palette map.transform-tool; Edit > Manipulators, the Transform menu and the Tools tab), listed without a key in the NetRadiant Custom profile as MouseTransform has none, and Select or Transform Tool (map.selectOrTransformTool, palette map.select-or-transform-tool, NRC MouseDragOrTransform/ToggleDragSkewModes) bound to Q in that profile only (the NetRadiant baseline leaves it unbound): Transform from Select, Select from any other tool, Move and Shear Components in a component mode, with the tool bar check state kept in step. The tool reuses LevelObjectTool::Transform, the shell's m_levelObjectTool and shared m_levelObjectPivot and the object LevelComponentGizmo in every plan view and the camera, which gained LevelComponentTool::Transform (object only) with handle ids 4 (pivot), 6 (move inside), 10-21 (side lines through the middle of the box, NRC's layout, so plan views show the outline), 30-41 (corner points; plan views keep only the turns about their line of sight, an oblique parallel camera shows all) and 50+ (scale from just outside, sides as base-3 digits), named by LevelComponentGizmo::transformKindOf(). Without a placed pivot each drag works about levelObjectTransformPivot(), the side or corner opposite its handle, which pivot() reports until finish() has asked for the commit. A side line is a Shear request (grid-snapped travel over the side's distance from the pivot, refused above 16 per unit), a corner a Rotate about its world axis (plane drag, or the corner's screen tangent when edge-on; the rotation step while Snap is on, Shift 15 degrees), outside a Scale through levelObjectSideScaleFactors() (Shift keeps proportions, Ctrl lands the size on the grid), inside a Move across a plan view or level in the camera with Alt lifting it (NRC TranslateFreeXY_Z) and Translate's Shift/Ctrl. The dashed wire previews skews, turns and scales; moves show the selection. Commits run shearLevelObjectsFromView() (core shearLevelMapObjects(): one axis through shearLevelMapSelectionAbout so Repeat replays it, a slide within the side's plane through transformLevelMapSelectionAffine; retried without Texture Lock where the face format cannot slant textures, as the plan Shear Tool does), rotateLevelObjectsFromGizmo(), scaleLevelObjectsFromGizmo() and translateLevelObjectsFromGizmo(), each one undo step with scene locks, hidden objects and the open group refused by levelObjectManipulatorTarget(). The Shear Tool (map.shearTool, TB G in the TrenchBroom profile) now also gives the camera TrenchBroom's shear handles: ModelViewport::setSelectionShearMode() keeps the resize box's six labelled faces; a side face slides along the other horizontal axis, or along Z with Alt (the slide so far kept and continued when Alt changes), a top or bottom face within its plane, relative to the move grid, with a dashed box and selection wire; release emits selectionShearRequested(), applied through the same side shear after a stale-source check. The Shear Tool no longer switches the layout to a 2D view, and it and the object tools exclude each other. CLI: map shear --side x+|x-|y+|y-|z+|z- --offset dx,dy,dz with --pivot and --context (cli/level_move_repeat.cpp runLevelObjectShear) writes exactly what the core writes; the global --about flag now fires only when no number follows it, so map shear --about N shears again. Scale verifier minors fixed: Rotate Tool texts no longer promise a free turn in every profile, a scale of a selection with no size explains itself, the camera hover defers to a winning box handle, the CLI manual covers --context with --size/--mins, and the camera manipulator commit path is now exercised. Tests: level-object-transform-smoke (pivots, side scale factors, side shear slants and matrices, refusals, one-axis shear byte-identical to shearLevelMapSelectionAbout and in-plane shear to transformLevelMapSelectionAffine with a brush, patch and point entity, texture lock as Shear's, hidden/locked/out-of-group/Doom refusals untouched, CLI --side/--offset/--pivot equal to the core, map shear --about 0 and -16 shearing, the global --about still printing About, refusals exit 4 and usage errors exit 2 writing nothing) and level-object-transform-ui-smoke (NRC/TrenchBroom/default profiles at 100% and at 200% with high contrast, expanded translator and RTL: commands, menu, Tools tiles, palette, accessible tool bar button, Q/G/no keys; Top handles; a real mouse skew previewing a 48-edge wire then equal to the core side shear in one undo step with exact undo; a 15-degree corner turn about the opposite corner, a scale from outside the right side and a move from inside, each equal to its core service; Escape; a placed pivot changing the skew's anchor; the camera's 12 lines and 12 corners and an Alt-lifted camera move committed equal to the core; the Shear Tool's camera faces: a top slide within its plane equal to the core in one undo step and a side slid sideways then up with Alt; one tool at a time; Select or Transform in both modes; locked selections; Q and G from a plan view). Verifier follow-up: pointer targets are capped at 15% of the box's smaller side on screen with a central move zone (20%) that always moves and lines and corners hidden below 16 px, so the move inside stays reachable at 200% text and on small boxes; map shear owns --about (bad values are usage errors) while every other command keeps the global --about in all forms; on Doom maps the Shear Tool brings a 2D view back and promises no camera handles; the UI test uses fresh settings per combination and covers GtkRadiant 1.6, QuArK and Hammer, passing 3 of 3 whole runs. Limits: plan views leave out NRC's edge-on side-midpoint turns; the scale from outside reaches a band round the box (no Alt plane pick); component modes keep the component tools; previews are wires; Escape in the camera does not leave the Shear Tool; on a box under 16 px on screen only move, scale and the pivot remain; Doom has no Transform or camera shear; pointer only; native input, screen readers and macOS/Linux are untested.

Remaining: Plan views leave out NetRadiant Custom's turns about in-plane axes from the sides' midpoints; only the corners' turns about the line of sight are offered there. The Transform tool's scale from outside reaches a 32-pixel band round the box (NRC: anywhere outside), and the camera has no Alt pick of a single plane. In component modes the Transform tool is not a combined component manipulator; Select or Transform switches Move and Shear Components instead. Skews, turns, scales and the camera Shear Tool preview dashed wires rather than transformed surfaces; texture-lock refusals show on release. Escape in the camera cancels a shear drag but does not turn the Shear Tool off (escape-cancel-cascade, next in this lane). On a box under 16 pixels on screen the Transform tool's side lines and corners give way (move, scale from outside and the pivot remain), so skewing or turning a tiny box needs a closer zoom. Doom maps have no Transform tool and no camera shear handles (the plan Shear Tool still works there). The manipulators are pointer-only; keyboard users use Shear…, Rotate Selection…, Scale Selection… and Move Selection…. Native physical input (Alt held during a camera drag may reach the menu bar on some platforms), screen readers and macOS/Linux acceptance are untested. Coordinator: refresh the i18n catalogues (new tr()/Text::tr/QCoreApplication::translate/QT_TRANSLATE_NOOP strings in LevelComponentGizmo, ApplicationShell, ModelViewport, LevelObjectManipulator, LevelMoveRepeatCli, LevelTransformCli (the map shear summary moved from QStringLiteral), VibeStudioSemantics; %n plurals carry counts) and regenerate docs/OFFLINE_USER_GUIDE.md after the manual edits.

## 2026-10-29: Parity wave 5

### Package `patch-cap-convert-deform`

This package adds NetRadiant Custom's Cap Selection styles, TrenchBroom's Convert Selection to Patches and NetRadiant Custom's Deform, and fixes three points the Exact-shapes verifier left. LevelPatchCapRequest::style (core/level_patch_cap.h) offers Radial, VibeStudio's planar radial cap, beside the Bevel, Endcap, Inverted Bevel, Inverted Endcap and Cylinder seams, built from each boundary's controls taken as upstream's Patch::MakeCap takes them (first row forwards, last row backwards; first column backwards and last column forwards, so column seams face as radial caps do) in Patch::ConstructSeam's layouts, with upstream's width checks (3 or 5, 5 and 9; a five-wide bevel uses the first span, as upstream does). Planar mapping is Radiant's Cap Texture at map units per tile, and Boundary mapping carries the boundary's coordinates to each derived control with the same weights as its position. capLevelMapPatches is Cap Selection: every patch, hidden ones and ones the style cannot cap skipped with reasons, caps in their sources' entities, one undo step, caps selected. The Cap Patch dialog gains Cap style, an All selected patches choice that lists only visible patches, and the current material, which the seam styles take until another is chosen; map cap-patch gains --style and repeatable --patch with the first and last rows by default; the NetRadiant Custom profile binds Shift+C (the NetRadiant baseline leaves it unbound, unaudited). core/level_patch_convert.cpp converts faces of Quake III brushes: each face polygon is fanned from its first corner into quadrilaterals and at most one triangle (an edge collapsed), each a bilinear patch of an odd grid that is exactly the face, facing away from the brush, in the face's material as a patchDef2 token, with texture coordinates from levelTextureProjection at every control (divided by the image size for classic and Valve 220 faces; 64 by 64 assumed and reported when unknown). Whole brushes are removed once every face with area converted unless keepBrushes; picked faces keep their brush; hidden and out-of-group brushes are skipped, brushDef3 faces refused and locks refuse the edit; additions and removal fold into one undo step and the patches are selected. The Levels command converts picked faces or selected brushes at 3x3 and turns on Edit Vertices, and TrenchBroom's Ctrl+P is bound in the kShortcutFocusClaims layer so Go to File keeps it outside Levels; map convert-patches adds --columns, --rows, --keep-brushes and --texture-size or --package. deformLevelMapPatches moves every control point of the chosen visible patches along one axis by a whole number of units from 0 to the maximum, drawn from levelPatchDeformOffsets (a std::mt19937 stream scaled and truncated as upstream scales rand()), so a seed repeats a run on every platform; Deform Patch… asks for Maximum (64), Axis (Z) and a Seed with New Seed and remembers the maximum and axis, and map deform-patch takes --max, --axis and --seed and reports a drawn seed. The verifier's points: map add-patch refuses --square and --ring-start for shapes other than cylinder, cone, classic-cone and sphere, as map add-shape does; Auto ring start treats only the 9x3 four-segment cylinder as Radiant's prefab, so a nine-column cylinder of more rows starts at angle 0 as NRC's Exact Cylinder does, and the manual now says exactly which densities start half a turn round (and that a cylinder's or cone's rows run straight); the Patch Density note counts only visible selected brushes and patches that exist. Tests: new level-patch-convert-deform-smoke and level-patch-convert-deform-ui-smoke (NRC 100%, NRC 200% high-contrast RTL expanded, TrenchBroom 100%), and extended patch-cap-smoke, patch-cap-ui-smoke and level-patch-density-smoke, all passing with the related patch, shapes, locks, semantics, profiles, palette-coverage, sidebar, geometry-tools and patch-editor suites and the source-layout, docs and CLI-docs validators. Limits left: the GUI converts at 3x3 only, larger faces become several patches, brushDef3 faces are not converted, seam caps are mapped at units per tile rather than by the image size, Deform's random stream differs from upstream's rand(), and native physical input and macOS/Linux acceptance remain open.

Remaining: Convert Selection to Patches in the Levels page uses TrenchBroom's 3x3 grid; other densities are command-line only. A face with more than four corners becomes several patches rather than one; brushDef3 (idTech 4) faces are not converted. Seam caps are mapped at map units per tile rather than by the material image's size at Radiant's default scale. Deform's seeded stream differs from upstream's rand(), so upstream results cannot be reproduced number for number. Native physical-input and macOS/Linux acceptance remain open for all three commands.

### Package `camera-drive-keys-and-speed`

What shipped. NetRadiant Custom connects its camera keys as main-window accelerators (camwindow.cpp CamWnd_Move_Discrete_Enable and CamWnd_Move_Enable at 449778b3), so the new PlanViewControls::cameraDriveKeys (gesture plan.cameraDriveKeys, on in netRadiantCustomLevelControls() and gtkRadiantLevelControls(), so in NRC, NetRadiant, GtkRadiant 1.4 to 1.6, Q3Radiant, QeRadiant, DoomEdit and DarkRadiant) lets a 2D view pass the camera's drive keys on. planRoutedCameraDriveKeys() and planViewKeepsKey() (core/level_editor_controls.h) leave out what the view keeps (its arrows, which still drive through planCameraStep, + - = _, Home, F, Space, Backspace, Delete, Escape, Enter, Tab and the pan-hold key), and levelPlanCameraDriveKey() (core/level_camera_keys.h) resolves an unmodified press to its motion; fly keys stay the camera's own. ApplicationShell::handleLevelCameraDriveKey (app/level_camera_drive_actions.cpp, in the plan views' event filter after the nudge and floor handlers) leaves the key to the view while it drags or its gizmo is active and to any enabled command whose key it is and that reaches the view, so a Doom map's Draw Sector keeps D and a user's key keeps its command; otherwise it claims the shortcut override and calls ModelViewport::driveCameraFromKey: a stepCameraDrive step per press and repeat with discreteDriveKeys, otherwise held flight on the camera's timer until releaseCameraDriveKey or the plan view's focus loss (releaseCameraDriveKeys). levelCameraShortcutWarnings reports a routed key a command can take unless both are the profile's own. NetRadiant Custom now defaults to discrete drive steps (m_bCamDiscrete) with CameraViewControls::drivePitchLimitDegrees 90 (Camera_PitchUp_Discrete; the camera itself stops at 89 so it keeps its heading; other profiles keep 85) and moveSpeed 500 (CamMoveSpeed); the NetRadiant profile keeps its unaudited continuous drive, 85-degree stop and map-scaled speed. CameraViewControls::moveSpeed (gesture camera.moveSpeed: auto or levelCameraMoveSpeeds(), 50, 250 to 9750 by 250, 9999) replaces the map-scaled ModelViewport::flySpeed() for held keys; mouse look's dolly keeps the map scale (automaticFlySpeed). map.cameraSpeedUp and map.cameraSpeedDown (View > Camera, Tools tab Navigate group, palette entries map.camera-speed-up/down, Shift+Num++ and Shift+Num+- in NRC only, as CameraSpeedInc and CameraSpeedDec) step through the list with levelCameraSteppedMoveSpeed() from the map-scaled speed when none is saved, save the profile's gesture override, set the camera without resetting navigation (setCameraMoveSpeed), announce the speed in the status bar and grey out at the ends of the list or without a first-person camera; a levelCameraSpeed label beside Textures above the views shows the speed with an accessible name and description. The controls reference gains 2D-view Camera drive keys and camera Camera speed rows, and levelEditorControlProblems refuses unlisted speeds and pitch stops outside (0, 90]. The CLI route is editor gestures --set camera.moveSpeed=N|auto and plan.cameraDriveKeys=on|off; nothing touches the map, its selection or undo history. Tests and results. New level-camera-drive-keys-smoke (speed list and steps, routed keys and motions per profile, that across all 24 profiles a routed key overlaps only Draw Sector's D and never a window-wide or accessibility key, that Shift+Num+/- belong to the speed commands alone, gesture fields, controls rows, warnings and editor gestures/controls) and level-camera-drive-keys-ui-smoke (NRC at 100% and at 200% with high contrast, RTL and expanded text, GtkRadiant 1.6.0 and TrenchBroom: commands, menus, accessible Tools buttons, keys, readout and saved speed, A and D steps from the top view, bound up, down and pitch keys with the 89-degree stop, held glide stopping on release and on focus loss, a user's A and a Doom map's Draw Sector keeping their keys, real QTest key presses, map unchanged) pass, as do level-gestures (core and UI), editor-profiles, studio-semantics, level-nudge (core and UI), model-viewport, level-camera-drive-ui, level-camera-floor-ui, level-camera-focus-ui, level-camera-render-ui, level-camera-texture-ui, level-profiles-ui, level-palette-coverage-ui and level-plan-framing-ui (Read Aloud keeps Ctrl+Shift+U); shell-interaction-smoke fails only its three known checkPaletteRecents checks. The source-layout, docs and CLI-docs validators pass. Limits left. NetRadiant Custom's exact 90-degree pitch, its acceleration to the held speed (m_time_toMaxSpeed) and its plan arrows' fixed 32-unit steps (plan.fixedCameraSteps stays off, so NRC's 2D arrows still step max(grid, 16)) are not reproduced; the speed list is fixed, so 50 steps up to 250 rather than NRC's 300; TrenchBroom's wheel speed adjustment during fly is not added; physical keyboards (keypad Shift chords, held keys) and macOS/Linux still need acceptance.

Remaining: NetRadiant Custom's exact 90-degree pitch: the camera clamps at 89 degrees so it keeps its heading (ModelViewport kMaxPitch). NetRadiant Custom's acceleration to the held speed (m_time_toMaxSpeed) is not reproduced. NRC's 2D arrow keys still step max(grid, 16) units: plan.fixedCameraSteps stays off for NRC (turning it on would also change Shift/Ctrl+arrow handling). The speed list is fixed (50, 250..9750, 9999), so from 50 Increase goes to 250 rather than NRC's 300. TrenchBroom's wheel adjustment of the fly speed during mouse look is not added. Physical keyboards (keypad Shift chords, held keys from a 2D view) and macOS/Linux acceptance remain open.

### Package `rerun-and-launch-args`

Run Last Build, Re-run Last Compile and launch arguments (wave 5, lane D, 2026-10-29). Shipped: NetRadiant Custom's Build_runRecentExecutedBuild (F5, at 449778b3; NetRadiant b4b295d7 binds it too) as Run Last Build (build.runLast), which re-plans what the project ran last against the open map: a build pipeline (with its launch when Build and Launch ran it) or the Levels Run Profile compile, recorded per project in StudioSettings::lastBuildRun(); F5 in the NetRadiant Custom profile, inherited by NetRadiant, where Build and Launch gives the key up. TrenchBroom's Re-run compilation (MapWindow::rerunLastCompilation at 4c756be1) as Re-run Last Compile (compiler.rerunLast, no key), which replays the open map's newest saved run in .vibestudio/compiler-runs (findNewestCompilerRunManifest(), across the project and the map's folder) through rerunCompilerCommandManifest(), saves the replay beside it and is enabled only when such a run exists; when saving unsaved edits writes a new file, that file is compiled with the same profile instead. Run Profile remembers its profile for each game (quake, quake2, quake3, doom) and the next map of that game opens with it chosen when it suits the map, as TrenchBroom remembers the last compilation profile per game. Both commands stay in Levels and join Launch Game in the Tools tab's Region and Build group. Launch and Test gains Extra arguments, kept per installation and split as a shell would, and Launch Profiles… for the user's own launch profiles (after TrenchBroom's game engine profiles): a name, an engine and an argument line with {map}, {mod}, {basedir}, {bsp}, {iwad} and {warp}, grouped from switch to switch so an empty token drops its group (silently for {mod}), with a live preview and problems named in words. Launch Game from Levels now shows its review over the map instead of switching to Build. Compiles now re-enable Run Compiler Profile and the repeat commands when they finish. The CLI adds compiler rerun --last and launch profiles, profile-set, profile-remove and extra-args, and launch plan/run use the user's profiles and the saved extra arguments unless --extra-args is given, which is now split shell-style. The same round settled the Map Info verifier's minors: TrenchBroom layer and group containers are counted as layers and groups, not entities (NetRadiant Custom counts them as func_group group entities), documented and shown on a statistics line of their own; Map Properties, Mods and the Worldspawn list change while a group is open, as in TrenchBroom (editLevelMapWorldSettings()); the Mods list follows a change of installation; Doom thing types sort by number; and the netradiant profile's M for Map Info is tested and documented. Tests: the new build-rerun-smoke (saved-run lookup and replay through a fake compiler, user profiles, groups, validation, JSON, plans, settings, profile keys and every new CLI route with dry runs and exit codes) and build-rerun-ui-smoke (NetRadiant Custom at 100%, TrenchBroom at 200% with high contrast, an expanded translator and RTL: Run Profile, Re-run Last Compile, Run Last Build by F5, extra arguments, the Launch Profiles dialog, the launch review over Levels and map settings with a group open) pass, as do the extended level-map-info-smoke and the related build, compiler, settings, profile, semantics, sidebar, palette, group, deployment and document suites. Limits left: Run Last Build re-plans rather than replaying exactly, and a region compile is not recorded as the last build; Re-run Last Compile looks at the newest 64 runs per folder for its enablement and does not offer older runs; the Build page's pipeline choice itself is not restored across sessions; user launch profiles are global rather than per game, have no executable of their own (the installation's is used) and are not shared with TrenchBroom; build pipelines' own stage manifests are not replayed by Re-run Last Compile; the map's enabled mods are not yet offered as {mod}. Native physical input, screen readers and macOS/Linux acceptance remain open. Status: Partial; VibeStudio remains pre-alpha.

Remaining: Run Last Build re-plans the recorded pipeline or profile with the current settings instead of replaying it exactly, refuses when the last build was for another game, and does not record region compiles. Re-run Last Compile offers only the newest saved run of the open map (its enablement reads the newest 64 runs per folder); build pipelines' stage manifests are not replayed, and there is no list of older runs to choose from. The Build page's pipeline choice itself is not restored across sessions (the last build record is used only by Run Last Build). User launch profiles use the installation's executable, are global rather than per game, and are not shared with TrenchBroom's engine profiles; the launch review is a confirmation, not TrenchBroom's editable Launch Engine dialog. The map's enabled mods (_tb_mod) are not yet offered as the launch {mod} or Game folder. GtkRadiant 1.6's Map Info key is not bound (its source was not at hand to confirm M). Native physical input, screen readers and macOS/Linux acceptance remain open.

### Package `project-build-pipelines`

Project build pipelines and program trust (wave 5, lane D, 2026-10-29). Program trust (core/build_program_trust.*) gates every GUI and CLI build entry point that can start a program named by project data or a saved run; trust is per user, per project and per program hash. Project pipelines (nrc:BuildMenuCustomize) are kept in the manifest's buildPipelines key, edited in Build Pipelines…, and read by build list/plan/run, with pipeline-set and pipeline-remove. Tests: build-project-pipelines-smoke and -ui-smoke (new), plus build-rerun smoke and UI (extended); the related build, deployment, palette and map-info suites pass. Limits: stages run compiler profiles only, with no free command lines, copy or export tasks; there is no UI or CLI to list or revoke trust; `compiler run` and `build run-prepared` do not check trust. Status: Partial; VibeStudio remains pre-alpha.

Remaining: Nothing of project-build-pipelines was left unbuilt; it was not reverted (see summary). Product limits: stages are compiler profiles only, with no free command lines or copy, rename or export tasks; pipelines are per project and need an open project; trusted programs cannot be listed or revoked in the UI or CLI; `compiler run` and `build run-prepared` do not check trust; shell-interaction-smoke was not rerun.

### Package `nrc-drag-tool-gestures`

NetRadiant Custom drag-tool gestures and Build tool (2026-10-29, lane A). Shipped: a core module, src/core/level_plane_drag, holds NRC's QE4 drag as data. pickLevelPlaneDrag() takes a probe (a point on the press ray, its direction and the view's line of sight) and, on the selected brushes and the brushes of selected entities (hidden ones left out, at most 256), picks the planes facing the press point and seen most nearly edge-on (DragManipulator's selectPlanes with the 0.005 front test and 0.03 tie, one plane per brush in the camera, and the planes back to back with them on other selected brushes), the best plane (the face the ray meets first, a 2D view passing faces it sees flat, else the face behind the nearest edge) with the planes lying on it either way round (Plane) or the same way round (Extrude), or the vertices stacked under the pointer within a tolerance, else that plane's corners (Vertices). moveLevelBrushPlanes() moves plane points by the translation and leaves texture parameters alone, so textures stay put in the world as NRC's resize does, and searches each axis in turn for the furthest grid step (or 1/64 of the travel) that keeps every face, after BrushInstance::transformComponents. applyLevelPlaneDrag() is one undo step through replaceLevelMapComponentGeometry(), transformLevelMapComponents() or extrudeLevelMapFaces() (split: a pull makes new brushes, a push splits a slab off), refusing unselected, hidden, locked and out-of-group brushes and non-Quake maps without touching the document; previewLevelPlaneDrag() gives the result's edges from the picked brushes. The gesture table gains PlanEmptyDrag::DragPlanes ('drag-planes', the empty drag with a selection in the NetRadiant Custom, NetRadiant and GtkRadiant profiles; a selection with nothing facing the press falls back to the box resize), plan.vertexDragModifiers and plan.extrudeDragModifiers, and camera.planeDragModifiers and camera.extrudeDragModifiers (Alt and Ctrl+Alt in NRC only; inherited values yield to conflicting older customisations such as quick clip on Alt), with control-reference rows and conflict problems. A shared controller, app/level_plane_drag_controller, picks through a LevelComponentGizmoFrame, follows the pointer with an axis drag (distance snapped by snapLevelBrushExtrusionDistance) or a grid-snapped view-plane drag, previews dashed result wires over the heavy picked planes and emits commitRequested or chainRequested; every plan view (map_viewport_plane_drag.cpp) and the camera (model_viewport_plane_drag.cpp, reading the shell document through setPlaneDragDocument()) own one, with Escape, another button and focus loss cancelling and the summary in the accessible description. In the NRC profile Alt pressed during a camera selection move now lifts it, keeping the move so far. The Build tool (tool bar Build, map.buildTool, palette map.build-tool, Edit > Manipulators, the Transform menu and the Tools tab; B in the NetRadiant Custom profile, unbound in the NetRadiant baseline and elsewhere) puts both views in Build mode: a drag on or beside a selected brush extrudes the plane under the pointer, Shift and a drag from the selection lays levelCloneChain() copies end to end along the drag's main axis (half a box length rounds up, at most 64) as one Duplicate with Offset array, and recordLevelCloneChainRepeat() leaves only the last copy selected and records the chain as duplicate steps for Repeat Last Commands, so a repeat carries it on from that copy without stacking (arrays made elsewhere stay unrecorded, as level-repeat-smoke requires); a click selects, and Build excludes the object tools and the Shear Tool and hides the camera's resize box. CLI: map drag-planes --from x,y,z --view top|front|side or --direction x,y,z --camera [--forward], --mode facing-planes|plane|vertices|extrude, --delta or --distance, --grid, --tolerance, --texture-lock, --context (cli/level_move_repeat.cpp runLevelPlaneDrag), writing exactly what the core writes. Tests: the new level-plane-drag-smoke (core and CLI) and level-plane-drag-ui-smoke (NRC, TrenchBroom and default profiles at 100% and at 200% with high contrast, expanded translator and RTL; real-mouse QE4, Alt vertex and Ctrl+Alt extrude drags in the Top view and an Alt plane drag in the camera, each equal to the core in one undo step; the Build face drag, the Shift chain's positions, its last copy selected and Repeat carrying it on without stacking; Escape; locked refusal; B) pass in builddir-lvl-a with werror, as do level-gestures (core and UI), editor-profiles, studio-semantics, level-camera-resize (UI and shell), level-component-drag-ui, level-repeat (core and UI), level-sidebar-ui, level-palette-coverage-ui, level-profiles-ui, level-object-translate-ui, level-object-transform (core and UI), level-duplicate-move-ui and model-viewport; shell-interaction-smoke fails only its three known checkPaletteRecents checks. validate_source_layout, validate_cli_docs, validate_docs, validate_credits and the docs site build pass. Limits left: the camera has no facing-planes drag over empty space (Alt picks one plane) and no brush creation from an empty drag; patches and light radii are not plane-dragged; a drag stops before a face vanishes rather than keeping a redundant plane; NRC's component-mode drags stay with the component tools; a chain follows the drag's main axis only; previews are wires; native physical input (Alt and Ctrl+Alt may reach the menu bar or window manager on some platforms), screen readers and macOS/Linux acceptance are untested. Status: Partial; VibeStudio remains pre-alpha. Verifier fix: after a Build chain only the last copy stays selected (the array selects copies copy by copy), so Repeat Last Commands continues the chain one length at a time instead of copying the whole group onto itself; level-plane-drag-smoke and level-plane-drag-ui-smoke now check the copies' positions before and after the repeat.

Remaining: Camera: no facing-planes drag over empty space beside the selection (NRC picks one plane per brush there); Alt picks the single plane under the pointer instead. Camera: no brush creation from an empty drag with nothing selected (NRC g_3DCreateBrushes); Draw Brush covers it. Patches and light radii (NRC PlaneSelectable on patches and lights) are not plane-dragged; selections of only those fall back to the box resize. A plane drag stops before any face would vanish; NRC keeps a non-contributing plane and binary-searches only a collapsing brush. NRC's component-mode drag behaviours (dragging picks from anywhere, vertex insertion on an empty press) are not merged into Select; the component modes keep their own tools. No hover highlight of the planes a press would pick before pressing (NRC's DragManipulator::highlight). Build chains follow the drag's main axis only, step by the box length (no spacing), and preview as boxes. Native physical input (Alt and Ctrl+Alt drags may reach the menu bar or window manager on some platforms), screen readers and macOS/Linux acceptance are untested.

### Wave 5 wind-down (2026-10-29)

The wave stopped early at the user's request, as the weekly allowance ran
low. Five packages above shipped. `patch-split-merge` and
`entity-labels-angles-light-radii` were started and reverted; only their
follow-up fixes for earlier packages were kept (editing keys no longer act
from Levels text fields; camera drive-key and speed fixes). The remaining
queue is unchanged from the parity package list.
