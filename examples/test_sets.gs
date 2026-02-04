# Test Sets
s1 = {1, 2, 3}
s2 = {3, 4, 5}

s3 = s1 | s2
log("Set Union len: ", s3.len()) # should be 5

s4 = s1 & s2
log("Set Intersection len: ", s4.len()) # should be 1
