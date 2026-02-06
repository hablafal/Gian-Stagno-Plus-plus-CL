# GS++ — Gian Stagno Plus Plus

A **modern systems-level language** that combines **Python’s simplicity and readability** with **C++’s performance and low-level power**. Fully **compiled** to native code (no interpreter).

- **File extension:** `.gs`
- **Compiler:** `gsc` (GS++ Compiler)

## Latest Features

- **Modular Architecture**: Compiler is split into specialized components for Parser, Semantic Analyzer, and Code Generator.
- **Interactive REPL**: Test GS++ code instantly in the terminal by running `gsc --repl`.
- **Advanced Generics**: Full support for user-defined generic structs and functions using C++-style monomorphization (e.g., `struct Box[T]: ...`).
- **Pythonic OOP**: Full support for `struct` and `class` with inheritance (`class Derived(Base):`), constructors (`def init(self, ...):`), and the `super` keyword.
- **Advanced Concurrency**:
    - `spawn`: Run functions in native threads.
    - `chan[T]`: Go-style channels for safe communication.
    - `mutex` & `lock`: Built-in synchronization primitives.
- **Improved Syntax**:
    - Indentation-based blocks (optional, braces still supported).
    - `ptr[T]` or `ptr T` for explicit pointers (plus `*T` support).
    - Triple-quoted strings (`"""long string"""`).
- **Robust Runtime**: Comprehensive support for Strings, Lists, Dicts, Sets, and Tuples.

## Requirements

- **Windows 10/11** or **Linux** (64-bit recommended)
- **C++17 compiler** (GCC or MinGW-w64)
- **pthread** support for concurrency

## Building the compiler

Simply run `make` in the root directory:

```bash
make
```

The executable **gsc** will be created.

## Usage

```bash
gsc main.gs -o app      # compile and link
gsc main.gs -S          # emit assembly only
gsc main.gs -g          # debug mode
gsc main.gs -O          # release mode (optimize)
gsc --repl              # start interactive REPL
```

## Quick example: Concurrency & OOP

```gs
struct Greeter:
    name: string
    def init(self, n: string):
        self.name = n
    def greet(self):
        println("Hello, " + self.name)

def worker(c: chan[int]):
    let val = <- c
    println(val)

fn main():
    let g = new Greeter("GS++")
    g.greet()

    let c = chan[int](1)
    spawn worker(c)
    c <- 1337
```

## Documentation

- **[LANGUAGE_SPEC.md](LANGUAGE_SPEC.md)** — Short language reference
- **[docs/GSPP_SPEC.md](docs/GSPP_SPEC.md)** — Full specification

GS++ (Gian Stagno Plus Plus) — *Python simplicity. C++ power. One compiler.*
