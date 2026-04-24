#ifndef LITENES_JIT_H
#define LITENES_JIT_H

#include <stdint.h>
#include <stdbool.h>

// Stub declarations to keep shell commands linkable even when JIT is absent.
extern bool jit_enabled;
extern bool difftest_enabled;
extern int difftest_pass_count;
extern int difftest_fail_count;

void jit_init(void);
int  jit_run(uint16_t pc);          // returns cycles or 0 on fallback
int  jit_run_single(uint16_t pc);   // single-step for DiffTest (no cache pollution)
void jit_print_stats(void);
void jit_reset_stats(void);
void jit_dump_code(void);

#endif // LITENES_JIT_H
