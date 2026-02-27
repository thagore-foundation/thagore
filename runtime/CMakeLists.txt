cmake_minimum_required(VERSION 3.20)

add_library(thag_runtime STATIC
  src/thag_runtime.cpp
  src/print.cpp
  src/string.cpp
  src/io_stack.cpp
  src/concurrency.cpp
  src/platform_posix.cpp
  src/platform_windows.cpp
)

target_include_directories(thag_runtime PUBLIC include)
target_compile_features(thag_runtime PUBLIC cxx_std_20)

if(NOT WIN32)
  find_package(Threads REQUIRED)
  target_link_libraries(thag_runtime PUBLIC Threads::Threads)
endif()

find_package(CURL QUIET)
if(CURL_FOUND)
  target_compile_definitions(thag_runtime PUBLIC THAG_RUNTIME_HAS_CURL=1)
  target_link_libraries(thag_runtime PUBLIC CURL::libcurl)
endif()

find_package(SQLite3 QUIET)
if(SQLite3_FOUND)
  target_compile_definitions(thag_runtime PUBLIC THAG_RUNTIME_HAS_SQLITE3=1)
  target_link_libraries(thag_runtime PUBLIC SQLite::SQLite3)
endif()
