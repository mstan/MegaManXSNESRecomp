/*
 * post_mortem.c — unified exit/crash diagnostic dump.
 *
 * Writes a single JSON file (build/last_run_report.json) at exit / crash
 * / on-demand TCP request, snapshotting:
 *   - reason + UTC timestamp
 *   - SEH info (when triggered from the Windows SEH filter)
 *   - SMW status (frame counter, main cycles, trace ring write index)
 *   - hardware state (CpuState, GameMode region, DP region, sprite arrays)
 *   - recomp call stack (g_recomp_stack[])
 *   - cpu_trace ring (last 256 events)
 *   - cpu_dbpb ring (last 16 DB/PB transitions)
 *   - scoped tripwire snapshot (when armed/triggered)
 *   - PX tripwire snapshot (when armed/triggered)
 *   - per-thread host stacks (Toolhelp32 + DbgHelp StackWalk64)
 *
 * Triggered from:
 *   - SEH unhandled-exception filter in main.c (recomp_post_mortem_dump("seh", info))
 *   - signal() crash handler in main.c (recomp_post_mortem_dump("signal", NULL))
 *   - atexit() registered in main()
 *   - TCP "post_mortem_dump" command in debug_server.c
 *
 * Design notes (mirrors PokemonStadiumRecomp/src/main/post_mortem.cpp):
 *   - One file per run, overwritten each dump (last write wins). Don't
 *     tail-append — the file is meant to be parsed.
 *   - All payload comes from already-existing rings; this module is a
 *     serializer, not a recorder.
 *   - JSON is hand-rolled (no allocations on the SEH path; the SEH
 *     handler runs in unstable state).
 *   - Mutex so on-demand TCP dump and SEH/atexit dump can't race.
 */

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dbghelp.h>
#include <tlhelp32.h>
#pragma comment(lib, "dbghelp.lib")
#endif

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "post_mortem.h"
#include "cpu_state.h"
#include "cpu_trace.h"

/* ── External hooks into existing rings ──────────────────────────────── */

extern uint8_t g_ram[0x20000];
extern CpuState g_cpu;
extern int snes_frame_counter;
extern uint64_t g_main_cpu_cycles_estimate;

extern const char *g_recomp_stack[];
extern int g_recomp_stack_top;
extern const char *g_last_recomp_func;

/* Output path — overwritten per dump. */
static const char *kReportPath =
    "F:/Projects/snesrecomp/SuperMarioWorldRecomp/build/last_run_report.json";

/* Mutex so on-demand TCP dump and SEH/atexit dump can't race. */
#ifdef _WIN32
static CRITICAL_SECTION g_dump_mutex;
static volatile LONG    g_dump_mutex_init = 0;
static void dump_lock(void) {
    if (InterlockedCompareExchange(&g_dump_mutex_init, 1, 0) == 0) {
        InitializeCriticalSection(&g_dump_mutex);
    }
    EnterCriticalSection(&g_dump_mutex);
}
static void dump_unlock(void) { LeaveCriticalSection(&g_dump_mutex); }
#else
#include <pthread.h>
static pthread_mutex_t g_dump_mutex = PTHREAD_MUTEX_INITIALIZER;
static void dump_lock(void)   { pthread_mutex_lock(&g_dump_mutex); }
static void dump_unlock(void) { pthread_mutex_unlock(&g_dump_mutex); }
#endif

/* ── JSON helpers ───────────────────────────────────────────────────── */

/* Escape a C string for JSON. Output written to `out` up to `cap-1` bytes
 * plus a NUL. Handles backslash, quote, and control bytes. */
static void json_escape(const char *src, char *out, size_t cap) {
    if (cap == 0) return;
    size_t o = 0;
    if (src == NULL) { out[0] = 0; return; }
    for (const char *p = src; *p && o + 6 < cap; p++) {
        unsigned char c = (unsigned char)*p;
        if (c == '\\' || c == '"') {
            out[o++] = '\\'; out[o++] = (char)c;
        } else if (c == '\n') {
            out[o++] = '\\'; out[o++] = 'n';
        } else if (c == '\r') {
            out[o++] = '\\'; out[o++] = 'r';
        } else if (c == '\t') {
            out[o++] = '\\'; out[o++] = 't';
        } else if (c < 0x20) {
            o += (size_t)snprintf(out + o, cap - o, "\\u%04x", c);
        } else {
            out[o++] = (char)c;
        }
    }
    out[o] = 0;
}

/* Emit a JSON array of bytes "[N,N,...]" from a g_ram slice. */
static void dump_ram_bytes_json(FILE *f, uint32_t off, uint32_t n) {
    fputc('[', f);
    for (uint32_t i = 0; i < n; i++) {
        fprintf(f, "%s%u", (i ? "," : ""), (unsigned)g_ram[off + i]);
    }
    fputc(']', f);
}

/* ── Section dumpers ────────────────────────────────────────────────── */

static void dump_seh_json(FILE *f, void *fault_info_void) {
#ifdef _WIN32
    EXCEPTION_POINTERS *info = (EXCEPTION_POINTERS *)fault_info_void;
    if (info == NULL) {
        fprintf(f, "  \"seh\": null,\n");
        return;
    }
    EXCEPTION_RECORD *er = info->ExceptionRecord;
    fprintf(f,
        "  \"seh\": {\n"
        "    \"code\": %lu,\n"
        "    \"address\": %llu,\n"
        "    \"last_recomp_func\": \"%s\"",
        (unsigned long)er->ExceptionCode,
        (unsigned long long)(uintptr_t)er->ExceptionAddress,
        g_last_recomp_func ? g_last_recomp_func : "(none)");
    if (er->ExceptionCode == EXCEPTION_ACCESS_VIOLATION
        && er->NumberParameters >= 2) {
        const char *op =
            er->ExceptionInformation[0] == 0 ? "read" :
            er->ExceptionInformation[0] == 1 ? "write" : "execute";
        fprintf(f,
            ",\n    \"access\": \"%s\",\n"
            "    \"fault_addr\": %llu",
            op, (unsigned long long)er->ExceptionInformation[1]);
    }
    fprintf(f, "\n  },\n");
#else
    (void)fault_info_void;
    fprintf(f, "  \"seh\": null,\n");
#endif
}

static void dump_status_json(FILE *f) {
    uint64_t trace_idx = 0;
    uint64_t dbpb_idx  = 0;
#if SNESRECOMP_TRACE
    trace_idx = g_cpu_trace_idx;
    dbpb_idx  = g_cpu_dbpb_idx;
#endif
    fprintf(f,
        "  \"status\": {\n"
        "    \"frame\": %d,\n"
        "    \"main_cycles\": %llu,\n"
        "    \"trace_write_idx\": %llu,\n"
        "    \"dbpb_write_idx\": %llu\n"
        "  },\n",
        snes_frame_counter,
        (unsigned long long)g_main_cpu_cycles_estimate,
        (unsigned long long)trace_idx,
        (unsigned long long)dbpb_idx);
}

static void dump_hardware_state_json(FILE *f) {
    fprintf(f,
        "  \"cpu\": {\n"
        "    \"A\": %u, \"X\": %u, \"Y\": %u, \"S\": %u, \"D\": %u,\n"
        "    \"DB\": %u, \"PB\": %u, \"P\": %u,\n"
        "    \"m_flag\": %u, \"x_flag\": %u, \"emulation\": %u\n"
        "  },\n",
        (unsigned)g_cpu.A, (unsigned)g_cpu.X, (unsigned)g_cpu.Y,
        (unsigned)g_cpu.S, (unsigned)g_cpu.D,
        (unsigned)g_cpu.DB, (unsigned)g_cpu.PB, (unsigned)g_cpu.P,
        (unsigned)g_cpu.m_flag, (unsigned)g_cpu.x_flag,
        (unsigned)g_cpu.emulation);

    fprintf(f, "  \"wram\": {\n");
    fprintf(f, "    \"GameMode_0100\": ");          dump_ram_bytes_json(f, 0x0100, 16);  fprintf(f, ",\n");
    fprintf(f, "    \"DP_0080\": ");                dump_ram_bytes_json(f, 0x0080, 32);  fprintf(f, ",\n");
    fprintf(f, "    \"DP_low_0000\": ");            dump_ram_bytes_json(f, 0x0000, 32);  fprintf(f, ",\n");
    fprintf(f, "    \"sprite_type_009E\": ");       dump_ram_bytes_json(f, 0x009E, 12);  fprintf(f, ",\n");
    fprintf(f, "    \"sprite_status_14C8\": ");     dump_ram_bytes_json(f, 0x14C8, 12);  fprintf(f, ",\n");
    fprintf(f, "    \"player_state_1925\": ");      dump_ram_bytes_json(f, 0x1925, 32);  fprintf(f, ",\n");
    fprintf(f, "    \"init_sig_7F8000\": ");        dump_ram_bytes_json(f, 0x18000, 4);  fprintf(f, "\n");
    fprintf(f, "  },\n");
}

static void dump_recomp_stack_json(FILE *f) {
    fprintf(f, "  \"recomp_stack\": {\n"
               "    \"top\": %d,\n"
               "    \"last_func\": \"%s\",\n"
               "    \"frames\": [",
               g_recomp_stack_top,
               g_last_recomp_func ? g_last_recomp_func : "(none)");
    int n = g_recomp_stack_top;
    if (n < 0) n = 0;
    if (n > 64) n = 64;
    for (int i = n - 1; i >= 0; i--) {
        char esc[256];
        json_escape(g_recomp_stack[i] ? g_recomp_stack[i] : "?", esc, sizeof(esc));
        fprintf(f, "%s\"%s\"", (i == n - 1 ? "" : ","), esc);
    }
    fprintf(f, "]\n  },\n");
}

#if SNESRECOMP_TRACE
static const char *trace_event_name(uint8_t et) {
    switch (et) {
        case CPU_TR_BLOCK:       return "BLOCK";
        case CPU_TR_PHB:         return "PHB";
        case CPU_TR_PLB:         return "PLB";
        case CPU_TR_PHK:         return "PHK";
        case CPU_TR_PLP:         return "PLP";
        case CPU_TR_PHP:         return "PHP";
        case CPU_TR_RTI:         return "RTI";
        case CPU_TR_JSL:         return "JSL";
        case CPU_TR_RTL:         return "RTL";
        case CPU_TR_MVN:         return "MVN";
        case CPU_TR_MVP:         return "MVP";
        case CPU_TR_DB_WRITE:    return "DB_WRITE";
        case CPU_TR_PB_WRITE:    return "PB_WRITE";
        case CPU_TR_FUNC_ENTRY:  return "FUNC_ENTRY";
        case CPU_TR_WRAM_WRITE:  return "WRAM_WRITE";
        default:                 return "?";
    }
}
#endif

static void dump_trace_recent_json(FILE *f, int n_max) {
#if SNESRECOMP_TRACE
    uint64_t widx = g_cpu_trace_idx;
    int n = n_max;
    if ((uint64_t)n > widx) n = (int)widx;
    if ((uint64_t)n > g_cpu_trace_capacity) n = (int)g_cpu_trace_capacity;
    fprintf(f, "  \"trace_recent\": {\"write_idx\": %llu, \"events\": [",
            (unsigned long long)widx);
    for (int i = 0; i < n; i++) {
        uint64_t off = widx - n + i;
        const CpuTraceEvent *e = &g_cpu_trace_ring[off % g_cpu_trace_capacity];
        fprintf(f,
            "%s{\"i\":%llu,\"type\":\"%s\",\"pc24\":%u,"
            "\"A\":%u,\"X\":%u,\"Y\":%u,\"S\":%u,\"D\":%u,"
            "\"DB\":%u,\"PB\":%u,\"P\":%u,\"M\":%u,\"XF\":%u,"
            "\"e0\":%u,\"e1\":%u,"
            "\"bank\":%u,\"addr16\":%u,\"width\":%u,"
            "\"old_value\":%u,\"new_value\":%u}",
            (i ? "," : ""), (unsigned long long)off,
            trace_event_name(e->event_type),
            (unsigned)e->pc24,
            (unsigned)e->A, (unsigned)e->X, (unsigned)e->Y,
            (unsigned)e->S, (unsigned)e->D,
            (unsigned)e->DB, (unsigned)e->PB, (unsigned)e->P,
            (unsigned)e->M, (unsigned)e->XF,
            (unsigned)e->extra0, (unsigned)e->extra1,
            (unsigned)e->bank, (unsigned)e->addr16, (unsigned)e->width,
            (unsigned)e->old_value, (unsigned)e->new_value);
    }
    fprintf(f, "]},\n");
#else
    (void)n_max;
    fprintf(f, "  \"trace_recent\": {\"disabled\": true},\n");
#endif
}

static void dump_dbpb_recent_json(FILE *f) {
#if SNESRECOMP_TRACE
    uint64_t widx = g_cpu_dbpb_idx;
    int n = CPU_DBPB_RING_LEN;
    if ((uint64_t)n > widx) n = (int)widx;
    fprintf(f, "  \"dbpb_recent\": {\"write_idx\": %llu, \"events\": [",
            (unsigned long long)widx);
    for (int i = 0; i < n; i++) {
        uint64_t off = widx - n + i;
        const CpuDbpbEvent *e = &g_cpu_dbpb_ring[off % CPU_DBPB_RING_LEN];
        fprintf(f,
            "%s{\"i\":%llu,\"type\":\"%s\",\"reg\":\"%s\","
            "\"old\":%u,\"new\":%u,\"S\":%u,\"pc24\":%u}",
            (i ? "," : ""), (unsigned long long)off,
            trace_event_name(e->event_type),
            e->reg_id == 0 ? "DB" : "PB",
            (unsigned)e->old_val, (unsigned)e->new_val,
            (unsigned)e->S, (unsigned)e->pc24);
    }
    fprintf(f, "]},\n");
#else
    fprintf(f, "  \"dbpb_recent\": {\"disabled\": true},\n");
#endif
}

static void dump_tripwires_json(FILE *f) {
#if SNESRECOMP_TRACE
    /* Scoped (WRAM-range) tripwire */
    fprintf(f,
        "  \"scoped_tripwire\": {\n"
        "    \"armed\": %u, \"triggered\": %u,\n"
        "    \"bank\": %u, \"addr_lo\": %u, \"addr_hi\": %u,\n"
        "    \"match_enabled\": %u, \"match_val\": %u,\n",
        (unsigned)g_scoped_tripwire.armed,
        (unsigned)g_scoped_tripwire.triggered,
        (unsigned)g_scoped_tripwire.bank,
        (unsigned)g_scoped_tripwire.addr_lo,
        (unsigned)g_scoped_tripwire.addr_hi,
        (unsigned)g_scoped_tripwire.match_enabled,
        (unsigned)g_scoped_tripwire.match_val);
    if (g_scoped_tripwire.triggered) {
        char esc[128];
        fprintf(f,
            "    \"frame\": %d, \"main_cycles\": %llu, \"trace_idx\": %llu,\n"
            "    \"hit_addr\": %u, \"hit_val\": %u, \"width_seen\": %u,\n"
            "    \"cpu\": {\"A\":%u,\"X\":%u,\"Y\":%u,\"S\":%u,\"D\":%u,"
            "\"DB\":%u,\"PB\":%u,\"P\":%u,"
            "\"m_flag\":%u,\"x_flag\":%u,\"e_flag\":%u},\n",
            g_scoped_tripwire.frame,
            (unsigned long long)g_scoped_tripwire.main_cycles,
            (unsigned long long)g_scoped_tripwire.trace_idx,
            (unsigned)g_scoped_tripwire.hit_addr,
            (unsigned)g_scoped_tripwire.hit_val,
            (unsigned)g_scoped_tripwire.width_seen,
            (unsigned)g_scoped_tripwire.A, (unsigned)g_scoped_tripwire.X,
            (unsigned)g_scoped_tripwire.Y, (unsigned)g_scoped_tripwire.S,
            (unsigned)g_scoped_tripwire.D,
            (unsigned)g_scoped_tripwire.DB, (unsigned)g_scoped_tripwire.PB,
            (unsigned)g_scoped_tripwire.P,
            (unsigned)g_scoped_tripwire.m_flag,
            (unsigned)g_scoped_tripwire.x_flag,
            (unsigned)g_scoped_tripwire.e_flag);
        json_escape(g_scoped_tripwire.last_func_name, esc, sizeof(esc));
        fprintf(f, "    \"last_func\": \"%s\",\n", esc);
        fprintf(f, "    \"stack\": [");
        for (int i = 0; i < g_scoped_tripwire.stack_depth; i++) {
            json_escape(g_scoped_tripwire.stack[i], esc, sizeof(esc));
            fprintf(f, "%s\"%s\"", (i ? "," : ""), esc);
        }
        fprintf(f, "],\n    \"dp\": [");
        for (int i = 0; i < SCOPED_TRIPWIRE_DP_BYTES; i++)
            fprintf(f, "%s%u", (i ? "," : ""), (unsigned)g_scoped_tripwire.dp_snapshot[i]);
        fprintf(f, "],\n    \"gm\": [");
        for (int i = 0; i < SCOPED_TRIPWIRE_GM_BYTES; i++)
            fprintf(f, "%s%u", (i ? "," : ""), (unsigned)g_scoped_tripwire.gm_snapshot[i]);
        fprintf(f, "],\n    \"dp_low\": [");
        for (int i = 0; i < 32; i++)
            fprintf(f, "%s%u", (i ? "," : ""), (unsigned)g_scoped_tripwire.dp_low_snapshot[i]);
        fprintf(f, "]\n");
    } else {
        fprintf(f, "    \"triggered\": false\n");
    }
    fprintf(f, "  },\n");

    /* PX tripwire */
    fprintf(f,
        "  \"px_tripwire\": {\n"
        "    \"armed\": %u, \"triggered\": %u,\n"
        "    \"breadcrumb_count\": %u, \"pmut_count\": %u",
        (unsigned)g_px_tripwire.armed,
        (unsigned)g_px_tripwire.triggered,
        (unsigned)g_px_tripwire.breadcrumb_count,
        (unsigned)g_px_tripwire.pmut_count);
    if (g_px_tripwire.triggered) {
        char esc[128];
        fprintf(f,
            ",\n    \"trip_event\": {\"pc24\":%u,\"source_kind\":%u,"
            "\"old_p\":%u,\"new_p\":%u,\"old_x\":%u,\"new_x\":%u,\"S\":%u},\n"
            "    \"trip_trace_idx\": %u,\n"
            "    \"cpu\": {\"A\":%u,\"X\":%u,\"Y\":%u,\"S\":%u,\"D\":%u,"
            "\"DB\":%u,\"PB\":%u,\"P\":%u,"
            "\"m_flag\":%u,\"x_flag\":%u,\"e_flag\":%u},\n",
            (unsigned)g_px_tripwire.trip_event.pc24,
            (unsigned)g_px_tripwire.trip_event.source_kind,
            (unsigned)g_px_tripwire.trip_event.old_p,
            (unsigned)g_px_tripwire.trip_event.new_p,
            (unsigned)g_px_tripwire.trip_event.old_x_flag,
            (unsigned)g_px_tripwire.trip_event.new_x_flag,
            (unsigned)g_px_tripwire.trip_event.S,
            (unsigned)g_px_tripwire.trip_trace_idx,
            (unsigned)g_px_tripwire.A, (unsigned)g_px_tripwire.X,
            (unsigned)g_px_tripwire.Y, (unsigned)g_px_tripwire.S,
            (unsigned)g_px_tripwire.D,
            (unsigned)g_px_tripwire.DB, (unsigned)g_px_tripwire.PB,
            (unsigned)g_px_tripwire.P,
            (unsigned)g_px_tripwire.m_flag,
            (unsigned)g_px_tripwire.x_flag,
            (unsigned)g_px_tripwire.e_flag);
        json_escape(g_px_tripwire.last_func, esc, sizeof(esc));
        fprintf(f, "    \"last_func\": \"%s\",\n", esc);
        fprintf(f, "    \"stack\": [");
        for (int i = 0; i < g_px_tripwire.stack_depth; i++) {
            json_escape(g_px_tripwire.stack[i], esc, sizeof(esc));
            fprintf(f, "%s\"%s\"", (i ? "," : ""), esc);
        }
        fprintf(f, "]");
    }
    /* Frozen pmut ring (oldest-first within retained window) */
    fprintf(f, ",\n    \"pmut_ring\": [");
    {
        uint32_t cnt = g_px_tripwire.pmut_count;
        if (cnt > PX_TRIPWIRE_PMUT_RING) cnt = PX_TRIPWIRE_PMUT_RING;
        uint32_t base = (g_px_tripwire.pmut_write_idx - cnt) % PX_TRIPWIRE_PMUT_RING;
        for (uint32_t i = 0; i < cnt; i++) {
            const PxPMutEvent *e = &g_px_tripwire.pmut_ring[(base + i) % PX_TRIPWIRE_PMUT_RING];
            fprintf(f,
                "%s{\"pc24\":%u,\"kind\":%u,\"old_p\":%u,\"new_p\":%u,"
                "\"old_x\":%u,\"new_x\":%u,\"S\":%u}",
                (i ? "," : ""),
                (unsigned)e->pc24, (unsigned)e->source_kind,
                (unsigned)e->old_p, (unsigned)e->new_p,
                (unsigned)e->old_x_flag, (unsigned)e->new_x_flag,
                (unsigned)e->S);
        }
    }
    fprintf(f, "],\n    \"breadcrumbs\": [");
    {
        uint32_t bcnt = g_px_tripwire.breadcrumb_count;
        if (bcnt > PX_BREADCRUMB_MAX) bcnt = PX_BREADCRUMB_MAX;
        for (uint32_t i = 0; i < bcnt; i++) {
            const PxBreadcrumb *b = &g_px_tripwire.breadcrumbs[i];
            char esc[128];
            json_escape(b->label, esc, sizeof(esc));
            fprintf(f,
                "%s{\"marker\":%u,\"label\":\"%s\","
                "\"P\":%u,\"m\":%u,\"x\":%u,\"e\":%u,"
                "\"A\":%u,\"X\":%u,\"Y\":%u,\"S\":%u,\"D\":%u,"
                "\"DB\":%u,\"PB\":%u}",
                (i ? "," : ""),
                (unsigned)b->marker, esc,
                (unsigned)b->P, (unsigned)b->m_flag,
                (unsigned)b->x_flag, (unsigned)b->e_flag,
                (unsigned)b->A, (unsigned)b->X, (unsigned)b->Y,
                (unsigned)b->S, (unsigned)b->D,
                (unsigned)b->DB, (unsigned)b->PB);
        }
    }
    fprintf(f, "]\n  },\n");
#else
    fprintf(f, "  \"scoped_tripwire\": {\"disabled\": true},\n");
    fprintf(f, "  \"px_tripwire\": {\"disabled\": true},\n");
#endif
}

#ifdef _WIN32

/* Walk one thread's host stack, write up to `max_frames` frames as
 * comma-separated JSON objects to `f`. Caller wraps in []. */
static void dump_thread_stack_json(FILE *f, HANDLE proc, HANDLE thr,
                                   CONTEXT *ctx, int max_frames) {
    STACKFRAME64 frame;
    memset(&frame, 0, sizeof(frame));
    DWORD machine = IMAGE_FILE_MACHINE_AMD64;
    frame.AddrPC.Offset    = ctx->Rip; frame.AddrPC.Mode    = AddrModeFlat;
    frame.AddrFrame.Offset = ctx->Rbp; frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = ctx->Rsp; frame.AddrStack.Mode = AddrModeFlat;
    char symbuf[sizeof(SYMBOL_INFO) + 256];
    SYMBOL_INFO *sym = (SYMBOL_INFO *)symbuf;
    memset(symbuf, 0, sizeof(symbuf));
    sym->SizeOfStruct = sizeof(SYMBOL_INFO);
    sym->MaxNameLen = 255;
    IMAGEHLP_LINE64 line; memset(&line, 0, sizeof(line));
    line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);

    int first = 1;
    for (int i = 0; i < max_frames; i++) {
        if (!StackWalk64(machine, proc, thr, &frame, ctx, NULL,
                         SymFunctionTableAccess64, SymGetModuleBase64, NULL))
            break;
        if (!frame.AddrPC.Offset) break;
        DWORD64 disp64 = 0; DWORD disp32 = 0;
        const char *name = "?"; const char *file = "?"; DWORD lineno = 0;
        if (SymFromAddr(proc, frame.AddrPC.Offset, &disp64, sym))
            name = sym->Name;
        if (SymGetLineFromAddr64(proc, frame.AddrPC.Offset, &disp32, &line)) {
            file = line.FileName; lineno = line.LineNumber;
        }
        char esc_name[256], esc_file[512];
        json_escape(name, esc_name, sizeof(esc_name));
        json_escape(file, esc_file, sizeof(esc_file));
        if (!first) fputs(",", f);
        first = 0;
        fprintf(f,
            "{\"i\":%d,\"pc\":%llu,\"name\":\"%s\",\"file\":\"%s\",\"line\":%lu}",
            i, (unsigned long long)frame.AddrPC.Offset,
            esc_name, esc_file, (unsigned long)lineno);
    }
}

static void dump_all_threads_json(FILE *f, void *fault_info_void) {
    EXCEPTION_POINTERS *fault_info = (EXCEPTION_POINTERS *)fault_info_void;
    HANDLE proc = GetCurrentProcess();
    SymInitialize(proc, NULL, TRUE);

    DWORD pid = GetCurrentProcessId();
    DWORD self_tid = GetCurrentThreadId();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        fprintf(f, "  \"threads\": [],\n  \"threads_error\": \"snapshot_failed\"\n");
        return;
    }

    THREADENTRY32 te; memset(&te, 0, sizeof(te));
    te.dwSize = sizeof(te);
    int first_thread = 1;
    fprintf(f, "  \"threads\": [\n");
    if (Thread32First(snap, &te)) {
        do {
            if (te.th32OwnerProcessID != pid) continue;
            DWORD tid = te.th32ThreadID;

            CONTEXT thr_ctx; memset(&thr_ctx, 0, sizeof(thr_ctx));
            thr_ctx.ContextFlags = CONTEXT_FULL;
            HANDLE thr = NULL;
            int suspended = 0;

            if (tid == self_tid) {
                if (fault_info) {
                    thr_ctx = *fault_info->ContextRecord;
                } else {
                    RtlCaptureContext(&thr_ctx);
                }
                thr = GetCurrentThread();
            } else {
                thr = OpenThread(THREAD_GET_CONTEXT | THREAD_SUSPEND_RESUME |
                                 THREAD_QUERY_INFORMATION, FALSE, tid);
                if (!thr) continue;
                if (SuspendThread(thr) == (DWORD)-1) {
                    CloseHandle(thr); continue;
                }
                suspended = 1;
                if (!GetThreadContext(thr, &thr_ctx)) {
                    ResumeThread(thr); CloseHandle(thr); continue;
                }
            }

            /* GetThreadDescription is Windows 10+; resolve dynamically so we
             * don't break older Windows targets. */
            char thr_name[128] = "";
            typedef HRESULT (WINAPI *pGetThreadDescription_t)(HANDLE, PWSTR*);
            static pGetThreadDescription_t pGTD = NULL;
            static int gtd_resolved = 0;
            if (!gtd_resolved) {
                HMODULE k32 = GetModuleHandleA("kernel32.dll");
                if (k32) pGTD = (pGetThreadDescription_t)
                    GetProcAddress(k32, "GetThreadDescription");
                gtd_resolved = 1;
            }
            if (pGTD) {
                PWSTR name_w = NULL;
                if (SUCCEEDED(pGTD(thr, &name_w)) && name_w) {
                    WideCharToMultiByte(CP_UTF8, 0, name_w, -1,
                                        thr_name, (int)sizeof(thr_name), NULL, NULL);
                    thr_name[sizeof(thr_name) - 1] = 0;
                    LocalFree(name_w);
                }
            }
            char esc_name[256];
            json_escape(thr_name, esc_name, sizeof(esc_name));

            if (!first_thread) fputs(",\n", f);
            first_thread = 0;
            fprintf(f,
                "    {\"tid\":%lu,\"name\":\"%s\","
                "\"rip\":%llu,\"rsp\":%llu,\"rbp\":%llu,"
                "\"frames\":[",
                (unsigned long)tid, esc_name,
                (unsigned long long)thr_ctx.Rip,
                (unsigned long long)thr_ctx.Rsp,
                (unsigned long long)thr_ctx.Rbp);
            dump_thread_stack_json(f, proc, thr, &thr_ctx, 24);
            fputs("]}", f);

            if (suspended) {
                ResumeThread(thr);
                CloseHandle(thr);
            }
        } while (Thread32Next(snap, &te));
    }
    fprintf(f, "\n  ]\n");
    CloseHandle(snap);
}

#else  /* !_WIN32 */

static void dump_all_threads_json(FILE *f, void *fault_info_void) {
    (void)fault_info_void;
    fprintf(f, "  \"threads\": []\n");
}

#endif

/* ── Public entry ───────────────────────────────────────────────────── */

void recomp_post_mortem_dump(const char *reason, void *fault_info) {
    dump_lock();

    FILE *f = fopen(kReportPath, "w");
    if (!f) { dump_unlock(); return; }

    char timebuf[64] = "?";
    time_t tt = time(NULL);
    struct tm tmbuf;
#ifdef _WIN32
    if (gmtime_s(&tmbuf, &tt) == 0)
#else
    if (gmtime_r(&tt, &tmbuf) != NULL)
#endif
    {
        strftime(timebuf, sizeof(timebuf), "%Y-%m-%dT%H:%M:%SZ", &tmbuf);
    }

    char esc_reason[128];
    json_escape(reason ? reason : "unknown", esc_reason, sizeof(esc_reason));
    fprintf(f,
        "{\n"
        "  \"reason\": \"%s\",\n"
        "  \"timestamp\": \"%s\",\n",
        esc_reason, timebuf);

    dump_seh_json(f, fault_info);
    dump_status_json(f);
    dump_hardware_state_json(f);
    dump_recomp_stack_json(f);
    dump_trace_recent_json(f, 256);
    dump_dbpb_recent_json(f);
    dump_tripwires_json(f);
    dump_all_threads_json(f, fault_info);

    fprintf(f, "}\n");
    fclose(f);

    dump_unlock();
}
