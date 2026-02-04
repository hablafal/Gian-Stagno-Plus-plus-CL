#include <vector>
#include <cstdlib>
#include <cstdint>

struct GSPPTuple {
    uint64_t* data;
    long long len;
    bool is_mutable;
};

extern "C" {

GSPPTuple* gspp_tuple_new(long long len, bool is_mutable) {
    GSPPTuple* t = (GSPPTuple*)malloc(sizeof(GSPPTuple));
    t->data = (uint64_t*)malloc(len * sizeof(uint64_t));
    t->len = len;
    t->is_mutable = is_mutable;
    return t;
}

void gspp_tuple_set(GSPPTuple* t, long long idx, uint64_t val) {
    if (idx >= 0 && idx < t->len) {
        t->data[idx] = val;
    }
}

uint64_t gspp_tuple_get(GSPPTuple* t, long long idx) {
    if (idx >= 0 && idx < t->len) {
        return t->data[idx];
    }
    return 0;
}

long long gspp_tuple_len(GSPPTuple* t) {
    return t->len;
}

}
