#include <stdio.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"
#include "vm.h"

// Exists to tavoid the need to cast a void* to the desired type
#define ALLOCATE_OBJ(type, objectType) \
  (type*)allocateObject(sizeof(type), objectType)

// Generic allocation function, size is passed in to allow objects of multiple sizes
static Obj* allocateObject(size_t size, ObjType type) {
  Obj* object = (Obj*)reallocate(NULL, 0, size);
  object->type = type;

  // Add object to object list for garbage collection
  object->next = vm.objects;
  vm.objects = object;
  return object;
}

ObjClosure* newClosure(ObjFunction* function) {
  ObjUpvalue** upvalues = ALLOCATE(ObjUpvalue*,
      function->upvalueCount);
  for(int i = 0; i < function->upvalueCount; i++) {
    upvalues[i] = NULL;
  }

  ObjClosure* closure = ALLOCATE_OBJ(ObjClosure, OBJ_CLOSURE);
  closure->function = function;
  closure->upvalues = upvalues;
  closure->upvalueCount = function->upvalueCount;
  return closure;
}

ObjFunction* newFunction() {
  ObjFunction* function = ALLOCATE_OBJ(ObjFunction, OBJ_FUNCTION);
  function->arity = 0;
  function->upvalueCount = 0;
  function->name = NULL;
  initChunk(&function->chunk);
  return function;
}

ObjNative* newNative(NativeFn function) {
  // Wraps a C function pointer in an ObjNative lox object
  ObjNative* native = ALLOCATE_OBJ(ObjNative, OBJ_NATIVE);
  native->function = function;
  return native;
}

// Allocates a string object, pointing to the given characters
static ObjString* allocateString(char* chars, int length, uint32_t hash) {
  ObjString* string = ALLOCATE_OBJ(ObjString, OBJ_STRING);
  string->length = length;
  string->chars = chars;
  string->hash = hash;
  // Add string to the hash set of interned strings
  tableSet(&vm.strings, string, NIL_VAL);
  return string;
}

// String Hashing Function
// FNV-1a
static uint32_t hashString(const char* key, int length) {
  uint32_t hash = 2166136261u;
  for (int i = 0; i < length; i++) {
      hash ^= (uint8_t)key[i];
      hash *= 16777619;
  }
  return hash;
}

// We already have a character array on the heap.
// Coping again would be redundant and force concat to free its copy
// This takes ownership of the string.
ObjString* takeString(char* chars, int length) {
  uint32_t hash = hashString(chars, length);
  // Check if there is an already interned string
  ObjString* interned = tableFindString(&vm.strings, chars, length, hash);
  if (interned != NULL) {
    FREE_ARRAY(char, chars, length + 1);
    return interned;
  }

  return allocateString(chars, length, hash);
}

// This ensures that every ObjString reliables owns its character array by 
// copying the characters into the heap preemptively
ObjString* copyString(const char* chars, int length) {
  uint32_t hash = hashString(chars, length);
  // Check if there is an already interned Loxstring
  ObjString* interned = tableFindString(&vm.strings, chars, length, hash);
  if (interned != NULL) return interned;
  // Copies the characters in a string literal to the heap,
  // so that the resulting ObjString* owns its own characters
  // rather than pointing to the source code chars
  char* heapChars = ALLOCATE(char, length + 1);
  memcpy(heapChars, chars, length);
  // Add String terminator character to end of string
  heapChars[length] = '\0';
  return allocateString(heapChars, length, hash);
}

ObjUpvalue* newUpvalue(Value* slot) {
  ObjUpvalue* upvalue = ALLOCATE_OBJ(ObjUpvalue, OBJ_UPVALUE);
  upvalue->closed = NIL_VAL;
  upvalue->location = slot;
  upvalue->next = NULL;
  return upvalue;
}

static void printFunction(ObjFunction* function) {
  if (function->name == NULL) {
    printf("<script>");
    return;
  }
  printf("<fn %s>", function->name->chars);
}

void printObject(Value value) {
  switch (OBJ_TYPE(value)) {
    case OBJ_CLOSURE:
      printFunction(AS_CLOSURE(value)->function);
      break;
    case OBJ_NATIVE:
      printf("<native fn>");
      break;
    case OBJ_FUNCTION:
      printFunction(AS_FUNCTION(value));
      break;
    case OBJ_STRING:
      printf("%s", AS_CSTRING(value));
      break;
    case OBJ_UPVALUE:
      printf("upvalue");
      break;
  }
}
