cmake_minimum_required(VERSION 3.10)
project(gsc LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)

set(SOURCES
  src/lexer.cpp
  src/ast.cpp
  src/parser.cpp
  src/semantic.cpp
  src/optimizer.cpp
  src/codegen.cpp
  src/main.cpp
)

add_executable(gsc ${SOURCES})
target_include_directories(gsc PRIVATE ${CMAKE_SOURCE_DIR}/src)

if(MSVC)
  target_compile_options(gsc PRIVATE /W4)
else()
  target_compile_options(gsc PRIVATE -Wall -Wextra)
endif()

install(TARGETS gsc RUNTIME DESTINATION bin)
