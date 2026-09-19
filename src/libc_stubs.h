#ifndef LIBC_STUBS_H
#define LIBC_STUBS_H

#include "core.h"

typedef u64 size_t;
typedef s64 ssize_t;
typedef s64 ptrdiff_t;
#define NULL ((void*)0)
#define offsetof(type, member) ((size_t)&((type*)0)->member)

#define INT_MAX   0x7FFFFFFF
#define INT_MIN   (-INT_MAX - 1)
#define UINT_MAX  0xFFFFFFFFU
#define LONG_MAX  0x7FFFFFFFFFFFFFFFL
#define LONG_MIN  (-LONG_MAX - 1)
#define CHAR_BIT  8
#define PATH_MAX  256
#define SHRT_MAX  0x7FFF
#define SHRT_MIN  (-SHRT_MAX - 1)

#ifndef __cplusplus
#define bool  _Bool
#define true  1
#define false 0
#endif

typedef __builtin_va_list va_list;
#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_end(ap)         __builtin_va_end(ap)
#define va_arg(ap, type)   __builtin_va_arg(ap, type)
#define va_copy(d, s)      __builtin_va_copy(d, s)

extern int errno;
#define ENOENT  2
#define EINVAL  22
#define ENOMEM  12
#define EEXIST  17
#define EBADF   9

void *memcpy(void *dst, const void *src, size_t n);
void *memset(void *s, int c, size_t n);
void *memmove(void *dst, const void *src, size_t n);
int   memcmp(const void *a, const void *b, size_t n);
void *memchr(const void *s, int c, size_t n);

size_t strlen(const char *s);
char  *strcpy(char *dst, const char *src);
char  *strncpy(char *dst, const char *src, size_t n);
char  *strcat(char *dst, const char *src);
char  *strncat(char *dst, const char *src, size_t n);
int    strcmp(const char *a, const char *b);
int    strncmp(const char *a, const char *b, size_t n);
int    strcasecmp(const char *a, const char *b);
int    strncasecmp(const char *a, const char *b, size_t n);
char  *strchr(const char *s, int c);
char  *strrchr(const char *s, int c);
char  *strstr(const char *haystack, const char *needle);
char  *strdup(const char *s);
char  *strerror(int errnum);

int toupper(int c);
int tolower(int c);
int isdigit(int c);
int isalpha(int c);
int isalnum(int c);
int isspace(int c);
int isupper(int c);
int islower(int c);
int isprint(int c);
int isxdigit(int c);
int ispunct(int c);

typedef struct _FILE
{
    s32 fd;
    int eof;
    int error;
    int mode;

    u8 *buf;
    int buf_pos;
    int buf_len;
    int buf_dir;  /* 0 = idle, 1 = holding reads, 2 = holding writes */
} FILE;

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#define EOF      (-1)
#define BUFSIZ   1024

FILE  *fopen(const char *path, const char *mode);
size_t fread(void *ptr, size_t size, size_t nmemb, FILE *f);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *f);
int    fclose(FILE *f);
int    fseek(FILE *f, long offset, int whence);
long   ftell(FILE *f);
void   rewind(FILE *f);
int    feof(FILE *f);
int    ferror(FILE *f);
int    fflush(FILE *f);
int    fputc(int c, FILE *f);
int    fputs(const char *s, FILE *f);
char  *fgets(char *buf, int n, FILE *f);
int    fgetc(FILE *f);
int    ungetc(int c, FILE *f);

int    printf(const char *fmt, ...);
int    fprintf(FILE *f, const char *fmt, ...);
int    sprintf(char *buf, const char *fmt, ...);
int    snprintf(char *buf, size_t n, const char *fmt, ...);
int    vsnprintf(char *buf, size_t n, const char *fmt, va_list ap);
int    vfprintf(FILE *f, const char *fmt, va_list ap);
int    sscanf(const char *str, const char *fmt, ...);

int    putchar(int c);
int    puts(const char *s);
int    remove(const char *path);
int    rename(const char *oldpath, const char *newpath);

void  *malloc(size_t size);
void   free(void *ptr);
void  *realloc(void *ptr, size_t size);
void  *calloc(size_t nmemb, size_t size);

void   exit(int status);
void   abort(void);
int    atexit(void (*func)(void));

int    abs(int x);
long   labs(long x);
int    atoi(const char *s);
long   atol(const char *s);
long   strtol(const char *s, char **end, int base);
unsigned long strtoul(const char *s, char **end, int base);

void   qsort(void *base, size_t nmemb, size_t size,
             int (*cmp)(const void *, const void *));

int    rand(void);
void   srand(unsigned int seed);
#define RAND_MAX 0x7FFFFFFF

char  *getenv(const char *name);

typedef long time_t;

struct tm
{
    int tm_sec, tm_min, tm_hour;
    int tm_mday, tm_mon, tm_year;
    int tm_wday, tm_yday, tm_isdst;
};

time_t       time(time_t *t);
struct tm   *localtime(const time_t *t);

#define SIGINT   2
#define SIGTERM  15
#define SIGKILL  9
#define SIG_DFL  ((void(*)(int))0)
#define SIG_IGN  ((void(*)(int))1)

typedef void (*sighandler_t)(int);
sighandler_t signal(int signum, sighandler_t handler);

typedef long jmp_buf[8];
int  setjmp(jmp_buf env);
void longjmp(jmp_buf env, int val);

int    access(const char *path, int mode);
#define F_OK 0
#define R_OK 4
#define W_OK 2

void libc_init(void *gadget_ptr, void *dlsym_ptr, void *mmap_fn, void *kopen,
               void *kread, void *kwrite, void *kclose,
               void *klseek, void *kunlink, void *kusleep,
               void *kmkdir,
               void *sendto, s32 log_fd, u8 *log_sa);

#endif
