# Test Enhanced Dicts
d = {"a": 1, "b": 2}
log("Initial len: ", d.len())

val = d.get("a", 0)
log("Get a: ", val)

val = d.get("c", 100)
log("Get c (default): ", val)

d.remove("a")
log("After remove a, len: ", d.len())

keys = d.keys()
log("Keys len: ", keys.len())

d.clear()
log("After clear, len: ", d.len())
