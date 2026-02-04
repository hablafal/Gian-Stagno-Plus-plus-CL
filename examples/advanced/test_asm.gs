def main() -> int {
    let x = 0;
    unsafe {
        asm {
            "movq $123, -8(%rbp)"
        }
    }
    println(x);
    return 0;
}
