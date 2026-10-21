#pragma once
#include "mod_runtime.h"

/* MSVC discards the framework's unreferenced *static* registration pointer,
 * even inside .CRT$XCU, so no plugin registered in Visual Studio builds and
 * the launcher reported "The executable does not provide this trusted
 * plugin". Keep an external pointer and tell the linker to retain it.
 * GCC/Clang builds keep the framework's constructor attribute. The proper
 * home for this is snesrecomp's mod_runtime.h. */
#if defined(_MSC_VER)
#undef SNES_MOD_CONSTRUCTOR
#define SNES_MOD_CONSTRUCTOR(name)                                          \
    static void __cdecl name(void);                                         \
    __declspec(allocate(".CRT$XCU"))                                        \
    void (__cdecl* name##_constructor)(void) = name;                        \
    __pragma(comment(linker, "/include:" #name "_constructor"))             \
    static void __cdecl name(void)
#endif
