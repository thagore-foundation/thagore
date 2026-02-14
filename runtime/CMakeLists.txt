cmake_minimum_required(VERSION 3.16)
project(thagore_runtime LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(thag_runtime_obj OBJECT src/runtime.cpp)
set_target_properties(thag_runtime_obj PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(thag_runtime_static STATIC $<TARGET_OBJECTS:thag_runtime_obj>)
set_target_properties(thag_runtime_static PROPERTIES OUTPUT_NAME thag_runtime)

