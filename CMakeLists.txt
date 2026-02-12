cmake_minimum_required(VERSION 3.28)

project(thagore_bootstrap
  VERSION 0.1.0
  LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(LLVM REQUIRED CONFIG)
find_package(Python3 COMPONENTS Development REQUIRED)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

add_library(thag_common STATIC
  src/common/diagnostics.cpp
)
target_include_directories(thag_common PUBLIC include)
target_compile_features(thag_common PUBLIC cxx_std_23)

add_library(thag_frontend STATIC
  src/frontend/lexer.cpp
  src/frontend/parser.cpp
  src/frontend/semantic.cpp
)
target_include_directories(thag_frontend PUBLIC include)
target_compile_features(thag_frontend PUBLIC cxx_std_23)
target_link_libraries(thag_frontend PUBLIC thag_common)

add_library(thag_backend STATIC
  src/backend/ir_generator.cpp
)
target_include_directories(thag_backend PUBLIC include)
target_compile_features(thag_backend PUBLIC cxx_std_23)
target_link_libraries(thag_backend
  PUBLIC
    thag_common
    thag_frontend
)

llvm_map_components_to_libnames(THAG_LLVM_LIBS
  Core
  Support
  Target
  MC
  CodeGen
  Passes
  ScalarOpts
  InstCombine
  TransformUtils
  Analysis
  X86
  X86CodeGen
  X86Desc
  X86Info
  X86AsmParser
)
target_link_libraries(thag_backend PUBLIC ${THAG_LLVM_LIBS})
target_include_directories(thag_backend SYSTEM PUBLIC ${LLVM_INCLUDE_DIRS})
target_compile_definitions(thag_backend PUBLIC ${LLVM_DEFINITIONS})

add_library(thag_runtime STATIC
  src/runtime/runtime.cpp
  src/runtime/py_bridge.cpp
)
target_include_directories(thag_runtime PUBLIC include ${Python3_INCLUDE_DIRS})
target_compile_features(thag_runtime PUBLIC cxx_std_23)
target_compile_definitions(thag_runtime PUBLIC Py_NO_LINK_LIB)
if(Python3_RUNTIME_LIBRARY_RELEASE)
  target_compile_definitions(thag_runtime PUBLIC THAG_PYTHON_DLL_PATH="${Python3_RUNTIME_LIBRARY_RELEASE}")
endif()

add_library(thag_driver STATIC
  src/driver/driver.cpp
)
target_include_directories(thag_driver PUBLIC include)
target_compile_features(thag_driver PUBLIC cxx_std_23)
target_link_libraries(thag_driver
  PUBLIC
    thag_common
    thag_frontend
    thag_backend
    thag_runtime
)

add_executable(thag
  src/main.cpp
)
target_compile_features(thag PRIVATE cxx_std_23)
target_link_libraries(thag PRIVATE thag_driver)

include(CTest)
if(BUILD_TESTING)
  add_executable(thag_tests
    tests/test_main.cpp
  )
  target_compile_features(thag_tests PRIVATE cxx_std_23)
  target_include_directories(thag_tests PRIVATE include)
  target_compile_definitions(thag_tests PRIVATE THAG_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
  target_link_libraries(thag_tests
    PRIVATE
      thag_common
      thag_frontend
      thag_backend
      thag_runtime
      thag_driver
  )
  add_test(NAME thag_tests COMMAND thag_tests)
endif()
