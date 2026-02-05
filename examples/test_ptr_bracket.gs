struct S:
    x: int

def f(s: ptr[S]):
    s.x = 1337

fn main():
    let s = new S()
    f(s)
    println(s.x)
