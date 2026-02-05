class Counter:
    val: int

def increment(args: tuple):
    println(1001)
    println(cast<int>(args))
    let c = cast<*Counter>(args[0])
    println(cast<int>(c))
    let m = cast<mutex>(args[1])
    for let i = 0; i < 5; i = i + 1:
        lock m:
            c.val = c.val + 1
            println(c.val)

fn main():
    let c = new Counter()
    c.val = 0
    println(cast<int>(c))
    mutex m
    let args = (c, m)
    println(cast<int>(args))
    let t1 = spawn increment(args)
    let t2 = spawn increment(args)
    join t1
    join t2
    println(c.val)
