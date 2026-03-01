cmake_minimum_required(VERSION 3.20)
project(thagc LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(LLVM REQUIRED CONFIG)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
message(STATUS "Using LLVM ${LLVM_PACKAGE_VERSION}")

option(THAGC_STATIC_LLVM "Link LLVM component libraries statically into thagc" ON)
option(THAGC_STATIC_CXX_RUNTIME "Link libstdc++/libgcc statically when supported" ON)

add_executable(thagc
  src/main.cpp
  src/application/builder.cpp
  src/driver/core.cpp
  src/driver/parser.cpp
  src/driver/help.cpp
  src/driver/build.cpp
  src/driver/run.cpp
  src/driver/check.cpp
  src/driver/fmt.cpp
  src/driver/migrate.cpp
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

set(THAG_STDLIB_EMBED_TOOL "${CMAKE_SOURCE_DIR}/tooling/packaging/embed_stdlib_sources.py")
set(THAG_EMBEDDED_STDLIB_CPP "${CMAKE_CURRENT_BINARY_DIR}/generated/embedded_stdlib.cpp")
set(THAG_STDLIB_ROOT "${CMAKE_SOURCE_DIR}/stdlib")
set(THAG_STDLIB_MODULES
  std/core.tg
  std/string.tg
  std/list.tg
  lib/fs.tg
  lib/process.tg
  lib/toml.tg
  lib/http.tg
  lib/ws.tg
  lib/db.tg
  lib/time.tg
  lib/map.tg
  lib/json.tg
  lib/env.tg
  lib/crypto.tg
)
set(THAG_STDLIB_DEPENDS)
set(THAG_STDLIB_EMBED_ARGS)
foreach(THAG_STDLIB_MODULE IN LISTS THAG_STDLIB_MODULES)
  list(APPEND THAG_STDLIB_DEPENDS "${THAG_STDLIB_ROOT}/${THAG_STDLIB_MODULE}")
  list(APPEND THAG_STDLIB_EMBED_ARGS --module "${THAG_STDLIB_MODULE}")
endforeach()
add_custom_command(
  OUTPUT "${THAG_EMBEDDED_STDLIB_CPP}"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated"
  COMMAND "${Python3_EXECUTABLE}" "${THAG_STDLIB_EMBED_TOOL}"
          --root "${THAG_STDLIB_ROOT}"
          --output "${THAG_EMBEDDED_STDLIB_CPP}"
          ${THAG_STDLIB_EMBED_ARGS}
  DEPENDS "${THAG_STDLIB_EMBED_TOOL}" ${THAG_STDLIB_DEPENDS}
  VERBATIM
)
target_sources(thagc PRIVATE "${THAG_EMBEDDED_STDLIB_CPP}")

target_include_directories(thagc PRIVATE include)
target_include_directories(thagc PRIVATE ${LLVM_INCLUDE_DIRS})
target_compile_definitions(thagc PRIVATE ${LLVM_DEFINITIONS})

if (THAGC_STATIC_LLVM)
  message(STATUS "thagc LLVM linkage mode: static components")
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
    Passes
    coroutines
    CodeGen
    native
    nativecodegen
    AArch64
    ARM
    X86
  )
  if (NOT LLVM_LIBS)
    message(FATAL_ERROR "THAGC_STATIC_LLVM=ON but llvm_map_components_to_libnames returned empty library set")
  endif()
  target_link_libraries(thagc PRIVATE ${LLVM_LIBS})
elseif (TARGET LLVM)
  message(STATUS "thagc LLVM linkage mode: shared LLVM target")
  target_link_libraries(thagc PRIVATE LLVM)
else()
  message(STATUS "thagc LLVM linkage mode: fallback component map")
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
    Passes
    coroutines
    CodeGen
    native
    nativecodegen
    AArch64
    ARM
    X86
  )
  target_link_libraries(thagc PRIVATE ${LLVM_LIBS})
endif()

if(THAGC_STATIC_CXX_RUNTIME AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT APPLE AND NOT WIN32)
  target_link_options(thagc PRIVATE -static-libstdc++ -static-libgcc)
endif()

if(THAG_RUNTIME_USES_OPENSSL)
  target_compile_definitions(thagc PRIVATE THAG_RUNTIME_HAS_OPENSSL=1)
endif()
