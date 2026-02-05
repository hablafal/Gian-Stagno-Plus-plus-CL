def producer(c: chan[int]):
    for i in range(0, 5):
        c <- i
        println(100 + i)

fn main():
    let c = chan[int](2)
    spawn producer(c)
    for i in range(0, 5):
        let val = <- c
        println(val)
