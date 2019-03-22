#ifndef VECTOR_H
#define VECTOR_H

#include "tcg-plugin.h"

typedef struct memory_range {
    int address;
    int size;
    char perms;
    // bool file_backed;
    int file_backed;
    // struct memory_range* next;
} memory_range;

typedef union vector_item {
    memory_range mr;
    int callframes;
    target_ulong freed_obj;
} vector_item;

typedef struct vector
{
    vector_item item;
    struct vector *next;
} vector;

void PrintVector(vector *head);

vector *CreateNodeVector(const vector_item data);

vector *PushBackVector(const vector_item data, vector *head);

void SwapVectorNode(vector *p1, vector *p2);

vector *GetVectorEnd(vector *head);

int CmpVectorNode(const vector p1, const vector p2);

void QuickSortVector(vector *head, vector *end);

void BubbleSortVector(vector *head);

vector *DestoryVector(vector *head);

vector *PopBackVector(vector *head);

size_t GetVectorSize(vector *head);

vector* ReverseVector(vector *head);

// int IsItemEquals(const vector_item vi1, const vector_item vi2);

// int IsItemInVector(const vector_item data, vector *head);

#endif // !VECTOR_H

