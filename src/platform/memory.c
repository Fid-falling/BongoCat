#include "bongo_cat/memory.h"

#ifdef _WIN32
#include <malloc.h>
#elif defined(__APPLE__)
#include <malloc/malloc.h>
#elif defined(__GLIBC__)
#include <malloc.h>
#endif

void bongo_cat_platform_trim_memory(void) {
#ifdef _WIN32
    /* Return free CRT heap regions without evicting active code and data pages. */
    _heapmin();
#elif defined(__APPLE__)
    malloc_zone_pressure_relief(NULL, 0);
#elif defined(__GLIBC__)
    malloc_trim(0);
#endif
}
