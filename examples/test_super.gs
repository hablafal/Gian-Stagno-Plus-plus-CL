struct Base:
    val: int
    def init(self, v: int):
        self.val = v
    def greet(self):
        println(self.val)

struct Derived(Base):
    def greet(self):
        print(1000)
        super.greet()

fn main():
    let d = new Derived(42)
    d.greet()
