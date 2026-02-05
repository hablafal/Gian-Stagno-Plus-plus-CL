#include <cstdlib>
#include <cstdint>
#include <cstdio>

struct gspp_obj_header {
    uint64_t refcount;
    void (*destructor)(void*);
};

extern "C" {

void* gspp_alloc_with_dtor(size_t size, void (*dtor)(void*)) {
    void* ptr = malloc(sizeof(gspp_obj_header) + size);
    if (!ptr) return nullptr;
    gspp_obj_header* header = (gspp_obj_header*)ptr;
    header->refcount = 1;
    header->destructor = dtor;
    return (void*)(header + 1);
}

void* gspp_alloc(size_t size) {
    return gspp_alloc_with_dtor(size, nullptr);
}

void gspp_retain(void* ptr) {
    if (!ptr) return;
    gspp_obj_header* header = (gspp_obj_header*)ptr - 1;
    if (header->refcount == (uint64_t)-1) return;
    header->refcount++;
}

void gspp_release(void* ptr) {
    if (!ptr) return;
    gspp_obj_header* header = (gspp_obj_header*)ptr - 1;
    if (header->refcount == (uint64_t)-1) return;
    header->refcount--;
    if (header->refcount == 0) {
        if (header->destructor) {
            header->destructor(ptr);
        }
        free(header);
    }
}

// For compatibility with current malloc-based allocations while transitioning
void* gspp_malloc(size_t size) { return gspp_alloc(size); }

}
