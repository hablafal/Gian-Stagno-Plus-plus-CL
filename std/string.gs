def string_len(s: string) -> int {
    extern "C" def strlen(s: string) -> int;
    return strlen(s);
}

def string_concat(a: string, b: string) -> string {
    return a + b;
}
