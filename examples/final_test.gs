# Comprehensive GS++ Test
fn welcome():
    log("Welcome to GS++ - The ultimate easy language!")

welcome()

# Hybrid blocks and Indentation
if yes:
    log("Booleans work with 'yes'")
    if 1 < 2:
        log("Nested indentation works!")

# Type Aliases
name: Text = "Gian"
price: decimal = 19.99
items: Arr = [1, 2, 3]

log("Name: ", name, " Price: ", price)

# List methods
items.append(4)
log("Items len: ", items.len())

# Defer
fn test_defer():
    defer log("This runs last (deferred)")
    log("This runs first")

test_defer()

# Loops
repeat 2:
    log("Repeating...")

loop items as x:
    log("Looping: ", x)

# Ternary
status = if yes then "Ready" else "Wait"
log("Status: ", status)

# Classes
class Person:
    def init(n):
        self.name = n

    def greet():
        log("Hello, my name is ", self.name)

p = Person("Stagno")
p.greet()

# File I/O
File.write("test.txt", "GS++ One-liner!")
content = File.read("test.txt")
log("Read from file: ", content)
