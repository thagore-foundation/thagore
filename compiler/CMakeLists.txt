cmake_minimum_required(VERSION 3.20)
project(thagc LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(LLVM REQUIRED CONFIG)
message(STATUS "Using LLVM ${LLVM_PACKAGE_VERSION}")

add_executable(thagc
  src/main.cpp
  src/application/build_usecase.cpp
  src/driver/command_router.cpp
  src/infra/adapters.cpp
  src/frontend/lexer.cpp
  src/frontend/parser.cpp
  src/frontend/typechecker.cpp
  src/middleend/core_ir_builder.cpp
  src/backend/llvm_context.cpp
  src/backend/object_emitter.cpp
  src/driver/pipeline.cpp
  src/shared/diag.cpp
  src/shared/filesystem.cpp
  src/shared/process.cpp
)

target_include_directories(thagc PRIVATE include)
target_include_directories(thagc PRIVATE ${LLVM_INCLUDE_DIRS})
target_compile_definitions(thagc PRIVATE ${LLVM_DEFINITIONS})

llvm_map_components_to_libnames(LLVM_LIBS
  Core
  Support
  Target
  TargetParser
  TargetMachine
  MC
  MCParser
  Object
  BitWriter
  AsmPrinter
  ScalarOpts
  CodeGen
)
target_link_libraries(thagc PRIVATE ${LLVM_LIBS})
