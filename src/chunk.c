#include <stdlib.h>

#include "chunk.h"
#include "memory.h"

void initChunk(Chunk *chunk) {
  chunk->count = 0;
  chunk->capacity = 0;
  chunk->code = NULL;
}

void freeChunk(Chunk *chunk) {
  // Deallocate all memory and then zero out the fields to leave it empty
  FREE_ARRAY(uint8_t, chunk->code, chunk->capacity);
  initChunk(chunk);
}

void writeChunk(Chunk *chunk, uint8_t byte) {
  // If writing to chunk will exceed the max capacity, reallocate array
  if (chunk->capacity < chunk->count + 1) {
    int oldCapacity = chunk->capacity;
    chunk->capacity = GROW_CAPACITY(oldCapacity);
    chunk->code = GROW_ARRAY(uint8_t, chunk->code,
        oldCapacity, chunk->capacity);
  }

  // Write to the next available point in the array
  // Also update the count to correctly track size
  chunk->code[chunk->count] = byte;
  chunk->count++;
}
