def worker(id: int):
    println(id)

fn main():
    let t1 = spawn worker(1)
    let t2 = spawn worker(2)
    join t1
    join t2
    println(100)
