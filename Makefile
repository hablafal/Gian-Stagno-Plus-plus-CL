CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -I src -m64
SRC = src/lexer.cpp src/ast.cpp src/parser.cpp src/semantic.cpp src/optimizer.cpp src/codegen.cpp src/main.cpp
TARGET = gsc
RUNTIME_SRC = runtime/Strings.cpp runtime/Lists.cpp runtime/Dicts.cpp runtime/Tuples.cpp runtime/Sets.cpp runtime/IO.cpp
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
