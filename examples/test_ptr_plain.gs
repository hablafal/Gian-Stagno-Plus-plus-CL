def f(x: ptr int):
    unsafe:
        asm { "movq $777, (%rdi)" }

fn main():
    let val = 0
    f(&val)
    println(val)
