cmake_minimum_required(VERSION 3.23)
project(clox VERSION 0.0.1 LANGUAGES C)

set(HEADER_FILES
  src/common.h
  src/chunk.h
  src/memory.h
  src/debug.h
  src/value.h
  src/vm.h
  src/compiler.h
  src/scanner.h
  src/object.h
)

set(SOURCE_FILES
  src/main.c
  src/chunk.c
  src/memory.c
  src/debug.c
  src/value.c
  src/vm.c
  src/compiler.c
  src/scanner.c
  src/object.c
)


add_executable(${PROJECT_NAME}
  ${SOURCE_FILES}
  ${HEADER_FILES}
)
