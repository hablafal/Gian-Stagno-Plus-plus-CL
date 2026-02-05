def worker(arg: int):
    println(arg)

fn main():
    let t = spawn worker(42)
    join t
