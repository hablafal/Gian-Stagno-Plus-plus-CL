#include <iostream>
#include <string>
#include <cstring>
#include <cstdlib>

extern "C" {

void* gspp_alloc(size_t size);

char* _gspp_strcat(const char* s1, const char* s2) {
    size_t len1 = strlen(s1);
    size_t len2 = strlen(s2);
    char* res = (char*)gspp_alloc(len1 + len2 + 1);
    strcpy(res, s1);
    strcat(res, s2);
    return res;
}

char* gspp_str_slice(const char* s, long long start, long long end) {
    long long len = (long long)strlen(s);
    if (start < 0) start = 0;
    if (end > len) end = len;
    if (start >= end) {
        char* res = (char*)gspp_alloc(1);
        res[0] = '\0';
        return res;
    }
    size_t sliceLen = (size_t)(end - start);
    char* res = (char*)gspp_alloc(sliceLen + 1);
    strncpy(res, s + start, sliceLen);
    res[sliceLen] = '\0';
    return res;
}

}
