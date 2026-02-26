cmake_minimum_required(VERSION 3.20)
project(thagc LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(LLVM REQUIRED CONFIG)
message(STATUS "Using LLVM ${LLVM_PACKAGE_VERSION}")

add_executable(thagc
  src/main.cpp
  src/application/builder.cpp
  src/driver/core.cpp
  src/driver/parser.cpp
  src/driver/help.cpp
  src/driver/build.cpp
  src/driver/run.cpp
  src/driver/stub.cpp
  src/infra/builder.cpp
  src/frontend/core.cpp
  src/frontend/ir.cpp
  src/frontend/builder.cpp
  src/middleend/builder.cpp
  src/backend/ir.cpp
  src/backend/builder.cpp
  src/driver/builder.cpp
  src/shared/core.cpp
  src/shared/io.cpp
  src/shared/process_runner.cpp
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
