# GS++ vs C++ vs Python — Side-by-Side Examples

## Hello World

| GS++ | C++ | Python |
|------|-----|--------|
| `def main() -> int {` | `int main() {` | `def main():` |
| `  let x = 42;` | `  int x = 42;` | `  x = 42` |
| `  println(x);` | `  std::cout << x << std::endl;` | `  print(x)` |
| `  return 0;` | `  return 0;` | `  return 0` |
| `}` | `}` | |

**GS++:** Compiled, minimal syntax, `def` + `let` like Python, semicolons like C++.

---

## Variables and Types

| GS++ | C++ | Python |
|------|-----|--------|
| `let a = 10;` | `auto a = 10;` | `a = 10` |
| `var b: int = 20;` | `int b = 20;` | `b: int = 20` |
| `let f = 3.14;` | `auto f = 3.14;` | `f = 3.14` |

**GS++:** Type inference with `let`; explicit types with `var name: Type = value`.

---

## Functions

| GS++ | C++ | Python |
|------|-----|--------|
| `def add(a: int, b: int) -> int {` | `int add(int a, int b) {` | `def add(a: int, b: int) -> int:` |
| `  return a + b;` | `  return a + b;` | `  return a + b` |
| `}` | `}` | |

**GS++:** `def`/`func` with arrow return type; no header/body split like C++.

---

## Structs / Classes

| GS++ | C++ | Python |
|------|-----|--------|
| `class Point {` | `struct Point {` | `class Point:` |
| `  x: int;` | `  int x;` | `  def __init__(self, x, y):` |
| `  y: int;` | `  int y;` | `    self.x = x` |
| `}` | `};` | `    self.y = y` |

**GS++:** `class` and `struct` are equivalent value types; no `;` after `}`.

---

## Control Flow

| GS++ | C++ | Python |
|------|-----|--------|
| `if (x > 0) {` | `if (x > 0) {` | `if x > 0:` |
| `  println(x);` | `  std::cout << x;` | `  print(x)` |
| `} else {` | `} else {` | `else:` |
| `  println(0);` | `  std::cout << 0;` | `  print(0)` |
| `}` | `}` | |
| `while (i < n) { ... }` | `while (i < n) { ... }` | `while i < n: ...` |
| `for (i=0; i<n; i=i+1) { }` | `for (int i=0; i<n; i++) { }` | `for i in range(n):` |

**GS++:** Braces like C++, conditions in parentheses; future: `for x in range` style.

---

## Summary

- **GS++** aims for Python-like readability (`def`, `let`, `class`) with C++-like performance and explicit types when needed.
- **Compiled** like C++, **no interpreter** like Python.
- **Minimal boilerplate:** no `#include`, no `std::`, optional type inference.
