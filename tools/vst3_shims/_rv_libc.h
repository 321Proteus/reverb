#pragma once
// Declarations only: the generator never links, it just needs the headers to
// parse.
#include <stddef.h>

extern "C" {
int memcmp(const void*, const void*, size_t);
void* memcpy(void*, const void*, size_t);
void* memset(void*, int, size_t);
size_t strlen(const char*);
char* strcpy(char*, const char*);
char* strncpy(char*, const char*, size_t);
int strcmp(const char*, const char*);
int strncmp(const char*, const char*, size_t);
void* malloc(size_t);
void free(void*);
int sprintf(char*, const char*, ...);
int snprintf(char*, size_t, const char*, ...);
}

namespace std {
using ::free;
using ::malloc;
using ::memcmp;
using ::memcpy;
using ::memset;
using ::size_t;
using ::strcmp;
using ::strcpy;
using ::strlen;
using ::strncmp;
using ::strncpy;
} // namespace std
