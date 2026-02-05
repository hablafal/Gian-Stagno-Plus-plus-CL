struct S:
    x: int
    def f(self):
        self.x = 1

fn main():
    let s = new S()
    s.f()
