#include <stdio.h>

#include "chunk.h"
#include "debug.h"
#include "value.h"

void disassembleChunk(Chunk *chunk, const char *name) {
  // Debug print the chunk being disassembled
  printf("== %s ==\n", name);

  for (int offset = 0; offset < chunk->count;) {
    // increment offset using disassembleInstruction as it returns offset of next instruction
    // Instructions can be of different sizes
    offset = disassembleInstruction(chunk, offset);
  }
}

static int simpleInstruction(const char* name, int offset) {
  // Print name of opcode
  printf("%s\n", name);
  // Returns the next byte in the code
  return offset + 1;
}

static int constantInstruction(const char* name, Chunk* chunk,
    int offset) {
  uint8_t constant = chunk->code[offset + 1];
  printf("%-16s %4d '", name, constant);
  printValue(chunk->constants.values[constant]);
  printf("'\n");
  // Offset of the next instruction is 2, one for the opcode and one for the operand (value)
  return offset + 2;
}

int disassembleInstruction(Chunk *chunk, int offset) {
  // Print byte offst of the given instruction
  printf("%04d ", offset);

  // Print source code line number after the offset of the instruction (bytes from beginning of the chunk)
  if (offset > 0 &&
      chunk->lines[offset] == chunk->lines[offset - 1]) {
    printf("   | ");
  } else {
    printf("%4d ", chunk->lines[offset]);
  }

  // Reads a single byte from the bytecode at the given offset.
  // This is our opcode
  // We dispatch to simpleInstruction to display it
  uint8_t instruction = chunk->code[offset];
  switch (instruction) {
    case OP_CONSTANT:
      return constantInstruction("OP_CONSTANT", chunk, offset);
    case OP_RETURN:
      return simpleInstruction("OP_RETURN", offset);
    default:
      // Handle unknown opcodes (result of bug in this compiler)
      printf("Unknown opcode %d\n", instruction);
      return offset + 1;
  }
}
