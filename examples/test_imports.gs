import "examples/lib.gs" as mylib
mylib.say_hello()

from "examples/lib.gs" import Tool
t = Tool()
t.work()
