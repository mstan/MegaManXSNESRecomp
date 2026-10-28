# Whole-view row rendering experiment

`MMX_NATIVE_ROW_PLAN=ON` selects a prepared-row pipeline in the NATIVE custom
renderer. It defaults ON for the qualified Windows x64 NATIVE configuration;
other platforms retain their existing defaults. Explicit OFF selects the
existing NATIVE compositor, and the separate LLE renderer remains buildable.

Each captured line/view prepares its background pixel and private-color planes
before composition. The shared `snes/raster_plan.h` job resolves widened hardware
windows, optional signed color-beam windows, enabled layers and native/margin
boundaries into at most eleven immutable spans. Composition selects main/sub
pixels with span masks rather than repeating window/enable decisions per pixel.

Scene-map reconstruction, native tile rows, palette/CHR ownership, sprite order,
color math, captured raster changes and guest execution remain authoritative.
This first experiment does not assume that reconstructed tiles or asset phases
are linear across every stage boundary. Map-binding spans are a separate possible
follow-up; this package does not implement them or GPU rendering.

The planner has no platform dependency, live PPU mutation, heap allocation or
persistent simulation state. The adapter uses bounded private row scratch;
rewind/rollback formats gain no host pointers or new serialized fields. The build
choice is recorded in the replay engine identity and benchmark JSON. No runtime
switch or cross-build state conversion is provided.

Validation uses the existing renderer scenes, 512 randomized full-color row
comparisons with scalar decisions, 4096 independent span/window comparisons,
ROM-backed Magic Carpet including its negative control, current-build restore,
rewind and fresh-process replay, and matched built-game A/B workloads. Per-game
CPU reductions and neutral results require recorded balanced samples; compilation
and component tests alone are not performance qualification.

Owning issue: `beads-8wg.2.161`; coordinating campaign: `beads-2wy8.1`. Bounded
raw receipts and source/binary/configuration identities are retained under
`G:/Temp/Codex/snesrecomp/full-view-20261027`. No original Xbox runtime or
cross-build netplay compatibility is claimed.

Five balanced A/B pairs per view on an Intel i5-4670 measured median process-CPU
reductions of 7.4% (custom 4:3), 9.6% (16:9) and 12.6% (32:9). The ordinary
stock-renderer workload was neutral/inconclusive. These are additional results
against the preceding optimized MMX build, not additive percentages or original
Xbox qualification. Raw samples, exact binary identities and settings are in
`campaign-results.json` beneath the evidence directory above.
