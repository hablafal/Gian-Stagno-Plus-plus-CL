def main() -> int {
    let size = 5;
    let arr: *int = new int[size];

    // Fill array using pointer arithmetic
    let i = 0;
    while (i < size) {
        *(arr + i) = i * 10;
        i = i + 1;
    }

    // Print array
    i = 0;
    while (i < size) {
        println(*(arr + i));
        i = i + 1;
    }

    delete arr;
    return 0;
}
