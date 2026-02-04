#include <iostream>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {

void println(long long val) {
    printf("%lld\n", val);
}

void print(long long val) {
    printf("%lld", val);
}

void println_float(double val) {
    printf("%f\n", val);
}

void print_float(double val) {
    printf("%f", val);
}

void println_string(const char* val) {
    printf("%s\n", val);
}

void print_string(const char* val) {
    printf("%s", val);
}

char* gspp_input() {
    char* buf = (char*)malloc(256);
    scanf("%255s", buf);
    return buf;
}

char* gspp_read_file(const char* filename) {
    FILE* f = fopen(filename, "r");
    if (!f) return nullptr;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    rewind(f);
    char* res = (char*)malloc(len + 1);
    fread(res, 1, len, f);
    res[len] = '\0';
    fclose(f);
    return res;
}

void gspp_write_file(const char* filename, const char* content) {
    FILE* f = fopen(filename, "w");
    if (!f) return;
    fputs(content, f);
    fclose(f);
}

int gspp_exec(const char* cmd) {
    return system(cmd);
}

}
