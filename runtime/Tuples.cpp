#include <vector>
#include <cstdlib>
#include <cstdint>

struct GSPPTuple {
    uint64_t* data;
    long long len;
    bool is_mutable;
};

extern "C" {

void* gspp_alloc(size_t size);
void* gspp_alloc_with_dtor(size_t size, void (*dtor)(void*));

void gspp_tuple_dtor(void* ptr) {
    GSPPTuple* t = (GSPPTuple*)ptr;
    if (t->data) free(t->data);
}

GSPPTuple* gspp_tuple_new(long long len, bool is_mutable) {
    GSPPTuple* t = (GSPPTuple*)gspp_alloc_with_dtor(sizeof(GSPPTuple), gspp_tuple_dtor);
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
