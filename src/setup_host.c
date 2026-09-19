/* A setup host has no generated interrupt entries. The shared SnesInit
 * guard refuses guest boot; these fail loudly if that contract is broken. */
#if !defined(SNESRECOMP_SETUP_HOST)
#error "Only build this file in the ROM-free setup host"
#endif
#include <stdio.h>
#include <stdlib.h>
#include "cpu_state.h"

static void CannotRunGuest(void) {
  fputs("Mega Man X setup host: generate the game sources before playing.\n", stderr);
  abort();
}
RecompReturn I_RESET(CpuState *cpu) { (void)cpu; CannotRunGuest(); return 0; }
RecompReturn I_NMI(CpuState *cpu) { (void)cpu; CannotRunGuest(); return 0; }
RecompReturn I_IRQ(CpuState *cpu) { (void)cpu; CannotRunGuest(); return 0; }
