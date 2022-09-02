#include <stdlib.h>

#include "memory.h"

void* reallocate(void* pointer, size_t oldSize, size_t newSize) {
  // Deallocate specifically if newsize is 0
  if (newSize == 0) {
    free(pointer);
    return NULL;
  }

  // Will allocate if no oldsize, if exceeds size of the block it is in will reallocate elesewhere
  // and return a pointer to the new block
  void* result = realloc(pointer, newSize);

  // If allocation fails exit the VM
  if (result == NULL) exit(1);

  return result;
}
