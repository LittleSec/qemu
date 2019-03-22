#ifndef WYCINWYC_H
#define WYCINWYC_H

#include "tcg-plugin.h"
#include <capstone/capstone.h>
#include "stl/vector.h"
#include "stl/map.h"

extern vector* mappings; // mappings.item.memory_range

//format specifier function-addresses for printf-tracking
extern target_ulong printf_addr;
extern target_ulong fprintf_addr;
extern target_ulong dprintf_addr;
extern target_ulong sprintf_addr;
extern target_ulong snprintf_addr;

//allocation/deallocation for heapobject-tracking
extern target_ulong malloc_addr;
extern target_ulong realloc_addr;
extern target_ulong free_addr;
extern target_ulong calloc_addr;
extern target_ulong malloc_r_addr;
extern target_ulong realloc_r_addr;
extern target_ulong free_r_addr;


#endif // ! WYCINWYC_H