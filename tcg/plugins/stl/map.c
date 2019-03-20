#include <stdio.h>
#include <stdlib.h>
#include "map.h"

int IsKeyEquals(const key k1, const key k2)
{
    if (k1.k1 == k2.k1)
    {
        return 1;
    }
    else
    {
        return 0;
    }
}

map *CreatePair(const key k, const value v)
{
    map *m = (map *)malloc(sizeof(map));
    if (m == NULL)
    {
        printf("  [-] Malloc error!\n");
    }
    else
    {
        m->k = k;
        m->v = v;
        m->next = NULL;
    }
    return m;
}

value GetValueInMap(const key k, map *head)
{
    map *cur = head;
    for (; cur != NULL; cur = cur->next)
    {
        if (IsKeyEquals(cur->k, k))
        {
            return cur->v;
        }
    }
    printf("  [-] Get value error. Key not in Map!\n");
    return head->v;
}

int IsValueInMap(const key k, map *head)
{
    map *cur = head;
    for (; cur != NULL; cur = cur->next)
    {
        if (IsKeyEquals(cur->k, k))
        {
            return 1;
        }
    }
    return 0;
}

map *SetValueInMap(const key k, const value v, map *head)
{
    map *prev = head;
    map *cur = head;
    if (head == NULL)
    {
        head = CreatePair(k, v);
    }
    else
    {
        while (cur != NULL) // key in map
        {
            if (IsKeyEquals(cur->k, k))
            {
                cur->v = v;
                break;
            }
            prev = cur;
            cur = cur->next;
        }
        if (cur == NULL) // key not in map, insert.
        {
            map *m = CreatePair(k, v);
            prev->next = m;
        }
    }
    return head;
}

map *DestoryMap(map *head)
{
    map *p = head;
    map *q = NULL;
    if (head != NULL)
    {
        while (p->next != NULL)
        {
            q = p->next;
            p->next = q->next;
            free(q);
        }
        if (p->next == NULL)
        {
            free(p);
        }
    }
    return NULL;
}

// int main()
// {
//     return 0;
// }