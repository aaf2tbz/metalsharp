#pragma once

#include <metalsharp/Win32Types.h>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <cctype>
#include <ctime>
#include <cwchar>
#include <clocale>

namespace metalsharp {
namespace win32 {

static void* MSABI msabi_malloc(size_t s) { return malloc(s); }
static void MSABI msabi_free(void* p) { free(p); }
static void* MSABI msabi_realloc(void* p, size_t s) { return realloc(p, s); }
static void* MSABI msabi_calloc(size_t n, size_t s) { return calloc(n, s); }
static void* MSABI msabi_memset(void* d, int c, size_t n) { return memset(d, c, n); }
static void* MSABI msabi_memcpy(void* d, const void* s, size_t n) { return memcpy(d, s, n); }
static void* MSABI msabi_memmove(void* d, const void* s, size_t n) { return memmove(d, s, n); }
static int MSABI msabi_memcmp(const void* a, const void* b, size_t n) { return memcmp(a, b, n); }
static size_t MSABI msabi_strlen(const char* s) { return strlen(s); }
static char* MSABI msabi_strcpy(char* d, const char* s) { return strcpy(d, s); }
static char* MSABI msabi_strcat(char* d, const char* s) { return strcat(d, s); }
static int MSABI msabi_strcmp(const char* a, const char* b) { return strcmp(a, b); }
static int MSABI msabi_strncmp(const char* a, const char* b, size_t n) { return strncmp(a, b, n); }
static char* MSABI msabi_strncpy(char* d, const char* s, size_t n) { return strncpy(d, s, n); }
static char* MSABI msabi_strstr(const char* h, const char* n) { return const_cast<char*>(strstr(h, n)); }
static char* MSABI msabi_strchr(const char* s, int c) { return const_cast<char*>(strchr(s, c)); }
static char* MSABI msabi_strrchr(const char* s, int c) { return const_cast<char*>(strrchr(s, c)); }
static int MSABI msabi_atoi(const char* s) { return atoi(s); }
static double MSABI msabi_atof(const char* s) { return atof(s); }
static long MSABI msabi_strtol(const char* s, char** e, int b) { return strtol(s, e, b); }
static double MSABI msabi_strtod(const char* s, char** e) { return strtod(s, e); }
static unsigned long MSABI msabi_strtoul(const char* s, char** e, int b) { return strtoul(s, e, b); }
static int MSABI msabi_sprintf(char* buf, const char* fmt, ...) { (void)fmt; if(buf) buf[0]=0; return 0; }
static int MSABI msabi_snprintf(char* buf, size_t n, const char* fmt, ...) { (void)fmt; if(buf&&n) buf[0]=0; return 0; }
static int MSABI msabi_sscanf(const char* s, const char* fmt, ...) { (void)s; (void)fmt; return 0; }
static int MSABI msabi_printf(const char* fmt, ...) { (void)fmt; return 0; }
static int MSABI msabi_fprintf(void* f, const char* fmt, ...) { (void)f; (void)fmt; return 0; }
static void* MSABI msabi_fopen(const char* p, const char* m) { return (void*)fopen(p, m); }
static int MSABI msabi_fclose(void* f) { return fclose((FILE*)f); }
static size_t MSABI msabi_fread(void* b, size_t s, size_t n, void* f) { return fread(b, s, n, (FILE*)f); }
static size_t MSABI msabi_fwrite(const void* b, size_t s, size_t n, void* f) { return fwrite(b, s, n, (FILE*)f); }
static int MSABI msabi_fseek(void* f, long o, int w) { return fseek((FILE*)f, o, w); }
static long MSABI msabi_ftell(void* f) { return ftell((FILE*)f); }
static char* MSABI msabi_fgets(char* b, int n, void* f) { return fgets(b, n, (FILE*)f); }
static int MSABI msabi_vsnprintf(char* b, size_t n, const char* fmt, void* ap) { (void)ap; if(b&&n) b[0]=0; (void)fmt; return 0; }
static int MSABI msabi_strcasecmp(const char* a, const char* b) { return strcasecmp(a, b); }
static int MSABI msabi_strncasecmp(const char* a, const char* b, size_t n) { return strncasecmp(a, b, n); }
static size_t MSABI msabi_wcslen(const wchar_t* s) { return wcslen(s); }
static wchar_t* MSABI msabi_wcscpy(wchar_t* d, const wchar_t* s) { return wcscpy(d, s); }
static wchar_t* MSABI msabi_wcscat(wchar_t* d, const wchar_t* s) { return wcscat(d, s); }
static int MSABI msabi_wcscmp(const wchar_t* a, const wchar_t* b) { return wcscmp(a, b); }
static int MSABI msabi_tolower(int c) { return tolower(c); }
static int MSABI msabi_toupper(int c) { return toupper(c); }
static int MSABI msabi_isalpha(int c) { return isalpha(c); }
static int MSABI msabi_isdigit(int c) { return isdigit(c); }
static int MSABI msabi_isspace(int c) { return isspace(c); }
static void MSABI msabi_qsort(void* b, size_t n, size_t s, int(*c)(const void*, const void*)) { qsort(b, n, s, c); }
static int MSABI msabi_rand() { return rand(); }
static void MSABI msabi_srand(unsigned int s) { srand(s); }
static int MSABI msabi_abs(int n) { return abs(n); }
static long MSABI msabi_labs(long n) { return labs(n); }
static double MSABI msabi_floor(double x) { return floor(x); }
static double MSABI msabi_ceil(double x) { return ceil(x); }
static double MSABI msabi_sqrt(double x) { return sqrt(x); }
static double MSABI msabi_sin(double x) { return sin(x); }
static double MSABI msabi_cos(double x) { return cos(x); }
static double MSABI msabi_tan(double x) { return tan(x); }
static double MSABI msabi_atan2(double y, double x) { return atan2(y, x); }
static double MSABI msabi_pow(double x, double y) { return pow(x, y); }
static double MSABI msabi_log(double x) { return log(x); }
static double MSABI msabi_log10(double x) { return log10(x); }
static double MSABI msabi_exp(double x) { return exp(x); }
static double MSABI msabi_fabs(double x) { return fabs(x); }
static double MSABI msabi_fmod(double x, double y) { return fmod(x, y); }
static double MSABI msabi_modf(double x, double* i) { return modf(x, i); }
static double MSABI msabi_ldexp(double x, int e) { return ldexp(x, e); }
static double MSABI msabi_frexp(double x, int* e) { return frexp(x, e); }
static void MSABI msabi_exit(int c) { exit(c); }
static void MSABI msabi__Exit(int c) { _Exit(c); }
static void MSABI msabi_abort() { abort(); }
static clock_t MSABI msabi_clock() { return clock(); }
static time_t MSABI msabi_time(time_t* t) { return time(t); }
static size_t MSABI msabi_strftime(char* b, size_t m, const char* f, const void* tm) { return strftime(b, m, f, (const struct tm*)tm); }
static char* MSABI msabi_setlocale(int c, const char* l) { return setlocale(c, l); }

}
}
