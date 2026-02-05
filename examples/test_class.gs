class Base:
    val: int
    def init(self, v: int):
        self.val = v
    def greet(self):
        println(self.val)

class Derived(Base):
    def greet(self):
        println(self.val + 100)

fn main():
    let b = new Base(10)
    b.greet()

    let d = new Derived(20)
    d.greet()
