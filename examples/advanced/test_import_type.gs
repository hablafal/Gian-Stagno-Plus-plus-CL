import math;

def main() -> int {
    let p: *math.Point = math.createPoint(10, 20);
    println(p.x);
    println(p.y);
    delete p;
    return 0;
}
