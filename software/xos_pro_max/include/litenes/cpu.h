#include "common.h"

#ifndef CPU_H
#define CPU_H

byte cpu_ram_read(word address);
void cpu_ram_write(word address, byte data);

void cpu_init();
void cpu_reset();
void cpu_interrupt();
void cpu_run(long cycles);

// Single-step interpreter: execute instruction at given PC, update cpu state, return cycles consumed.
int cpu_step_interp(uint16_t pc);

// CPU cycles that passed since power up
unsigned long long cpu_clock();

#endif
