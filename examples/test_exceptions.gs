# Test Exceptions
try:
    log("In try block")
    raise 123
    log("This should not print")
except e:
    log("Caught exception: ", e)
finally:
    log("Finally block runs")

log("Program continues")

fn fail():
    log("In fail()")
    raise 456

try:
    fail()
except error:
    log("Caught nested exception: ", error)
