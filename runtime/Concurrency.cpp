#include <thread>
#include <mutex>
#include <vector>
#include <iostream>
#include <cstdio>
#include <pthread.h>

extern "C" {

struct gspp_thread_args {
    void (*func)(void*);
    void* arg;
};

void* gspp_thread_wrapper(void* data) {
    gspp_thread_args* args = (gspp_thread_args*)data;
    void (*func)(void*) = args->func;
    void* arg = args->arg;
    delete args;

    func(arg);
    return NULL;
}

void* gspp_spawn(void (*func)(void*), void* arg) {
    pthread_t* t = new pthread_t();
    gspp_thread_args* args = new gspp_thread_args{func, arg};
    if (pthread_create(t, NULL, gspp_thread_wrapper, args) != 0) {
        delete t;
        delete args;
        return nullptr;
    }
    return (void*)t;
}

void gspp_join(void* thread_ptr) {
    pthread_t* t = (pthread_t*)thread_ptr;
    if (t) {
        pthread_join(*t, NULL);
        delete t;
    }
}

void gspp_detach(void* thread_ptr) {
    pthread_t* t = (pthread_t*)thread_ptr;
    if (t) {
        pthread_detach(*t);
        delete t;
    }
}

void* gspp_mutex_create() {
    pthread_mutex_t* m = new pthread_mutex_t();
    pthread_mutex_init(m, NULL);
    return (void*)m;
}

void gspp_mutex_lock(void* mutex_ptr) {
    pthread_mutex_t* m = (pthread_mutex_t*)mutex_ptr;
    if (m) pthread_mutex_lock(m);
}

void gspp_mutex_unlock(void* mutex_ptr) {
    pthread_mutex_t* m = (pthread_mutex_t*)mutex_ptr;
    if (m) pthread_mutex_unlock(m);
}

void gspp_mutex_destroy(void* mutex_ptr) {
    pthread_mutex_t* m = (pthread_mutex_t*)mutex_ptr;
    if (m) {
        pthread_mutex_destroy(m);
        delete m;
    }
}

void gspp_sleep(int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

}
