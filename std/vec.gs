class Vec<T> {
    data: *T;
    size: int;
    capacity: int;
}

def new_vec<T>() -> *Vec<T> {
    let v = new Vec<T>;
    v.size = 0;
    v.capacity = 4;
    v.data = new T[4];
    return v;
}

def push<T>(v: *Vec<T>, item: T) {
    if (v.size == v.capacity) {
        let newCap = v.capacity * 2;
        let newData = new T[newCap];
        let i = 0;
        while (i < v.size) {
            *(newData + i) = *(v.data + i);
            i = i + 1;
        }
        delete v.data;
        v.data = newData;
        v.capacity = newCap;
    }
    *(v.data + v.size) = item;
    v.size = v.size + 1;
}

def get<T>(v: *Vec<T>, index: int) -> T {
    return *(v.data + index);
}

def delete_vec<T>(v: *Vec<T>) {
    delete v.data;
    delete v;
}
