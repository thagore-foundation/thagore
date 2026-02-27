cmake_minimum_required(VERSION 3.20)
project(thagc LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(LLVM REQUIRED CONFIG)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
message(STATUS "Using LLVM ${LLVM_PACKAGE_VERSION}")

add_executable(thagc
  src/main.cpp
  src/application/builder.cpp
  src/driver/core.cpp
  src/driver/parser.cpp
  src/driver/help.cpp
  src/driver/build.cpp
  src/driver/run.cpp
  src/driver/fix.cpp
  src/driver/intent.cpp
  src/driver/state.cpp
  src/driver/install.cpp
  src/driver/target.cpp
  src/driver/update.cpp
  src/driver/flow.cpp
  src/driver/common.cpp
  src/driver/resolver.cpp
  src/driver/unknown.cpp
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

set(THAG_EMBED_TOOL "${CMAKE_SOURCE_DIR}/tooling/packaging/embed_runtime_archive.py")
set(THAG_EMBEDDED_RUNTIME_CPP "${CMAKE_CURRENT_BINARY_DIR}/generated/embedded_runtime.cpp")
add_custom_command(
  OUTPUT "${THAG_EMBEDDED_RUNTIME_CPP}"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated"
  COMMAND "${Python3_EXECUTABLE}" "${THAG_EMBED_TOOL}"
          --input "$<TARGET_FILE:thag_runtime>"
          --output "${THAG_EMBEDDED_RUNTIME_CPP}"
  DEPENDS thag_runtime "${THAG_EMBED_TOOL}" "$<TARGET_FILE:thag_runtime>"
  VERBATIM
)
target_sources(thagc PRIVATE "${THAG_EMBEDDED_RUNTIME_CPP}")
add_dependencies(thagc thag_runtime)

target_include_directories(thagc PRIVATE include)
target_include_directories(thagc PRIVATE ${LLVM_INCLUDE_DIRS})
target_compile_definitions(thagc PRIVATE ${LLVM_DEFINITIONS})

if (TARGET LLVM)
  target_link_libraries(thagc PRIVATE LLVM)
else()
  llvm_map_components_to_libnames(LLVM_LIBS
    Core
    Support
    Target
    TargetParser
    MC
    MCParser
    Object
    BitWriter
    AsmPrinter
    ScalarOpts
    coroutines
    CodeGen
    native
    nativecodegen
  )
  target_link_libraries(thagc PRIVATE ${LLVM_LIBS})
endif()

if(THAG_RUNTIME_USES_OPENSSL)
  target_compile_definitions(thagc PRIVATE THAG_RUNTIME_HAS_OPENSSL=1)
endif()
