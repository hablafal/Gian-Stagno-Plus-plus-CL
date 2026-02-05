def worker(arg: int):
    unsafe:
        asm { "movq $42, %rdi" }
        asm { "call println" }

fn main():
    println(1)
    let t = spawn worker(123)
    println(2)
    join t
    println(3)
