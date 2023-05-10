#ifndef clox_chunk_h
#define clox_chunk_h

#include "common.h"
#include "value.h"

// In our bytecode, each instruction has a one-byte operation code
// Controls what kind of instruction we are dealing with
typedef enum {
  OP_RETURN,
  // Binary Operators
  OP_ADD,
  OP_SUBTRACT,
  OP_MULTIPLY,
  OP_DIVIDE,
  OP_EQUAL,
  OP_POP,
  OP_DEFINE_GLOBAL,
  OP_GET_GLOBAL,
  OP_SET_GLOBAL,
  OP_GET_UPVALUE,
  OP_SET_UPVALUE,
  OP_CLOSE_UPVALUE,
  OP_GET_LOCAL,
  OP_SET_LOCAL,
  OP_JUMP_IF_FALSE,
  OP_JUMP,
  OP_LOOP,
  OP_CALL,
  OP_CLOSURE,
  OP_GREATER,
  OP_LESS,
  OP_NOT,
  OP_NEGATE,
  OP_CONSTANT,
  OP_NIL,
  OP_TRUE,
  OP_FALSE,
  OP_PRINT,
} OpCode;

// dynamic array
// capacity is number of elements allocated
// count is amount of allocated entries in use
typedef struct {
  int count;
  int capacity;
  uint8_t* code;
  int* lines;
  ValueArray constants;
} Chunk;

/* It works like an std vector, when the capacity is reached
 * we copy the elements to a new bigger array and then increment count and capacity */

void initChunk(Chunk* chunk);
void freeChunk(Chunk* chunk);
void writeChunk(Chunk* chunk, uint8_t byte, int line);
int addConstant(Chunk* chunk, Value value);

#endif
