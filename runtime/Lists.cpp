#include <vector>
#include <cstdlib>
#include <cstdint>

struct GSPPList {
    uint64_t* data;
    long long len;
    long long cap;
};

extern "C" {

GSPPList* gspp_list_new(long long initial_cap) {
    GSPPList* list = (GSPPList*)malloc(sizeof(GSPPList));
    if (initial_cap < 10) initial_cap = 10;
    list->data = (uint64_t*)malloc(initial_cap * sizeof(uint64_t));
    list->len = 0;
    list->cap = initial_cap;
    return list;
}

void gspp_list_append(GSPPList* list, uint64_t val) {
    if (list->len >= list->cap) {
        list->cap *= 2;
        list->data = (uint64_t*)realloc(list->data, list->cap * sizeof(uint64_t));
    }
    list->data[list->len++] = val;
}

GSPPList* gspp_list_slice(GSPPList* list, long long start, long long end) {
    if (start < 0) start = 0;
    if (end > list->len) end = list->len;
    if (start >= end) return gspp_list_new(10);

    long long sliceLen = end - start;
    GSPPList* res = gspp_list_new(sliceLen);
    for (long long i = 0; i < sliceLen; i++) {
        gspp_list_append(res, list->data[start + i]);
    }
    return res;
}

}
