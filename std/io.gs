extern "C" def printf(fmt: *char) -> int;

def print(s: string) {
    // print_string is a builtin but we can wrap it
    println(s);
}

def println_i(i: int) {
    println(i);
}

def println_f(f: float) {
    println_float(f);
}
