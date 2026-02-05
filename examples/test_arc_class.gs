struct S:
    val: int

fn main():
    let s1 = new S()
    let s2 = s1
    s1 = new S()
