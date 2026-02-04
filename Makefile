CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -I src -m64
SRC = src/lexer.cpp src/ast.cpp src/parser.cpp src/semantic.cpp src/optimizer.cpp src/codegen.cpp src/main.cpp
TARGET = gsc

ifeq ($(OS),Windows_NT)
	TARGET := gsc.exe
endif

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET) *.exe *.s *.o

.PHONY: all clean
