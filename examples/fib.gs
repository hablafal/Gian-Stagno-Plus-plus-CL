// Fibonacci: first 10 numbers — clean GS++ style
def main() -> int {
    var a: int = 0;
    var b: int = 1;
    var i: int = 0;
    while (i < 10) {
        println(a);
        let next = a + b;
        a = b;
        b = next;
        i = i + 1;
    }
    return 0;
}
