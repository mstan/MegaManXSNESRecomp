#ifndef BANKS_H
#define BANKS_H

#include "types.h"

/* v2 ABI: every recompiled function takes `CpuState *cpu` and mutates
 * register / flag state in place. The v1 aggregate typedefs (HdmaPtrs,
 * PairU8, OwHvPos, CollInfo, ExtCollOut, CheckPlatformCollRet,
 * CalcTiltPlatformArgs, RetAY, RetY, PairU16) are gone — none of the
 * v2-emitted code references them. The `RECOMP_BANK<BB>` macros and
 * the `bank_range.h` --range-bisection sieve are gone too: v2 always
 * emits all functions in a bank. */

#endif // BANKS_H
