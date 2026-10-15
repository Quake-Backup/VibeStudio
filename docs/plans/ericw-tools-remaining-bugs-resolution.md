# VibeMap2 known-issue reconciliation

## Current audit: 2026-10-09

**Partial.** The runtime catalogue and Quake map preflight now use VibeMap2
3.1.0 as their source baseline. VibeStudio remains pre-alpha and largely
untested in real projects. This is a source/documentation reconciliation,
not a new native compiler or visual acceptance run.

The reviewed standalone checkout and the studio submodule both resolve to
`4495049a9e4c1f6deadae3a76b8256614840af35`. Evidence comes from VibeMap2's
[upstream audit](https://github.com/themuffinator/VibeyMapTools/blob/4495049a9e4c1f6deadae3a76b8256614840af35/docs/upstream-audit.rst),
[3.1.0 changelog](https://github.com/themuffinator/VibeyMapTools/blob/4495049a9e4c1f6deadae3a76b8256614840af35/CHANGELOG.md),
[detailed changelog](https://github.com/themuffinator/VibeyMapTools/blob/4495049a9e4c1f6deadae3a76b8256614840af35/docs/changelog.rst), and
[build guide](https://github.com/themuffinator/VibeyMapTools/blob/4495049a9e4c1f6deadae3a76b8256614840af35/BUILDING.md). The documents still use the earlier
VibeyMapTools/VMT name; the fixes predate the VibeMap2 executable rename.
Both licences are GPL-3.0; attribution is recorded in [Credits](../CREDITS.md).

All **125** IDs from the original ericw-tools catalogue are accounted for:
**15 retired**, **3 informational guidance notes**, and **107 other retained
reports**. The coverage test keeps the original full ID set and requires a
unique, disjoint retained/retired partition. Removing an entry without an
explicit disposition fails the test. Fixed cases also have negative preflight
and plan-warning regressions; unresolved cases retain positive coverage.

### Retired reports

These entries are removed from runtime lookup, keyword matching and plan
warnings. The corresponding preflight predicates are removed where present.
Issue numbers remain ericw-tools references, not VibeMap2 issue numbers.

| Issue IDs | Decision and evidence |
|---|---|
| #167, #483 | Fixed in VibeMap2 3.1.0: opt-in `-embedsettings` writes a versioned, path-sanitised effective-settings manifest to `LIGHTING_SETTINGS`. #167 is the older light-provenance request covered by the same facility. This is not a promise to embed every pipeline command or private path. |
| #173, #475 | Fixed in VibeMap2 3.1.0: authored switchable-light style is retained as `oldstyle`; the runtime switchable style can still be generated. #173's duplicate warning is retired on that evidence. Inconsistent START_OFF groups (#122) remain separate. |
| #400, #416 | Fixed in VibeMap2 3.1.0: `-bouncedebug` and `-bouncelightsdebug` render indirect light and emitters. #400 describes the same all-black debug failure. |
| #401 | Fixed in VibeMap2 3.1.0: saturation boosts preserve texture brightness. |
| #422, #444 | Fixed in VibeMap2 3.1.0: region/antiregion brush culling and sealed Quake II region areaportals are repaired. This does not establish a fix for rotating-origin leaks (#417) or multiple-region union (#390). |
| #437 | Fixed in VibeMap2 3.1.0 for the audited GoldSrc sun/spot case: native `light_environment` and `light_spot` support. Retire the overly broad catalogue warning; do not infer full GoldSrc compiler parity. |
| #451 | Fixed in VibeMap2 3.1.0: safe prefix-free loose/PAK texture lookup follows the traditional `textures/` lookup, including Quake II metadata. |
| #463 | Fixed in VibeMap2 3.1.0: Hub MAP previews use owned temporary workspaces and preserve map-specific RAD input without overwriting sibling build outputs. |
| #470 | Fixed in VibeMap2 3.1.0: original Half-Life min/max light scale restored. Remove the generic `_minlight` preflight warning; this does not make all target formats' scales identical. |
| #485 | Fixed in VibeMap2 3.1.0: `-force_world_units_per_luxel` overrides entity values when paired with `-world_units_per_luxel`. It is distinct from #483's embedded manifest. |
| #335 | Inapplicable: discovery accepts prefixed `vibemap2-*` and `vmt-*` binaries, not generic `light`, `vis` or `qbsp`. |

The shared-fix decisions for #167, #173 and #400 follow the old catalogue's
own descriptions and the documented replacement behaviour, rather than a
claim that the upstream tracker closed those reports. VibeMap2's
[style regression](https://github.com/themuffinator/VibeyMapTools/blob/4495049a9e4c1f6deadae3a76b8256614840af35/tests/test_ltface.cc) checks `oldstyle`, and its
[lighting tests](https://github.com/themuffinator/VibeyMapTools/blob/4495049a9e4c1f6deadae3a76b8256614840af35/tests/test_light.cc) check the embedded manifest.

### Retained reports and reasons

Each remaining ID occurs in exactly one row below. A retained historical
report means there is insufficient issue-specific evidence to retire that
behaviour; it does **not** assert a reproduced defect in VibeMap2 3.1.0.
General hardening or a similarly worded changelog entry is insufficient to
claim a map-specific regression fixed. Existing severities and local actions
remain unless the row explicitly says informational guidance.

| Issue IDs | Disposition and reason |
|---|---|
| #425 | Informational guidance only, with no diagnostic keywords: ordered lightgrid ray batching is present in 3.1.0, but the audit explicitly leaves VPL batching to benchmarks. It does not establish playable-volume culling or completion of every performance request. |
| #480, #482 | Informational guidance only, with no diagnostic keywords: Qt Wayland/XCB and Embree 4/oneTBB are documented platform/dependency constraints. Registry metadata keeps #480 as a launch constraint and still avoids launching the GUI during probes. |
| #456 | Retained design limitation: brush-model luxel placement needs an explicit inside/outside policy; the audit rejects a silent global behaviour change. |
| #464 | Retained informational design limitation: generic manual cluster merging can weaken occlusion. The audit recommends PRT2 detail clustering or `-fast`; manual merging still needs conservative PVS tests. |
| #484 | Retained sampling limitation: the fork documents discrete `_sunlight2` direction steps and `-sunsamples`/`-extra4` guidance, with a legacy-dome brightness trade-off, rather than declaring a fix. |
| #309, #399, #415 | Retained BSPX metadata/contract reports: an effective-settings manifest does not supply missing lightmap dimensions or establish every older LMSHIFT/lump contract. |
| #122 | Retained input consistency check: style preservation does not reconcile contradictory START_OFF flags within a targetname group. |
| #77 | Retained AO debug report: bounce-debug repairs do not establish `_dirt -1` behaviour in dirt-debug output. |
| #87, #105, #129, #135, #245, #287, #288, #293 | Retained diagnostics and input safeguards: parser hardening does not remove path privacy, engine string limits, escaping portability or the need for source locations and readable diagnostics. |
| #121, #136, #298, #443 | Retained cross-tool/engine compatibility checks: entity key case, clipnode/marksurface limits, mapversion cleanup and numbered model ordering are not resolved by the recent compiler fixes. |
| #193, #194, #199, #207, #231, #327, #333 | Retained prefab reports: no issue-specific resolution is recorded for transformed lighting, missing classnames, path bases, grouped imports, lighting keys, merge targets or entity import scope. |
| #390, #417 | Retained region reports: the documented #422/#444 repairs do not establish multiple-region union or rotating-origin region leak correctness. |
| #91, #109, #134, #140, #145, #172, #185, #190, #217, #221, #238, #255, #256, #262, #268, #291, #349, #351, #364, #375, #377, #389, #405, #439, #441 | Retained lighting/geometry reports: focused sun-energy, Phong-backface, saturation and range fixes do not establish these distinct AO, beam, spotlight, seam, liquid, penumbra, switchable-shadow, relight, minlight or visblocker cases. #190's broad visual regression is not proved fixed solely by blur-radius parsing changes. |
| #203, #214, #247 | Retained lighting controls: independent sun-bounce suppression, high-gate behaviour and model shadow authoring are not explicitly resolved by the audited changes. |
| #192, #201, #346, #450 | Retained asset/workspace reports: prefix-free texture lookup does not settle custom Quake palette handling, output/WAD directory policy, every image format or intentional non-WAD warnings. The older `-notex` fix does not prove #450's later non-WAD case resolved. |
| #435 | Retained unverified helper-operation risk: generic utility parser hardening does not explicitly resolve the reported argument shape. Registry wording now states the evidence limit; operation-specific smoke coverage is still needed. |
| #215, #223, #225, #289, #449 | Retained informational integration notes: distribution routes, package-name tolerance, durable helper logs, missing inspection fields and stable log parsing remain wrapper concerns. These are not claims of compiler algorithm defects; fmt 12 compatibility is already present. |
| #103, #131, #205 | Retained VIS/approximation reports: portal validation and faster VIS scheduling do not establish hint correctness, approximation through occluders or one-way VIS support. |
| #249, #374, #436 | Retained lightmap compatibility/feature requests: legacy scale contracts, baked diffuse output and per-face lightmap disabling are distinct from forced luxel scale. |
| #301, #343 | Retained authoring requests: no audited implementation of custom hull sizes or conditional entity compilation. |
| #81, #106, #186, #189, #200, #234, #241, #246, #277, #281, #285, #413 | Retained feature requests: projection flipping, source-specific bounce/falloff, sky preservation, texture variation, colour grading, brush-volume/alternate-position lighting and transmission policies are not established by this audit. |
| #114, #254, #257, #264, #270, #278, #344, #350 | Retained geometry/runtime risks: bounds checks and transactional output do not prove semantic contents, hull fill, off-grid geometry, format detection or engine extent correctness for the reported maps. |
| #230, #308 | Retained path/origin reports: general path hardening does not establish dotted light-output naming, and region culling fixes do not establish brush-origin leak detection. |
| #209, #310 | Retained lighting input cases: native GoldSrc entities do not establish projected surface-light support or Quake II angle/mangle precedence. |
| #195 | Retained optional compatibility fixture: no documented issue-specific result for the reported Arcane Dimensions face. |
| #213, #233, #342, #411 | Retained conversion/source review: safer decompilation does not establish suffix expectations, empty-entity validity, unnamed-texture round trips or a dedicated decompile helper texture. |
| #222, #297, #455 | Retained documentation/authoring contracts: Quake II rangescale representation, dirt-radius help and compiler-only null-detail intent are not resolved by the reviewed recent changes. |

### Verification

On 2026-10-09, the six focused Meson tests passed on Windows with Qt 6.10.1:
`compiler-known-issues-smoke`, `quake-map-preflight-smoke`,
`compiler-registry-smoke`, `compiler-profiles-smoke`, `build-pipeline-smoke`
and `compiler-artifact-validation-smoke`. The task-owned build was configured
through `scripts/meson_build.ps1 -BuildDir .agents/tmp/vibemap2-audit/build -ConfigureOnly`.
This validates the shared wrapper services and their regression fixtures;
it does not rerun VibeMap2's own compiler or graphics tests.

### Integration boundaries

- The catalogue targets the pinned 3.1.0 source baseline. Discovery can accept
  user-selected executables but does not version-gate individual issue entries
  or re-enable old warnings for older/custom builds. Validate those separately.
- `-embedsettings` is opt-in; VibeStudio does not enable it automatically.
  Command manifests remain the full pipeline record, including invocations
  that cannot write a BSP. Forced luxel scale is also an explicit compiler option.
- Hub presence-only discovery is not a native Qt/OpenGL launch test. The registry
  advertises isolated preview workspaces separately from platform constraints.
- Map preflight, plan warnings, CLI and GUI use the same services. Removing a
  stale known-issue explanation does not suppress an actual compiler error or
  bypass output validation, dependency review, packaging or deployment checks.
- No compiler source was changed or imported. Native Windows/macOS/Linux
  compiler regression and visual acceptance remain separate from this audit.

## Historical remaining-pass matrix: 2026-05-02

The following 62-ID matrix records the original wrapper mitigation pass against
stock ericw-tools. Its statuses describe that pass, not current VibeMap2 defects;
the reconciliation above supersedes it for runtime warnings.

Date: 2026-05-02

Context snapshot: `.omx/context/ericw-remaining-bugs-20260502T094709Z.md`

Source audit: `docs/plans/ericw-tools-open-issues-audit.md`

This matrix records the Ralph remaining-bugs pass for the 62 ericw-tools issue
IDs that were outside the first high-value acceptance set. It does not claim
that ericw-tools compiler algorithms, BSP output, lighting output, VIS output,
or helper-tool argument parsing are fixed upstream. VibeStudio's ownership is
to warn, preflight, validate artifacts where local services can inspect them,
and keep upstream limitations visible.

## Status Definitions

| Status | Acceptance meaning |
|---|---|
| Implemented catalog warning | `src/core/compiler_known_issues.*` has an issue descriptor with upstream URL, severity, tool/profile scope, match keywords, warning text, and local action text. |
| Implemented map preflight warning | `src/core/quake_map_preflight.*` emits a map/entity/path warning before the compiler starts. These IDs also remain in the catalog for help text and matching. |
| Implemented registry/helper readiness | Registry or setup diagnostics make source/executable/probe/helper readiness visible before users rely on the tool. This pass adds first-class `bspinfo`, `bsputil`, and `lightpreview` discovery plus readiness warnings; deeper helper-operation probes remain a separate future wrapper task. |
| Implemented artifact validation | VibeStudio validates produced artifacts before promotion, packaging, or release. This pass adds lightweight expected-output, BSP-family, lump, face-reference, conversion-output, and BSPX metadata checks; catalog warnings remain the honest mitigation for output features the validator cannot inspect. |
| Upstream-only tracked limitation | VibeStudio can document, warn, or create regression fixtures, but the functional fix belongs in ericw-tools or a documented fork/submodule update. |

## Acceptance Matrix

Each issue ID appears in exactly one row in this table.

| Status | Issue IDs | Count | Acceptance note |
|---|---:|---:|---|
| Implemented map preflight warning | #455, #443, #342, #327, #291, #268, #262, #256, #255, #254, #247, #241, #238, #233, #217, #209, #190, #172, #140, #136, #129, #121 | 22 | Preflight catches the source-map, entity, brush, bmodel, texture, prefab, and selected lighting-risk shapes before invoking ericw-tools. |
| Implemented catalog warning | #437, #411, #401, #374, #351, #298, #297, #214, #203, #195, #186, #185, #145, #134, #109, #106, #105, #91, #81 | 19 | Catalog descriptors provide scoped warnings and local actions where VibeStudio cannot safely infer a specific source-map violation or cannot inspect the requested output feature directly. |
| Implemented registry/helper readiness | #480, #449, #335, #225, #223, #215 | 6 | Registry descriptors and setup diagnostics make generic binary names, helper availability, captured output, packaging names, and optional `lightpreview` launch risk visible. Operation-level helper behavior remains upstream-owned unless a smoke-tested wrapper operation exists. |
| Implemented artifact validation | #415, #309, #249, #213 | 4 | VibeStudio now preserves manifests, inspects generated BSP artifacts, and keeps conversion output paths plus BSPX/lightmap metadata expectations explicit. Compiler-side output generation remains upstream-owned, and catalog warnings cover decompile/export features the validator cannot inspect. |
| Upstream-only tracked limitation | #436, #425, #413, #285, #281, #277, #246, #234, #222, #200, #189 | 11 | These are compiler features, lighting/material controls, performance changes, or internal cleanup requests. VibeStudio tracks them and may expose options only after upstream support exists or a documented fork is approved. |

Total: 62 distinct issue IDs.

## Boundary Notes

- Map preflight warnings mitigate risky inputs; they do not prove the compiler
  will emit correct geometry, lighting, VIS data, or BSPX lumps.
- Catalog warnings are product-owned explanation and triage. They are not
  upstream fixes.
- Registry/helper readiness is limited to discoverability, probe results, and
  operation smoke checks. Native `bsputil`, `bspinfo`, and `lightpreview`
  behavior remains upstream-owned.
- Artifact validation can reject, quarantine, or warn about outputs before
  packaging. It does not repair a bad BSP or replace ericw-tools algorithmic
  fixes.
- New compiler features should be exposed in VibeStudio only after upstream
  support exists or after a documented VibeStudio fork is approved.
