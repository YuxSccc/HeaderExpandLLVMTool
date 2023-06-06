cmake_minimum_required(VERSION 3.25)
project(Header_Expand_Tools)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_VERBOSE_MAKEFILE ON)

find_package(Clang REQUIRED)
find_package(LLVM REQUIRED CONFIG)

set(CMAKE_MODULE_PATH
        ${CMAKE_MODULE_PATH}
        "${LLVM_CMAKE_DIR}"
        "${CLANG_CMAKE_DIR}"
)

include(AddLLVM)

add_llvm_executable(Header_Expand_Tools main.cpp)
target_link_libraries(Header_Expand_Tools PRIVATE clangTooling)