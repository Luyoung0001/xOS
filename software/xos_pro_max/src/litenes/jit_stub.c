#include "litenes/jit.h"
#include <stdio.h>

bool jit_enabled = false;
bool difftest_enabled = false;
int difftest_pass_count = 0;
int difftest_fail_count = 0;

void jit_init(void) {
    // nothing to init in stub
}

int jit_run(uint16_t pc) {
    (void)pc;
    return 0; // force fallback
}

void jit_print_stats(void) {
    printf("[JIT] stub: disabled\n");
}

void jit_reset_stats(void) {
    difftest_pass_count = 0;
    difftest_fail_count = 0;
}

void jit_dump_code(void) {
    printf("[JIT] stub: no code\n");
}
