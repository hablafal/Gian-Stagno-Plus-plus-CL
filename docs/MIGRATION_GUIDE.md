# Migration Guide: MyLang / C++ / Python → GS++

## From MyLang (Previous Custom Language)

| MyLang | GS++ |
|--------|------|
| File `.my` | File `.gs` |
| `mycc source.my -o app` | `gsc source.gs -o app` |
| `func` | `func` or `def` (same) |
| `struct` only | `struct` or `class` (same) |
| No `let` | `let x = value` (inferred type) |
| No `import` | `import "io";` or `import mod;` |

**Steps:**

1. Rename `.my` → `.gs`.
2. Replace `mycc` with `gsc` in build scripts.
3. Optionally use `def` instead of `func`, `let` instead of `var` when type is obvious, and `class` instead of `struct` for clarity.

---

## From C++

| C++ | GS++ |
|-----|------|
| `int main() { }` | `def main() -> int { }` |
| `int x = 5;` | `var x: int = 5;` or `let x = 5;` |
| `auto x = 5;` | `let x = 5;` |
| `std::cout << x << std::endl;` | `println(x);` |
| `struct S { int a; };` | `struct S { a: int; }` or `class S { a: int; }` |
| `#include <iostream>` | Built-in `println`; use `import "io";` when available |

**Philosophy:** GS++ drops headers, `std::` namespace, and preprocessor; adds `def`/`let` and cleaner declarations.

---

## From Python

| Python | GS++ |
|--------|------|
| `def f():` | `def f() -> int { }` (return type required for non-void) |
| `x = 10` | `let x = 10;` (semicolon required) |
| `print(x)` | `println(x);` |
| `if x > 0:` (indent) | `if (x > 0) { }` (braces) |
| `class C:` | `class C { }` (members with types and `;`) |
| Interpreted | **Compiled** — run `gsc file.gs -o app` then `./app` |

**Philosophy:** GS++ keeps `def`/`let`/`class` and readability but is statically typed and compiled; use braces and semicolons.

---

## Build and Run

```bash
# Compile and link (default 32-bit on Windows)
gsc main.gs -o app.exe

# Debug symbols
gsc main.gs -o app.exe -g

# Release
gsc main.gs -o app.exe -O

# Assembly only
gsc main.gs -S
```

Requires **GCC** (e.g. MinGW on Windows) in `PATH` for linking.
