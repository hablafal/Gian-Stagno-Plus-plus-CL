# Test Low Level Features
# Pointers
x = 42
ptr = &x
log("Value via pointer: ", *ptr)

*ptr = 100
log("New value of x: ", x)

# Manual Memory
p = new int
*p = 500
log("Heap value: ", *p)
delete p

# Pointer Arithmetic
arr = new int[5]
p2 = arr
*p2 = 1
p2 = cast<*int>(cast<int>(p2) + 8)
*p2 = 2

log("Arr[0]: ", arr[0])
log("Arr[1]: ", arr[1])
delete arr

# Sizeof
log("Size of int: ", sizeof(int))
log("Size of x: ", sizeof(x))

# Casting
addr = cast<int>(&x)
log("Address of x: ", addr)
