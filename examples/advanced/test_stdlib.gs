import "std/vec.gs";

def main() -> int {
    let v = vec.new_vec<int>();

    vec.push<int>(v, 10);
    vec.push<int>(v, 20);
    vec.push<int>(v, 30);

    println(vec.get<int>(v, 0));
    println(vec.get<int>(v, 1));
    println(vec.get<int>(v, 2));

    vec.delete_vec<int>(v);
    return 0;
}
