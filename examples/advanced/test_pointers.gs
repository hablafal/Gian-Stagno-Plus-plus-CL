def main() -> int {
    let x = 42;
    let ptr = &x;
    println(*ptr);

    unsafe {
        let heapPtr: *int = new int;
        *heapPtr = 100;
        println(*heapPtr);
        delete heapPtr;
    }

    let s = "Hello, GS++!";
    // print string address for now
    println(4242);

    return 0;
}
