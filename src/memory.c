#include <stdlib.h>

#include "memory.h"
#include "vm.h"

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

static void freeObject(Obj* object) {
  switch (object->type) {
    case OBJ_CLOSURE: {
      ObjClosure* closure = (ObjClosure*)object;
      // free upvalues
      FREE_ARRAY(ObjUpvalue*, closure->upvalues,
          closure->upvalueCount);

      FREE(ObjClosure, object);
      break;
    }
    case OBJ_NATIVE: {
      FREE(ObjNative, object);
      break;
    }
    case OBJ_FUNCTION: {
      ObjFunction* function = (ObjFunction*)object;
      freeChunk(&function->chunk);
      FREE(ObjFunction, object);
      break;
    }
    case OBJ_STRING: {
      ObjString* string = (ObjString*)object;
      // +1 is to free the terminator (not included in string length field)
      FREE_ARRAY(char, string->chars, string->length + 1);
      FREE(ObjString, object);
      break;
    }
    case OBJ_UPVALUE: {
      FREE(ObjUpvalue, object);
      break;
    }
  }
}

// Walk linked list and call free on every object til we reach null node
void freeObjects() {
  Obj* object = vm.objects;
  while (object != NULL) {
    Obj* next = object->next;
    freeObject(object);
    object = next;
  }
}
