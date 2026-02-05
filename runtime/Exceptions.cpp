#include <setjmp.h>
#include <vector>
#include <cstdio>
#include <cstdlib>

struct GSPP_ExceptionFrame {
    jmp_buf env;
    void* exceptionObject;
};

static thread_local std::vector<jmp_buf*> exceptionStack;
static thread_local void* currentException = nullptr;

extern "C" {

void gspp_push_exception_handler(jmp_buf* env) {
    exceptionStack.push_back(env);
}

void gspp_pop_exception_handler() {
    if (!exceptionStack.empty()) {
        exceptionStack.pop_back();
    }
}

void gspp_raise(void* obj) {
    currentException = obj;
    if (exceptionStack.empty()) {
        fprintf(stderr, "Unhandled GS++ Exception: %p\n", obj);
        exit(1);
    }
    jmp_buf* env = exceptionStack.back();
    exceptionStack.pop_back();
    longjmp(*env, 1);
}

void* gspp_get_current_exception() {
    return currentException;
}

}
