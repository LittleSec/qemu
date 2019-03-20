#ifndef WYCINWYC_H
#define WYCINWYC_H

#include "tcg-plugin.h"
#include <capstone/capstone.h>
#include "stl/vector.h"
#include "stl/map.h"

vector* mappings; // mappings.item.memory_range

target_ulong printf_addr;
target_ulong fprintf_addr;
target_ulong dprintf_addr;
target_ulong sprintf_addr; 
target_ulong snprintf_addr;

target_ulong malloc_addr;
target_ulong malloc_r_addr;
target_ulong realloc_addr;
target_ulong realloc_r_addr;
target_ulong free_addr;
target_ulong free_r_addr;
target_ulong calloc_addr;


#endif // ! WYCINWYC_H