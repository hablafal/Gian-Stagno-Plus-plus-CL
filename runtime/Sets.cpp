#include <unordered_set>
#include <vector>
#include <cstdint>
#include <algorithm>
#include <cstring>
#include <new>

struct GSPPSet {
    std::unordered_set<uint64_t> elements;
    // We should handle string keys properly if they are string pointers
};

// Simplified: using the uint64_t as key. For strings, we might need a better hash.
// But following the same logic as Dict.

extern "C" {

void* gspp_alloc(size_t size);
void* gspp_alloc_with_dtor(size_t size, void (*dtor)(void*));

void gspp_set_dtor(void* ptr) {
    ((GSPPSet*)ptr)->~GSPPSet();
}

GSPPSet* gspp_set_new() {
    GSPPSet* s = (GSPPSet*)gspp_alloc_with_dtor(sizeof(GSPPSet), gspp_set_dtor);
    new (s) GSPPSet();
    return s;
}


void gspp_set_add(GSPPSet* s, uint64_t val) {
    // Basic set add. If string, we'd need to compare content.
    // For now, let's keep it simple and just use the pointer/int value.
    s->elements.insert(val);
}

bool gspp_set_has(GSPPSet* s, uint64_t val) {
    return s->elements.find(val) != s->elements.end();
}

GSPPSet* gspp_set_union(GSPPSet* s1, GSPPSet* s2) {
    GSPPSet* res = (GSPPSet*)gspp_alloc_with_dtor(sizeof(GSPPSet), gspp_set_dtor);
    new (res) GSPPSet(*s1);
    for (uint64_t e : s2->elements) {
        res->elements.insert(e);
    }
    return res;
}

GSPPSet* gspp_set_intersection(GSPPSet* s1, GSPPSet* s2) {
    GSPPSet* res = gspp_set_new();
    for (uint64_t e : s1->elements) {
        if (s2->elements.find(e) != s2->elements.end()) {
            res->elements.insert(e);
        }
    }
    return res;
}

long long gspp_set_len(GSPPSet* s) {
    return (long long)s->elements.size();
}

}
