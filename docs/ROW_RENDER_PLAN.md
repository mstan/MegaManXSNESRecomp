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

## Reconstructed background source spans (R1)

`MMX_NATIVE_BG_SPANS=ON` additionally resolves full-view map tiles, source-row
planes and private CHR/palette bindings per source span. Its development default
remains OFF by owner direction. The old scalar
`background()` remains the independent reference and handles per-pixel imported
menu art and the Rush ceiling; mosaic retains its existing path.

Spans end at native-view edges, source characters, scene reflections/clamps,
door cells, submarine bands and authored resource-phase changes. Resource
columns may advance forward, backward, at half-camera scale or remain clamped;
CHR and palette changes need not align with a tile. Bindings are private to a
captured row, with no new persistent cache, guest write or serialized state.

The prototype passes existing native scenes, 1,536 scalar-compared full-width
rows, separate non-aligned CHR/palette phase checks, all four Magic Carpet
gates and eight captured view/HUD comparisons. The row suite also executes on
Linux. Both clean-source OFF/ON builds pass current-build restore, rewind,
deterministic replay and fresh-process replay. A dedicated ROM-backed rollback
check also compares identical guest inputs while varying render widths and
draw counts; the resulting rollback state is identical.

Five balanced 4,200-frame Highway pairs per profile on an i5-4670 measured
median process-CPU reductions of 12.81% at 16:9 and 34.69% at 32:9, with all
five pairs positive in each wider view. Custom 4:3 and stock were
neutral/inconclusive; repeated OFF/ON, identical-binary and canonical-main
controls are retained. These results belong to this MMX title adapter and
package, not every SNES game; do not add percentages to the preceding row
package. Exact measured source is `ef821a32` (runtime code from `7e558267`).
Raw samples, source/runtime/binary identities, feature settings and size costs
are indexed in `campaign-results.json` under the task directory below.
Automated qualification is complete; combined owner playtest approval and
merge remain pending. No original Xbox or cross-build netplay claim is made.

Owner direction on 2026-10-28: finish this already-authorized title candidate,
then prioritize game-agnostic engine mechanisms. Reuse or elevate shared
operations with actual consumer validation; retain X1 scene and asset policy
here. X2/X3 already use the shared live PPU and a shared background provider,
so enabling this X1-specific build flag is not a direct port. Substantial
adoption belongs after the shared-engine burndown or is deferred.

Tracking: `beads-8wg.1.150`, roadmap
`beads-49cy`, campaign `beads-2wy8.1`. Evidence:
`G:/Temp/Codex/snesrecomp/background-spans-20261027/evidence`.
