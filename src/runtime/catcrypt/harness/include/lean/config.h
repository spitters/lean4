/* Stand-in for the CMake-generated lean/config.h, sufficient to compile
   src/runtime/mpn.cpp and mpz.cpp outside the Lean build: no mimalloc, no
   lazy RC, not stage 0. */
#pragma once
#include <lean/version.h>

#define LEAN_IS_STAGE0 0

#ifndef __cplusplus
void free(void *);
#endif
