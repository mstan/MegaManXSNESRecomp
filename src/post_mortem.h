#pragma once

/* post_mortem.h — unified exit/crash diagnostic dump.
 *
 * Single public entry. See post_mortem.c for what gets dumped and
 * design rationale. Output: build/last_run_report.json (overwritten).
 *
 * `reason` is a short tag ("seh" / "signal" / "atexit" / "on_demand").
 * `fault_info` is a Windows EXCEPTION_POINTERS* (cast to void* so the
 * header doesn't drag in windows.h); pass NULL outside the SEH path.
 */

#ifdef __cplusplus
extern "C" {
#endif

void recomp_post_mortem_dump(const char *reason, void *fault_info);

#ifdef __cplusplus
}
#endif
