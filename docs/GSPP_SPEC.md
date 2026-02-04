# GS++ Language Specification  
## Gian Stagno Plus Plus — Modern Systems Language

**Version:** 1.0  
**File extension:** `.gs`  
**Compiler:** `gsc` (GS++ Compiler)

---

## 1. Philosophy

- **Python-style readability** — Clean blocks, minimal punctuation, `def`/`let`/`class`
- **C++-level performance** — Compiled to native code (x86/x64), no interpreter overhead
- **Safe by default** — Static typing, explicit `unsafe` for low-level code
- **Minimal boilerplate** — Type inference where obvious, simple function/class syntax

---

## 2. Lexical Elements

| Category | Examples |
|----------|----------|
| **Comments** | `//` line, `/* */` block |
| **Identifiers** | `letter` or `_`, then `letter`, `digit`, `_` |
| **Literals** | Integers `42`, floats `3.14`, booleans `true`/`false`, strings `"hello"` |
| **Keywords** | `var`, `let`, `func`, `def`, `class`, `struct`, `if`, `else`, `while`, `for`, `in`, `return`, `int`, `float`, `bool`, `and`, `or`, `not`, `import`, `asm`, `unsafe` |

---

## 3. Types

- **Primitives:** `int`, `float`, `bool`
- **User types:** `struct Name { ... }` or `class Name { ... }` (equivalent; both value types)
- **Type inference:** `let x = 42` infers `int`; `var x: int = 42` is explicit.

---

## 4. Declarations

```gs
var name: Type = value;     // explicit type
let name = value;           // inferred type (preferred when obvious)
func name(a: int, b: int) -> int { ... }
def name(a: int, b: int) -> int { ... }   // same as func
struct Point { x: int; y: int; }
class Point { x: int; y: int; }           // same as struct
import "io";
import math;
```

---

## 5. Statements

- **Blocks:** `{ stmt; stmt; ... }`
- **Conditionals:** `if (cond) { } else { }`
- **Loops:** `while (cond) { }`, `for (init; cond; step) { }`
- **Return:** `return expr;` or `return;`
- **Expression statement:** `expr;`

---

## 6. Expressions

- **Binary:** `+ - * / %`, `== != < > <= >=`, `and`, `or`
- **Unary:** `-`, `not`
- **Call:** `name(args)`
- **Member:** `obj.member`
- **Literals:** integer, float, boolean, (string in lexer; full support in progress)

---

## 7. Standard Library (Built-in)

| Area | Functions |
|------|-----------|
| **I/O** | `print(int)`, `println(int)`, `print_float(float)`, `println_float(float)` |
| **Math** | (planned) `math::sqrt`, `math::sin`, etc. |
| **Strings** | (planned) `string::len`, `string::concat` |
| **Files** | (planned) `io::read_file`, `io::write_file` |
| **OS** | (planned) `os::env`, `os::args` |
| **Containers** | (planned) `Vec`, `Map` |

---

## 8. Safety and Low-Level

- **Safe by default:** No raw pointers in safe code; bounds and types checked.
- **Unsafe:** `unsafe { ... }` allows inline assembly and C, manual memory (when implemented).
- **Inline assembly:** `asm { "instruction" }` (syntax reserved; pass-through in progress).

---

## 9. Compilation

```bash
gsc main.gs -o app.exe    # compile and link
gsc main.gs -S             # emit assembly only
gsc main.gs -g             # debug build
gsc main.gs -O             # release (optimize)
gsc main.gs -m64           # 64-bit (requires 64-bit toolchain)
```

---

## 10. Reserved for Future Versions

- Templates/generics
- Modules and packages (import semantics)
- Error handling (Result/Option style)
- Pointers and manual memory (opt-in)
- RAII and destructors
- Async/await and threading
- Full standard library (files, net, math, strings, containers, OS)
