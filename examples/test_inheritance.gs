class Animal:
    name: string
    def init(self, name: string):
        self.name = name

    def speak(self):
        println_string("Animal makes a sound")

class Dog(Animal):
    breed: string
    def init(self, name: string, breed: string):
        super.init(name)
        self.breed = breed

    def speak(self):
        super.speak()
        println_string("Dog barks: Woof!")

fn main():
    let d = new Dog("Buddy", "Golden Retriever")
    println_string("Dog name: ")
    println_string(d.name)
    println_string("Dog breed: ")
    println_string(d.breed)
    d.speak()
