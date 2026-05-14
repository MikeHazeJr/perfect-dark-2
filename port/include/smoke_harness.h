#ifndef _IN_SMOKE_HARNESS_H
#define _IN_SMOKE_HARNESS_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Smoke verify harness (Phase 1, 2026-05-11; action / mouse added c115,
 * 2026-05-14).
 *
 * The harness drives the client through a scripted scenario described by
 * a JSON test definition (tools/smoke-verify/tests/*.json). It dispatches
 * input events at scheduled millisecond offsets, force-exits on timeout,
 * and writes a SMOKE: result=... marker that the PowerShell runner can
 * parse after the binary has exited.
 *
 * Supported event types (see port/src/smoke_harness.c for the full
 * schema and tools/smoke-verify/README.md for examples):
 *   - wait    : inert sequencing marker
 *   - exit    : scripted clean exit
 *   - key     : SDL_KEYDOWN / KEYUP
 *   - action  : actionmap press/release injected directly (focus-independent)
 *   - mouse   : SDL_MOUSEBUTTONDOWN / UP at {x, y}
 *
 * Design ref: context/designs/engine/smoke-verify-gate.md.
 *
 * CLI surface:
 *
 *   PerfectDark.exe --smoke <path-to-test.json>
 *
 * Lifecycle (called from main.c):
 *
 *   smokeHarnessInit() -- after configInit, returns 1 if --smoke was
 *                         present and parsing succeeded; 0 otherwise.
 *   smokeHarnessTick() -- once per frame from mainLoop's input dispatch
 *                         point; cheap no-op when harness inactive.
 *   smokeHarnessExit() -- forced exit; writes the SMOKE: result line
 *                         and calls exit(code).
 *
 * The harness is client-only (port/src/smoke_harness.c is auto-discovered
 * by the pd target's file(GLOB_RECURSE)); the server target's hand-curated
 * SRC_SERVER list does not include this module.
 */

int  smokeHarnessInit(void);
void smokeHarnessTick(void);
void smokeHarnessExit(int code, const char *reason);
int  smokeHarnessIsActive(void);

#ifdef __cplusplus
}
#endif

#endif /* _IN_SMOKE_HARNESS_H */
