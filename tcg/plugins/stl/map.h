#ifndef MAP_H
#define MAP_H

typedef union key {
    int k1;
    long k2;
} key;

typedef union value {
    char v1;
    char *v2;
} value;

typedef struct map
{
    key k;
    value v;
    struct map *next;
} map;

void PrintMap(map *head);

int IsKeyEquals(const key k1, const key k2);

map *CreatePair(const key k, const value v);

value GetValueInMap(const key k, map *head);

int IsValueInMap(const key k, map *head);

map *SetValueInMap(const key k, const value v, map *head);

map *DestoryMap(map *head);

#endif // !MAP_H
