# GS++ (Gian Stagno Plus Plus) — Language Reference

## Keywords

`var`, `let`, `func`, `def`, `fn`, `class`, `struct`, `if`, `else`, `elif`, `while`, `for`, `in`, `range`, `return`, `import`, `as`, `from`, `asm`, `unsafe`, `new`, `delete`, `extern`, `nil`, `cast`, `sizeof`, `spawn`, `join`, `mutex`, `lock`, `chan`, `ptr`, `super`, `try`, `except`, `finally`, `raise`, `true`, `false`, `and`, `or`, `not`

## Types

- **Primitive:** `int`, `float`, `bool`, `char`, `string`
- **Collections:** `arr[T]`, `dict[K, V]`, `set[T]`, `tuple(...)`
- **Concurrency:** `chan[T]`, `mutex`, `thread`
- **Pointers:** `ptr[T]`, `ptr T`, or `*T`

## Syntax Highlights

### OOP
```gs
struct MyBase:
    x: int

struct MyDerived(MyBase):
    def init(self, x: int):
        self.x = x
    def show(self):
        super.show() # if it existed
```

### Concurrency
```gs
let c = chan[int](10)
spawn some_func(c)
c <- 42      # send
let v = <- c # receive
```

### Exceptions
```gs
try:
    raise "Something went wrong"
except Exception as e:
    println(e)
finally:
    println("Cleanup")
```

### Low-level
```gs
unsafe:
    asm { "movq $1, %rax" }
```

For the **full specification**, see **[docs/GSPP_SPEC.md](docs/GSPP_SPEC.md)**.
