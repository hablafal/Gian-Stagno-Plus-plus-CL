# GS++ — Gian Stagno Plus Plus

A **modern systems-level language** that combines **Python’s simplicity and readability** with **C++’s performance and low-level power**. Fully **compiled** to native code (no interpreter).

- **File extension:** `.gs`
- **Compiler:** `gsc` (GS++ Compiler)

## Philosophy

- **Easy to read and write** like Python (`def`, `let`, `class`)
- **Fast and low-level** like C++ (compiled to x86/x64)
- **Minimal boilerplate** — type inference, clean syntax
- **Safe by default** — `unsafe` and inline asm when you need control

## Requirements

- **Windows 10/11** or **Linux** (64-bit or 32-bit)
- **C++17 compiler** (MinGW-w64, GCC, or MSVC) to build `gsc`
- **GCC** in PATH for assembling/linking (e.g. MinGW on Windows)

## Building the compiler

```bash
cd "MY CODING LANGUAGE"
g++ -std=c++17 -Wall -I src -o gsc.exe src/lexer.cpp src/ast.cpp src/parser.cpp src/semantic.cpp src/optimizer.cpp src/codegen.cpp src/main.cpp
```

Or use the **Makefile** (`make`) or **CMake** (e.g. `cmake -B build && cmake --build build`). The executable is **gsc** (or **gsc.exe** on Windows).

## Usage

```bash
gsc main.gs -o app.exe      # compile and link
gsc main.gs -o app          # Linux
gsc main.gs -S              # emit assembly only
gsc main.gs -g              # debug mode
gsc main.gs -O              # release (optimize)
gsc main.gs -m64            # 64-bit (requires 64-bit MinGW/GCC)
```

## Quick example

**hello.gs:**

```gs
def main() -> int {
    let x = 42;
    println(x);
    return 0;
}
```

```bash
gsc hello.gs -o hello.exe
hello.exe
```

Output: `42`

## Language overview

| Feature | Syntax |
|---------|--------|
| Variables | `var x: int = 5;` or `let x = 5;` |
| Functions | `def name(a: int, b: int) -> int { ... }` or `func` |
| Structs/Classes | `struct Point { x: int; y: int; }` or `class Point { ... }` |
| Control flow | `if (c) { } else { }`, `while (c) { }`, `for (init; c; step) { }` |
| Builtins | `print`, `println`, `print_float`, `println_float` |
| Modules | `import "io";` or `import math;` (stdlib in progress) |

See **[docs/GSPP_SPEC.md](docs/GSPP_SPEC.md)** for the full language specification.

## Project layout

```
src/          — Compiler: lexer, parser, AST, semantic, optimizer, codegen
examples/     — Sample .gs programs
docs/         — GS++ spec, examples (vs C++/Python), migration guide
```

## Documentation

- **[docs/GSPP_SPEC.md](docs/GSPP_SPEC.md)** — Full language specification
- **[docs/EXAMPLES_GSPP_VS_CPP_PYTHON.md](docs/EXAMPLES_GSPP_VS_CPP_PYTHON.md)** — GS++ vs C++ vs Python
- **[docs/MIGRATION_GUIDE.md](docs/MIGRATION_GUIDE.md)** — Migrating from MyLang, C++, or Python

## Cross-platform

- **Windows:** 32-bit by default (works with MinGW32); use `-m64` for 64-bit.
- **Linux:** Use `gsc main.gs -o app` (no `.exe`); 32/64-bit via `-m32`/`-m64` as needed.

GS++ (Gian Stagno Plus Plus) — *Python simplicity. C++ power. One compiler.*
