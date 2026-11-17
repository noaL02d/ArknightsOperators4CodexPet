#pragma once

#ifdef PET_CONSOLE
#include <cstdio>
#define PETLOG(...) printf(__VA_ARGS__)
#else
#define PETLOG(...) ((void)0)
#endif
