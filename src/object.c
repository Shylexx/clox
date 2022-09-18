#include <stdio.h>
#include <string.h>

#include "memory.h"
#include "object.h"
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

// Allocates a string object, pointing to the given characters
static ObjString* allocateString(char* chars, int length, uint32_t hash) {
  ObjString* string = ALLOCATE_OBJ(ObjString, OBJ_STRING);
  string->length = length;
  string->chars = chars;
  string->hash = hash;
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
  return allocateString(chars, length, hash);
}

// This ensures that every ObjString reliables owns its character array by 
// copying the characters into the heap preemptively
ObjString* copyString(const char* chars, int length) {
  uint32_t hash = hashString(chars, length);
  // Copies the characters in a string literal to the heap,
  // so that the resulting ObjString* owns its own characters
  // rather than pointing to the source code chars
  char* heapChars = ALLOCATE(char, length + 1);
  memcpy(heapChars, chars, length);
  // Add String terminator character to end of string
  heapChars[length] = '\0';
  return allocateString(heapChars, length, hash);
}

void printObject(Value value) {
  switch (OBJ_TYPE(value)) {
    case OBJ_STRING:
      printf("%s", AS_CSTRING(value));
      break;
  }
}
