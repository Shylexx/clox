#ifndef clox_chunk_h
#define clox_chunk_h

#include "common.h"

// In our bytecode, each instruction has a one-byte operation code
// Controls what kind of instruction we are dealing with
typedef enum {
  OP_RETURN,
} OpCode;

// dynamic array
// capacity is number of elements allocated
// count is amount of allocated entries in use
typedef struct {
  int count;
  int capacity;
  uint8_t* code;
} Chunk;

/* It works like an std vector, when the capacity is reached
 * we copy the elements to a new bigger array and then increment count and capacity */

void initChunk(Chunk* chunk);
void freeChunk(Chunk* chunk);
void writeChunk(Chunk* chunk, uint8_t byte);

#endif
