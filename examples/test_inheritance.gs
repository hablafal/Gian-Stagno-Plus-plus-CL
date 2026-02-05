class Animal:
    def init(name: string):
        self.name = name

    def speak():
        log("Animal makes a sound")

class Dog(Animal):
    def init(name: string, breed: string):
        super.init(name)
        self.breed = breed

    def speak():
        super.speak()
        log(self.name, " barks: Woof!")

fn main():
    d = Dog("Buddy", "Golden Retriever")
    log("Dog name: ", d.name)
    log("Dog breed: ", d.breed)
    d.speak()
