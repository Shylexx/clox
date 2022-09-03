#include <stdio.h>

#include "chunk.h"
#include "common.h"
#include "debug.h"
#include "value.h"
#include "vm.h"

VM vm;

static void resetStack() {
  vm.stackTop = vm.stack;
}

void initVM() {
  resetStack();
}

void freeVM() {
}

void push(Value value) {
  // Stores value in the array element at the top of the stack
  // (Points at the next empty element)
  *vm.stackTop = value;
  // Increment the pointer to the new next empty element
  vm.stackTop++;
}

Value pop() {
  // Move back to the last element
  vm.stackTop--;
  // Return the last element (now pointing at next slot)
  // Dont need to free anything ahead as the array is already
  // full in use, stackTop just finds the next place we can write to
  return *vm.stackTop;
}

static InterpretResult run() {
// Reads the byte pointed to by ip and then advances it
#define READ_BYTE() (*vm.ip++)
// Reads the next byte of bytecode, and looks up the Value in the chunks constant table
#define READ_CONSTANT() (vm.chunk->constants.values[READ_BYTE()])
// Binary Operators (main 4 binary ops are same but different C operator)
// Do while loop in a macro is a C trick to attach block to a single scope
// Takes the top two values (the operands) by popping
// Performs the operation on the two values and pushes it back on
#define BINARY_OP(op) \
  do { \
    double b = pop(); \
    double a = pop(); \
    push(a op b); \
  } while (false)

  // Each iteration of this loop reads and executes 1 bytecode instruction
  for (;;) {
#ifdef DEBUG_TRACE_EXECUTION
// Print stack before each instruction (stack trace)
    printf("          ");
    for (Value* slot = vm.stack; slot < vm.stackTop; slot++) {
      printf("[ ");
      printValue(*slot);
      printf(" ]");
    }
    printf("\n");
// Print instructions as they are executed
    disassembleInstruction(vm.chunk, 
                            (int)(vm.ip - vm.chunk->code));
#endif
    uint8_t instruction;
    switch (instruction = READ_BYTE()) {
      case OP_CONSTANT: {
        Value constant = READ_CONSTANT();
        push(constant);
        break;
      }
      case OP_ADD:      BINARY_OP(+); break;
      case OP_SUBTRACT: BINARY_OP(-); break;
      case OP_MULTIPLY: BINARY_OP(*); break;
      case OP_DIVIDE:   BINARY_OP(/); break;
      case OP_NEGATE: push(-pop()); break;
      case OP_RETURN: {
        printValue(pop());
        printf("\n");
        return INTERPRET_OK;
      }
    }
  }

// Undefine our macros like good little boys
#undef READ_BYTE
#undef READ_CONSTANT
#undef BINARY_OP
}

InterpretResult interpret(Chunk *chunk) {
  vm.chunk = chunk;
  vm.ip = vm.chunk->code;
  return run();
}
