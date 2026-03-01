cmake_minimum_required(VERSION 3.20)

add_library(thag_runtime STATIC
  src/thag_runtime.cpp
  src/print.cpp
  src/string.cpp
  src/json.cpp
  src/tensor.cpp
  src/crypto.cpp
  src/fs.cpp
  src/process.cpp
  src/toml.cpp
  src/io_stack.cpp
  src/http.cpp
  src/ws.cpp
  src/db.cpp
  src/event_loop.cpp
  src/concurrency.cpp
  src/platform_posix.cpp
  src/platform_windows.cpp
)

target_include_directories(thag_runtime PUBLIC include)
target_compile_features(thag_runtime PUBLIC cxx_std_20)

option(THAG_RUNTIME_USE_SYSTEM_OPENSSL "Enable OpenSSL-backed HTTPS/crypto support in runtime" ON)

if(NOT WIN32)
  find_package(Threads REQUIRED)
  target_link_libraries(thag_runtime PUBLIC Threads::Threads)
  if(NOT APPLE)
    target_link_libraries(thag_runtime PUBLIC dl)
  endif()
endif()

# Optional system OpenSSL for HTTPS support in self-implemented HTTP client.
set(THAG_RUNTIME_USES_OPENSSL OFF PARENT_SCOPE)
if(THAG_RUNTIME_USE_SYSTEM_OPENSSL)
  find_package(OpenSSL QUIET)
  if(OpenSSL_FOUND)
    set(THAG_RUNTIME_USES_OPENSSL ON PARENT_SCOPE)
    target_compile_definitions(thag_runtime PUBLIC THAG_RUNTIME_HAS_OPENSSL=1)
    target_link_libraries(thag_runtime PUBLIC OpenSSL::SSL OpenSSL::Crypto)
  endif()
endif()

# SQLite is allowed as self-contained dependency (amalgamation) or system library.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vendor/sqlite3/sqlite3.c")
  target_sources(thag_runtime PRIVATE vendor/sqlite3/sqlite3.c)
  target_include_directories(thag_runtime PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/vendor/sqlite3")
  target_compile_definitions(thag_runtime PUBLIC THAG_RUNTIME_HAS_SQLITE3=1 THAG_RUNTIME_HAS_SQLITE3_AMALGAMATION=1)
else()
  find_path(THAG_SQLITE3_INCLUDE_DIR sqlite3.h)
  find_library(THAG_SQLITE3_LIBRARY sqlite3)
  if(THAG_SQLITE3_INCLUDE_DIR AND THAG_SQLITE3_LIBRARY)
    target_include_directories(thag_runtime PUBLIC "${THAG_SQLITE3_INCLUDE_DIR}")
    target_link_libraries(thag_runtime PUBLIC "${THAG_SQLITE3_LIBRARY}")
    target_compile_definitions(thag_runtime PUBLIC THAG_RUNTIME_HAS_SQLITE3=1 THAG_RUNTIME_HAS_SQLITE3_SYSTEM=1)
  endif()
endif()

if(WIN32)
  target_link_libraries(thag_runtime PUBLIC ws2_32)
endif()
