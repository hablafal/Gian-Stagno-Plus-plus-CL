class Box<T> {
    value: T;
}

def createBox<T>(v: T) -> *Box<T> {
    let b = new Box<T>;
    b.value = v;
    return b;
}

def main() -> int {
    let b1 = createBox<int>(42);
    println(b1.value);

    let b2 = createBox<float>(3.14);
    println_float(b2.value);

    delete b1;
    delete b2;
    return 0;
}
