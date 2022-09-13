#ifndef clox_memory_h
#define clox_memory_h

#include "common.h"

// Allocates an array with given type and count
#define ALLOCATE(type, count) \
  (type*)reallocate(NULL, 0, sizeof(type) * (count))

// Calculates new array capacity
// Doubles current capacity or sets it to 8 if it is 0
#define GROW_CAPACITY(capacity) \
  ((capacity) < 8 ? 8 : (capacity) * 2)

// Gets the size of the array element type and casts the void* to the correct type after calling reallocate
#define GROW_ARRAY(type, pointer, oldCount, newCount) \
  (type*)reallocate(pointer, sizeof(type) * (oldCount), \
      sizeof(type) * (newCount))

// Frees memory by passing in 0 for new size in reallocate call
#define FREE_ARRAY(type, pointer, oldCount) \
  reallocate(pointer, sizeof(type) * (oldCount), 0)

void* reallocate(void* pointer, size_t oldSize, size_t newSize);

#endif
