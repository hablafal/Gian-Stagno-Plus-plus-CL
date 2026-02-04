# Test Tuples
t = (1, 2, 3)
log("Tuple element 0: ", t[0])
log("Tuple element 1: ", t[1])

mt = mut (10, 20)
log("Mutable tuple before: ", mt[0])
mt[0] = 100
log("Mutable tuple after: ", mt[0])

# This should fail semantic analysis if uncommented
# t[0] = 5
