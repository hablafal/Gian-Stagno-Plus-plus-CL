def worker(arg: int):
    let x = arg

fn main():
    let t = spawn worker(42)
    join t
