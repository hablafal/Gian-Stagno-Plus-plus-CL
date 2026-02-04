extern "C" def puts(s: *char) -> int;

def main() -> int {
    puts("Hello from C puts!");
    return 0;
}
