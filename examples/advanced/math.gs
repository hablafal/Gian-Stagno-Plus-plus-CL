struct Point {
    x: int;
    y: int;
}

def createPoint(x: int, y: int) -> *Point {
    let p = new Point;
    p.x = x;
    p.y = y;
    return p;
}
