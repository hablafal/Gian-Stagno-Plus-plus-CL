CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -I src -m64 -pthread
SRC = src/lexer.cpp src/ast.cpp \
      src/parser_base.cpp src/parser_type.cpp src/parser_expr.cpp src/parser_stmt.cpp src/parser_decl.cpp \
      src/semantic_base.cpp src/semantic_type.cpp src/semantic_expr.cpp src/semantic_stmt.cpp src/semantic_decl.cpp \
      src/optimizer.cpp \
      src/codegen_base.cpp src/codegen_expr.cpp src/codegen_stmt.cpp src/codegen_func.cpp \
      src/main.cpp
TARGET = gsc
RUNTIME_SRC = runtime/Strings.cpp runtime/Lists.cpp runtime/Dicts.cpp runtime/Tuples.cpp runtime/Sets.cpp runtime/IO.cpp runtime/Concurrency.cpp runtime/Channels.cpp runtime/Memory.cpp runtime/Exceptions.cpp
RUNTIME_OBJ = $(RUNTIME_SRC:.cpp=.o)
RUNTIME_LIB = libgspprun.a

ifeq ($(OS),Windows_NT)
	TARGET := gsc.exe
endif

all: $(TARGET) $(RUNTIME_LIB)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRC)

$(RUNTIME_LIB): $(RUNTIME_OBJ)
	ar rcs $@ $^

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -f $(TARGET) *.exe *.s *.o

.PHONY: all clean
