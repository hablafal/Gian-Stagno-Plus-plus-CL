#include <queue>
#include <mutex>
#include <condition_variable>

extern "C" {

struct gspp_chan {
    std::queue<void*> queue;
    std::mutex mtx;
    std::condition_variable cv_send;
    std::condition_variable cv_recv;
    size_t capacity;
};

void* gspp_chan_new(long long capacity) {
    auto* c = new gspp_chan();
    c->capacity = (size_t)capacity;
    return (void*)c;
}

void gspp_chan_send(void* chan_ptr, void* val) {
    auto* c = (gspp_chan*)chan_ptr;
    if (!c) return;
    std::unique_lock<std::mutex> lock(c->mtx);
    if (c->capacity > 0) {
        c->cv_send.wait(lock, [c] { return c->queue.size() < c->capacity; });
    }
    c->queue.push(val);
    c->cv_recv.notify_one();
}

void* gspp_chan_recv(void* chan_ptr) {
    auto* c = (gspp_chan*)chan_ptr;
    if (!c) return nullptr;
    std::unique_lock<std::mutex> lock(c->mtx);
    c->cv_recv.wait(lock, [c] { return !c->queue.empty(); });
    void* val = c->queue.front();
    c->queue.pop();
    c->cv_send.notify_one();
    return val;
}

void gspp_chan_destroy(void* chan_ptr) {
    delete (gspp_chan*)chan_ptr;
}

}
