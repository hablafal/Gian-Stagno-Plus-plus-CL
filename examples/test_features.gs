# Test new features
items = [1, 2, 3, 4, 5]
slice = items[1:4]
log("Slice: ", slice)

doubled = [x * 2 for x in items]
log("Doubled: ", doubled)

d = {"name": "Gian", "age": 20}
log("Name from dict: ", d["name"])
