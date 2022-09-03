#include <stdio.h>

#include "chunk.h"
#include "common.h"
#include "debug.h"
#include "vm.h"

VM vm;

void initVM() {
}

void freeVM() {
}

static InterpretResult run() {
// Reads the byte pointed to by ip and then advances it
#define READ_BYTE() (*vm.ip++)
// Reads the next byte of bytecode, and looks up the Value in the chunks constant table
#define READ_CONSTANT() (vm.chunk->constants.values[READ_BYTE()])

  // Each iteration of this loop reads and executes 1 bytecode instruction
  for (;;) {
// Print instructions as they are executed
#ifdef DEBUG_TRACE_EXECUTION
    disassembleInstruction(vm.chunk, 
                            (int)(vm.ip - vm.chunk->code));
#endif
    uint8_t instruction;
    switch (instruction = READ_BYTE()) {
      case OP_CONSTANT: {
        Value constant = READ_CONSTANT();
        printValue(constant);
        printf("\n");
        break;
      }
      case OP_RETURN: {
        return INTERPRET_OK;
      }
    }
  }

// Undefine our macros like good little boys
#undef READ_BYTE
#undef READ_CONSTANT
}

InterpretResult interpret(Chunk *chunk) {
  vm.chunk = chunk;
  vm.ip = vm.chunk->code;
  return run();
}
