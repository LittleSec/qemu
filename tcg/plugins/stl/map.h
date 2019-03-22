#ifndef MAP_H
#define MAP_H

#include "tcg-plugin.h"

typedef union key_map {
    target_ulong allocated_obj_k;
} key_map;

typedef union value_map {
    target_ulong allocated_obj_v;    
} value_map;

typedef struct map
{
    key_map k;
    value_map v;
    struct map *next;
} map;

void PrintMap(map *head);

int IsKeyEquals(const key_map k1, const key_map k2);

map *CreatePair(const key_map k, const value_map v);

value_map GetValueInMap(const key_map k, map *head);

int IsValueInMap(const key_map k, map *head);

map *SetValueInMap(const key_map k, const value_map v, map *head);

map *DestoryMap(map *head);

#endif // !MAP_H
