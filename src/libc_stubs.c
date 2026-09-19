#include "libc_stubs.h"

#define LOG_SOCKADDR_LEN 16

static void *gadget, *dlsym_fn;
static void *fn_mmap, *fn_kopen, *fn_kread, *fn_kwrite;
static void *fn_kclose, *fn_klseek, *fn_kunlink, *fn_kusleep;
static void *fn_kmkdir;
static void *fn_sendto;
static s32   log_socket_fd;
static u8   *log_sockaddr;

static void heap_init(void);
static void dbg_log(const char *msg);

void libc_init(void *gadget_ptr, void *dlsym_ptr, void *mmap_fn, void *kopen,
               void *kread, void *kwrite, void *kclose,
               void *klseek, void *kunlink, void *kusleep,
               void *kmkdir,
               void *sendto, s32 log_fd, u8 *log_sa)
{
    gadget = gadget_ptr;
    dlsym_fn = dlsym_ptr;
    fn_mmap = mmap_fn;
    fn_kopen = kopen;
    fn_kread = kread;
    fn_kwrite = kwrite;
    fn_kclose = kclose;
    fn_klseek = klseek;
    fn_kunlink = kunlink;
    fn_kusleep = kusleep;
    fn_kmkdir = kmkdir;
    fn_sendto = sendto;
    log_socket_fd = log_fd;
    log_sockaddr = log_sa;
    heap_init();
}

static void dbg_log(const char *msg)
{
    if (log_socket_fd < 0 || !fn_sendto) return;
    int len = 0;
    while (msg[len]) len++;
    NC(gadget, fn_sendto, (u64)log_socket_fd, (u64)msg, (u64)len, 0,
       (u64)log_sockaddr, LOG_SOCKADDR_LEN);
}

#define HEAP_SIZE             (32 * 1024 * 1024)
#define HEAP_ALIGN            16
#define HEAP_PROT_READ_WRITE  3
#define HEAP_MAP_PRIVATE_ANON 0x1002

static u8 *heap_base;
static u64 heap_used;

static void heap_init(void)
{
    heap_base = (u8 *)NC(gadget, fn_mmap, 0, (u64)HEAP_SIZE,
                         HEAP_PROT_READ_WRITE, HEAP_MAP_PRIVATE_ANON,
                         (u64)-1, 0);
    if ((s64)heap_base == -1) heap_base = 0;
    heap_used = 0;
}

void *malloc(size_t size)
{
    if (!heap_base || size == 0) return NULL;
    size = (size + HEAP_ALIGN - 1) & ~(HEAP_ALIGN - 1);
    if (heap_used + size > HEAP_SIZE)
    {
        dbg_log("MALLOC OOM\n");
        return NULL;
    }
    void *block = heap_base + heap_used;
    heap_used += size;
    return block;
}

void free(void *ptr)
{
    /* bump allocator; nothing is reclaimed, Doom recycles inside Z_Zone */
    (void)ptr;
}

void *realloc(void *ptr, size_t size)
{
    if (!ptr) return malloc(size);
    if (size == 0)
    {
        free(ptr);
        return NULL;
    }
    void *new_block = malloc(size);
    if (new_block && ptr)
    {
        u8 *dst_bytes = (u8*)new_block;
        u8 *src_bytes = (u8*)ptr;
        for (size_t i = 0; i < size; i++) dst_bytes[i] = src_bytes[i];
    }
    return new_block;
}

void *calloc(size_t nmemb, size_t size)
{
    size_t total = nmemb * size;
    void *block = malloc(total);
    if (block) memset(block, 0, total);
    return block;
}

void *memcpy(void *dst, const void *src, size_t n)
{
    u8 *dst_bytes = (u8*)dst;
    const u8 *src_bytes = (const u8*)src;
    for (size_t i = 0; i < n; i++) dst_bytes[i] = src_bytes[i];
    return dst;
}

void *memset(void *s, int c, size_t n)
{
    u8 *bytes = (u8*)s;
    for (size_t i = 0; i < n; i++) bytes[i] = (u8)c;
    return s;
}

void *memmove(void *dst, const void *src, size_t n)
{
    u8 *dst_bytes = (u8*)dst;
    const u8 *src_bytes = (const u8*)src;
    if (dst_bytes < src_bytes)
    {
        for (size_t i = 0; i < n; i++) dst_bytes[i] = src_bytes[i];
    }
    else
    {
        for (size_t i = n; i > 0; i--) dst_bytes[i-1] = src_bytes[i-1];
    }
    return dst;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const u8 *bytes_a = (const u8*)a;
    const u8 *bytes_b = (const u8*)b;
    for (size_t i = 0; i < n; i++)
    {
        if (bytes_a[i] != bytes_b[i]) return bytes_a[i] - bytes_b[i];
    }
    return 0;
}

void *memchr(const void *s, int c, size_t n)
{
    const u8 *bytes = (const u8*)s;
    for (size_t i = 0; i < n; i++)
        if (bytes[i] == (u8)c) return (void*)(bytes + i);
    return NULL;
}

size_t strlen(const char *s)
{
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

char *strcpy(char *dst, const char *src)
{
    char *dst_cursor = dst;
    while ((*dst_cursor++ = *src++))
        ;
    return dst;
}

char *strncpy(char *dst, const char *src, size_t n)
{
    size_t i;
    for (i = 0; i < n && src[i]; i++) dst[i] = src[i];
    for (; i < n; i++) dst[i] = '\0';
    return dst;
}

char *strcat(char *dst, const char *src)
{
    char *dst_cursor = dst;
    while (*dst_cursor) dst_cursor++;
    while ((*dst_cursor++ = *src++))
        ;
    return dst;
}

char *strncat(char *dst, const char *src, size_t n)
{
    char *dst_end = dst;
    while (*dst_end) dst_end++;
    size_t i;
    for (i = 0; i < n && src[i]; i++) dst_end[i] = src[i];
    dst_end[i] = '\0';
    return dst;
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *(unsigned char*)a - *(unsigned char*)b;
}

int strncmp(const char *a, const char *b, size_t n)
{
    for (size_t i = 0; i < n; i++)
    {
        if (a[i] != b[i]) return (unsigned char)a[i] - (unsigned char)b[i];
        if (!a[i]) return 0;
    }
    return 0;
}

int strcasecmp(const char *a, const char *b)
{
    while (*a && *b)
    {
        int char_a = *a, char_b = *b;
        if (char_a >= 'A' && char_a <= 'Z') char_a += 32;
        if (char_b >= 'A' && char_b <= 'Z') char_b += 32;
        if (char_a != char_b) return char_a - char_b;
        a++; b++;
    }
    return *(unsigned char*)a - *(unsigned char*)b;
}

int strncasecmp(const char *a, const char *b, size_t n)
{
    for (size_t i = 0; i < n; i++)
    {
        if (!a[i] && !b[i]) return 0;
        int char_a = a[i], char_b = b[i];
        if (char_a >= 'A' && char_a <= 'Z') char_a += 32;
        if (char_b >= 'A' && char_b <= 'Z') char_b += 32;
        if (char_a != char_b) return char_a - char_b;
    }
    return 0;
}

char *strchr(const char *s, int c)
{
    while (*s)
    {
        if (*s == (char)c) return (char*)s;
        s++;
    }
    return (c == 0) ? (char*)s : NULL;
}

char *strrchr(const char *s, int c)
{
    const char *last = NULL;
    while (*s)
    {
        if (*s == (char)c) last = s;
        s++;
    }
    if (c == 0) return (char*)s;
    return (char*)last;
}

char *strstr(const char *haystack, const char *needle)
{
    if (!*needle) return (char*)haystack;
    for (; *haystack; haystack++)
    {
        const char *hay_cursor = haystack, *needle_cursor = needle;
        while (*hay_cursor && *needle_cursor && *hay_cursor == *needle_cursor)
        {
            hay_cursor++;
            needle_cursor++;
        }
        if (!*needle_cursor) return (char*)haystack;
    }
    return NULL;
}

char *strdup(const char *s)
{
    size_t len = strlen(s) + 1;
    char *copy = (char*)malloc(len);
    if (copy) memcpy(copy, s, len);
    return copy;
}

char *strerror(int errnum)
{
    (void)errnum;
    return (char*)"error";
}

int toupper(int c) { return (c >= 'a' && c <= 'z') ? c - 32 : c; }
int tolower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }
int isdigit(int c) { return c >= '0' && c <= '9'; }
int isalpha(int c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }
int isalnum(int c) { return isalpha(c) || isdigit(c); }
int isspace(int c) { return c == ' '  || c == '\t' || c == '\n'
                        || c == '\r' || c == '\f' || c == '\v'; }
int isupper(int c) { return c >= 'A' && c <= 'Z'; }
int islower(int c) { return c >= 'a' && c <= 'z'; }
int isprint(int c) { return c >= 0x20 && c <= 0x7E; }
int isxdigit(int c) { return isdigit(c) || (c>='A'&&c<='F') || (c>='a'&&c<='f'); }
int ispunct(int c) { return isprint(c) && !isalnum(c) && c != ' '; }

int abs(int x) { return x < 0 ? -x : x; }
long labs(long x) { return x < 0 ? -x : x; }

int atoi(const char *s)
{
    int value = 0, neg = 0;
    while (isspace(*s)) s++;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') s++;
    while (isdigit(*s)) { value = value * 10 + (*s - '0'); s++; }
    return neg ? -value : value;
}

long atol(const char *s) { return (long)atoi(s); }

long strtol(const char *s, char **end, int base)
{
    long value = 0;
    int neg = 0;
    while (isspace(*s)) s++;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') s++;
    if (base == 0)
    {
        if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { base = 16; s += 2; }
        else if (s[0] == '0') { base = 8; s++; }
        else base = 10;
    }
    else if (base == 16 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;

    for (;;)
    {
        int digit = -1;
        if (*s >= '0' && *s <= '9') digit = *s - '0';
        else if (*s >= 'a' && *s <= 'f') digit = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'F') digit = *s - 'A' + 10;
        if (digit < 0 || digit >= base) break;
        value = value * base + digit;
        s++;
    }
    if (end) *end = (char*)s;
    return neg ? -value : value;
}

unsigned long strtoul(const char *s, char **end, int base)
{
    return (unsigned long)strtol(s, end, base);
}

static unsigned int rand_seed = 1;
int rand(void)
{
    rand_seed = rand_seed * 1103515245 + 12345;
    return (rand_seed >> 16) & RAND_MAX;
}
void srand(unsigned int seed) { rand_seed = seed; }

#define QSORT_MAX_ELEM_SIZE 256

void qsort(void *base, size_t nmemb, size_t size,
           int (*cmp)(const void *, const void *))
{
    u8 *arr = (u8*)base;
    u8 elem[QSORT_MAX_ELEM_SIZE];
    if (size > QSORT_MAX_ELEM_SIZE) return;

    for (size_t gap = nmemb / 2; gap > 0; gap /= 2)
    {
        for (size_t i = gap; i < nmemb; i++)
        {
            memcpy(elem, arr + i * size, size);
            size_t j = i;
            while (j >= gap && cmp(arr + (j - gap) * size, elem) > 0)
            {
                memcpy(arr + j * size, arr + (j - gap) * size, size);
                j -= gap;
            }
            memcpy(arr + j * size, elem, size);
        }
    }
}

char *getenv(const char *name) { (void)name; return NULL; }

jmp_buf exit_jmp;
int exit_jmp_set = 0;

void exit(int status)
{
    (void)status;
    if (exit_jmp_set)
    {
        dbg_log("[exit] unwinding via longjmp\n");
        longjmp(exit_jmp, 1);
    }
    dbg_log("[exit] called with exit_jmp_set==0 -> HANGING FOREVER\n");
    for (;;) { if (fn_kusleep) NC(gadget, fn_kusleep, 1000000, 0,0,0,0,0); }
}

void abort(void) { exit(1); }

#define MAX_ATEXIT 8

static void (*atexit_fns[MAX_ATEXIT])(void);
static int atexit_count = 0;
int atexit(void (*func)(void))
{
    if (atexit_count < MAX_ATEXIT)
    {
        atexit_fns[atexit_count++] = func;
        return 0;
    }
    return -1;
}

__attribute__((naked))
int setjmp(jmp_buf env)
{
    __asm__ volatile (
        "movq %%rbx, 0(%%rdi)\n\t"
        "movq %%rbp, 8(%%rdi)\n\t"
        "movq %%r12, 16(%%rdi)\n\t"
        "movq %%r13, 24(%%rdi)\n\t"
        "movq %%r14, 32(%%rdi)\n\t"
        "movq %%r15, 40(%%rdi)\n\t"
        "movq %%rsp, 48(%%rdi)\n\t"
        "movq (%%rsp), %%rax\n\t"
        "movq %%rax, 56(%%rdi)\n\t"
        "xorl %%eax, %%eax\n\t"
        "retq" ::: "memory"
    );
}

__attribute__((naked, noreturn))
void longjmp(jmp_buf env, int val)
{
    __asm__ volatile (
        "movl %%esi, %%eax\n\t"
        "testl %%eax, %%eax\n\t"
        "jnz 1f\n\t"
        "incl %%eax\n\t"
        "1:\n\t"
        "movq 0(%%rdi), %%rbx\n\t"
        "movq 8(%%rdi), %%rbp\n\t"
        "movq 16(%%rdi), %%r12\n\t"
        "movq 24(%%rdi), %%r13\n\t"
        "movq 32(%%rdi), %%r14\n\t"
        "movq 40(%%rdi), %%r15\n\t"
        "movq 48(%%rdi), %%rsp\n\t"
        "jmpq *56(%%rdi)" ::: "memory"
    );
}

int errno = 0;

#define MAX_FILES  32
#define FILE_BUFSZ 4096

#define BDIR_IDLE   0
#define BDIR_READS  1
#define BDIR_WRITES 2

#define O_RDONLY 0x0000
#define O_WRONLY 0x0001
#define O_RDWR   0x0002
#define O_APPEND 0x0008
#define O_CREAT  0x0200
#define O_TRUNC  0x0400

static FILE file_slots[MAX_FILES];
static u8   file_bufs[MAX_FILES][FILE_BUFSZ];
static int file_slots_inited = 0;

static int flush_writes(FILE *f)
{
    if (!f->buf || f->buf_dir != BDIR_WRITES || f->buf_pos == 0) return 0;
    int done = 0;
    while (done < f->buf_pos)
    {
        s32 n = (s32)NC(gadget, fn_kwrite, (u64)f->fd,
                        (u64)(f->buf + done), (u64)(f->buf_pos - done), 0, 0, 0);
        if (n <= 0) { f->error = 1; f->buf_pos = 0; return -1; }
        done += n;
    }
    f->buf_pos = 0;
    return 0;
}

static void drop_reads(FILE *f)
{
    if (!f->buf || f->buf_dir != BDIR_READS) return;
    if (f->buf_len > f->buf_pos && fn_klseek)
        NC(gadget, fn_klseek, (u64)f->fd,
           (u64)(s64)(f->buf_pos - f->buf_len), (u64)SEEK_CUR, 0, 0, 0);
    f->buf_pos = f->buf_len = 0;
    f->buf_dir = BDIR_IDLE;
}

static FILE _stdin_f  = { .fd = 0, .eof = 0, .error = 0, .mode = 0 };
static FILE _stdout_f = { .fd = 1, .eof = 0, .error = 0, .mode = 1 };
static FILE _stderr_f = { .fd = 2, .eof = 0, .error = 0, .mode = 1 };

FILE *stdin  = &_stdin_f;
FILE *stdout = &_stdout_f;
FILE *stderr = &_stderr_f;

FILE *fopen(const char *path, const char *mode)
{
    if (!fn_kopen) return NULL;

    if (!file_slots_inited)
    {
        for (int i = 0; i < MAX_FILES; i++) file_slots[i].fd = -1;
        file_slots_inited = 1;
    }

    s32 flags = 0;
    int stream_mode = 0;

    if (mode[0] == 'r')
    {
        flags = O_RDONLY;
        stream_mode = 0;
        if (mode[1] == '+') flags = O_RDWR;
    }
    else if (mode[0] == 'w')
    {
        flags = O_WRONLY | O_CREAT | O_TRUNC;
        stream_mode = 1;
        if (mode[1] == '+') flags = O_RDWR | O_CREAT | O_TRUNC;
    }
    else if (mode[0] == 'a')
    {
        flags = O_WRONLY | O_CREAT | O_APPEND;
        stream_mode = 2;
        if (mode[1] == '+') flags = O_RDWR | O_CREAT | O_APPEND;
    }

    s32 fd = (s32)NC(gadget, fn_kopen, (u64)path, (u64)flags,
                     (u64)0666, 0, 0, 0);
    if (fd < 0) return NULL;

    FILE *f = NULL;
    int slot = -1;
    for (int i = 0; i < MAX_FILES; i++)
    {
        if (file_slots[i].fd == -1)
        {
            f = &file_slots[i];
            slot = i;
            break;
        }
    }
    if (!f)
    {
        NC(gadget, fn_kclose, (u64)fd, 0,0,0,0,0);
        dbg_log("fopen: no free slots!\n");
        return NULL;
    }

    f->fd = fd;
    f->eof = 0;
    f->error = 0;
    f->mode = stream_mode;
    f->buf = file_bufs[slot];
    f->buf_pos = 0;
    f->buf_len = 0;
    f->buf_dir = BDIR_IDLE;
    return f;
}

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *f)
{
    if (!f || !fn_kread) return 0;
    size_t total = size * nmemb;
    if (total == 0) return 0;

    if (!f->buf)
    {
        if (f->eof) return 0;
        size_t done = 0;
        while (done < total)
        {
            s32 got = (s32)NC(gadget, fn_kread, (u64)f->fd,
                              (u64)((u8*)ptr + done),
                              (u64)(total - done), 0,0,0);
            if (got <= 0) { f->eof = 1; break; }
            done += got;
        }
        return done / size;
    }

    if (f->buf_dir == BDIR_WRITES) { flush_writes(f); f->buf_dir = BDIR_IDLE; }

    u8 *dst = (u8 *)ptr;
    size_t done = 0;
    while (done < total)
    {
        if (f->buf_pos >= f->buf_len)
        {
            if (f->eof) break;
            s32 got = (s32)NC(gadget, fn_kread, (u64)f->fd,
                              (u64)f->buf, (u64)FILE_BUFSZ, 0, 0, 0);
            if (got <= 0) { f->eof = 1; break; }
            f->buf_pos = 0;
            f->buf_len = got;
            f->buf_dir = BDIR_READS;
        }
        size_t avail = (size_t)(f->buf_len - f->buf_pos);
        size_t n = total - done;
        if (n > avail) n = avail;
        memcpy(dst + done, f->buf + f->buf_pos, n);
        f->buf_pos += (int)n;
        done += n;
    }
    return done / size;
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *f)
{
    if (!f || !fn_kwrite) return 0;
    size_t total = size * nmemb;
    if (total == 0) return 0;

    if (!f->buf)
    {
        size_t done = 0;
        while (done < total)
        {
            s32 wrote = (s32)NC(gadget, fn_kwrite, (u64)f->fd,
                                (u64)((const u8*)ptr + done),
                                (u64)(total - done), 0,0,0);
            if (wrote <= 0) { f->error = 1; break; }
            done += wrote;
        }
        return done / size;
    }

    drop_reads(f);
    f->buf_dir = BDIR_WRITES;

    const u8 *src = (const u8 *)ptr;
    size_t done = 0;
    while (done < total)
    {
        if (f->buf_pos >= FILE_BUFSZ && flush_writes(f) < 0) break;
        size_t space = (size_t)(FILE_BUFSZ - f->buf_pos);
        size_t n = total - done;
        if (n > space) n = space;
        memcpy(f->buf + f->buf_pos, src + done, n);
        f->buf_pos += (int)n;
        done += n;
    }
    return done / size;
}

int fclose(FILE *f)
{
    if (!f || f->fd < 0) return EOF;
    int err = flush_writes(f);
    NC(gadget, fn_kclose, (u64)f->fd, 0,0,0,0,0);
    f->fd = -1;
    f->buf_pos = f->buf_len = 0;
    f->buf_dir = BDIR_IDLE;
    return err < 0 ? EOF : 0;
}

int fseek(FILE *f, long offset, int whence)
{
    if (!f || !fn_klseek) return -1;

    if (f->buf)
    {
        if (f->buf_dir == BDIR_WRITES)
        {
            flush_writes(f);
        }
        else if (f->buf_dir == BDIR_READS && whence == SEEK_CUR)
        {
            offset -= (long)(f->buf_len - f->buf_pos);
        }
        f->buf_pos = f->buf_len = 0;
        f->buf_dir = BDIR_IDLE;
    }

    s64 new_pos = (s64)NC(gadget, fn_klseek, (u64)f->fd, (u64)offset,
                          (u64)whence, 0, 0, 0);
    if (new_pos < 0) return -1;
    f->eof = 0;
    return 0;
}

long ftell(FILE *f)
{
    if (!f || !fn_klseek) return -1;
    s64 pos = (s64)NC(gadget, fn_klseek, (u64)f->fd, 0, (u64)SEEK_CUR, 0, 0, 0);
    if (pos < 0) return -1;

    if (f->buf)
    {
        if (f->buf_dir == BDIR_WRITES)      pos += f->buf_pos;
        else if (f->buf_dir == BDIR_READS)  pos -= (f->buf_len - f->buf_pos);
    }
    return (long)pos;
}

void rewind(FILE *f)
{
    fseek(f, 0, SEEK_SET);
    if (f)
    {
        f->eof = 0;
        f->error = 0;
    }
}
int feof(FILE *f) { return f ? (f->eof && f->buf_pos >= f->buf_len) : 1; }
int ferror(FILE *f) { return f ? f->error : 1; }
int fflush(FILE *f) { return (f && flush_writes(f) < 0) ? EOF : 0; }

int fputc(int c, FILE *f)
{
    u8 ch = (u8)c;
    return fwrite(&ch, 1, 1, f) == 1 ? c : EOF;
}

int fputs(const char *s, FILE *f)
{
    size_t len = strlen(s);
    return fwrite(s, 1, len, f) == len ? 0 : EOF;
}

char *fgets(char *buf, int n, FILE *f)
{
    if (n <= 0 || !f) return NULL;
    int i = 0;
    while (i < n - 1)
    {
        u8 ch;
        if (fread(&ch, 1, 1, f) != 1)
        {
            if (i == 0) return NULL;
            break;
        }
        buf[i++] = ch;
        if (ch == '\n') break;
    }
    buf[i] = '\0';
    return buf;
}

int fgetc(FILE *f)
{
    u8 ch;
    return (fread(&ch, 1, 1, f) == 1) ? ch : EOF;
}

int ungetc(int c, FILE *f) { (void)c; (void)f; return c; }

int remove(const char *path)
{
    if (!fn_kunlink) return -1;
    return (s32)NC(gadget, fn_kunlink, (u64)path, 0,0,0,0,0);
}

/* no rename export, so copy then unlink */
int rename(const char *oldpath, const char *newpath)
{
    static u8 buf[8192];
    if (!fn_kopen || !fn_kread || !fn_kwrite || !fn_kclose) return -1;

    s32 in_fd = (s32)NC(gadget, fn_kopen, (u64)oldpath, 0, 0, 0, 0, 0);
    if (in_fd < 0) return -1;

    s32 out_fd = (s32)NC(gadget, fn_kopen, (u64)newpath,
                         (u64)(O_WRONLY | O_CREAT | O_TRUNC), (u64)0666,
                         0, 0, 0);
    if (out_fd < 0)
    {
        NC(gadget, fn_kclose, (u64)in_fd, 0,0,0,0,0);
        return -1;
    }

    int ok = 1;
    for (;;)
    {
        s32 n = (s32)NC(gadget, fn_kread, (u64)in_fd, (u64)buf,
                        sizeof(buf), 0,0,0);
        if (n == 0) break;
        if (n < 0) { ok = 0; break; }
        if ((s32)NC(gadget, fn_kwrite, (u64)out_fd, (u64)buf, (u64)n,
                    0,0,0) != n)
        {
            ok = 0;
            break;
        }
    }

    NC(gadget, fn_kclose, (u64)in_fd, 0,0,0,0,0);
    NC(gadget, fn_kclose, (u64)out_fd, 0,0,0,0,0);

    if (ok && fn_kunlink) NC(gadget, fn_kunlink, (u64)oldpath, 0,0,0,0,0);
    return ok ? 0 : -1;
}

#define PRINTF_BUFSZ 512
#define SPRINTF_MAX  4096

static int fmt_int(char *buf, int max, long val, int base, int is_upper,
                   int width, int zero_pad)
{
    char rev_digits[24];
    int neg = 0, ndigits = 0;
    unsigned long magnitude;

    if (val < 0 && base == 10) { neg = 1; magnitude = -val; }
    else magnitude = (unsigned long)val;

    if (magnitude == 0) rev_digits[ndigits++] = '0';
    else
    {
        const char *digits = is_upper ? "0123456789ABCDEF" : "0123456789abcdef";
        while (magnitude)
        {
            rev_digits[ndigits++] = digits[magnitude % base];
            magnitude /= base;
        }
    }

    int total = ndigits + neg;
    int pad = (width > total) ? width - total : 0;
    int out = 0;
    char pad_char = zero_pad ? '0' : ' ';

    if (!zero_pad)
    {
        for (int i = 0; i < pad && out < max; i++) buf[out++] = pad_char;
    }
    if (neg && out < max) buf[out++] = '-';
    if (zero_pad)
    {
        for (int i = 0; i < pad && out < max; i++) buf[out++] = pad_char;
    }
    for (int i = ndigits - 1; i >= 0 && out < max; i--)
        buf[out++] = rev_digits[i];
    return out;
}

static int fmt_uint(char *buf, int max, unsigned long val, int base,
                    int is_upper, int width, int zero_pad)
{
    char rev_digits[24];
    int ndigits = 0;
    if (val == 0) rev_digits[ndigits++] = '0';
    else
    {
        const char *digits = is_upper ? "0123456789ABCDEF" : "0123456789abcdef";
        while (val)
        {
            rev_digits[ndigits++] = digits[val % base];
            val /= base;
        }
    }
    int pad = (width > ndigits) ? width - ndigits : 0;
    int out = 0;
    char pad_char = zero_pad ? '0' : ' ';
    for (int i = 0; i < pad && out < max; i++) buf[out++] = pad_char;
    for (int i = ndigits - 1; i >= 0 && out < max; i--)
        buf[out++] = rev_digits[i];
    return out;
}

int vsnprintf(char *buf, size_t n, const char *fmt, va_list ap)
{
    int out = 0;
    int max = (int)n - 1;
    if (max < 0) max = 0;

    while (*fmt && out < max)
    {
        if (*fmt != '%') { buf[out++] = *fmt++; continue; }
        fmt++;

        int zero_pad = 0, left_align = 0;
        while (*fmt == '0' || *fmt == '-')
        {
            if (*fmt == '0') zero_pad = 1;
            if (*fmt == '-') left_align = 1;
            fmt++;
        }
        (void)left_align;

        int width = 0;
        while (*fmt >= '0' && *fmt <= '9')
        {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }

        int precision = -1;
        if (*fmt == '.')
        {
            fmt++;
            precision = 0;
            while (*fmt >= '0' && *fmt <= '9')
            {
                precision = precision * 10 + (*fmt - '0');
                fmt++;
            }
        }

        int is_long = 0;
        if (*fmt == 'l')
        {
            is_long = 1;
            fmt++;
            if (*fmt == 'l') { is_long = 2; fmt++; }
        }
        else if (*fmt == 'h')
        {
            fmt++;
            if (*fmt == 'h') fmt++;
        }
        else if (*fmt == 'z')
        {
            is_long = 1;
            fmt++;
        }

        switch (*fmt)
        {
        case 'd': case 'i':
        {
            long val = is_long ? va_arg(ap, long) : (long)va_arg(ap, int);
            int pad_width = width, pad_zeros = zero_pad;
            if (precision >= 0) { pad_width = precision; pad_zeros = 1; }
            out += fmt_int(buf + out, max - out, val, 10, 0,
                           pad_width, pad_zeros);
            break;
        }
        case 'u':
        {
            unsigned long val = is_long
                ? va_arg(ap, unsigned long)
                : (unsigned long)va_arg(ap, unsigned int);
            int pad_width = width, pad_zeros = zero_pad;
            if (precision >= 0) { pad_width = precision; pad_zeros = 1; }
            out += fmt_uint(buf + out, max - out, val, 10, 0,
                            pad_width, pad_zeros);
            break;
        }
        case 'x':
        {
            unsigned long val = is_long
                ? va_arg(ap, unsigned long)
                : (unsigned long)va_arg(ap, unsigned int);
            int pad_width = width, pad_zeros = zero_pad;
            if (precision >= 0) { pad_width = precision; pad_zeros = 1; }
            out += fmt_uint(buf + out, max - out, val, 16, 0,
                            pad_width, pad_zeros);
            break;
        }
        case 'X':
        {
            unsigned long val = is_long
                ? va_arg(ap, unsigned long)
                : (unsigned long)va_arg(ap, unsigned int);
            int pad_width = width, pad_zeros = zero_pad;
            if (precision >= 0) { pad_width = precision; pad_zeros = 1; }
            out += fmt_uint(buf + out, max - out, val, 16, 1,
                            pad_width, pad_zeros);
            break;
        }
        case 'o':
        {
            unsigned long val = is_long
                ? va_arg(ap, unsigned long)
                : (unsigned long)va_arg(ap, unsigned int);
            int pad_width = width, pad_zeros = zero_pad;
            if (precision >= 0) { pad_width = precision; pad_zeros = 1; }
            out += fmt_uint(buf + out, max - out, val, 8, 0,
                            pad_width, pad_zeros);
            break;
        }
        case 's':
        {
            const char *s = va_arg(ap, const char*);
            if (!s) s = "(null)";
            int len = 0;
            while (s[len]) len++;
            int limit = (precision >= 0 && precision < len) ? precision : len;
            for (int i = 0; i < limit && out < max; i++) buf[out++] = s[i];
            break;
        }
        case 'c':
        {
            int c = va_arg(ap, int);
            if (out < max) buf[out++] = (char)c;
            break;
        }
        case 'p':
        {
            unsigned long val = (unsigned long)va_arg(ap, void*);
            if (out < max) buf[out++] = '0';
            if (out < max) buf[out++] = 'x';
            out += fmt_uint(buf + out, max - out, val, 16, 0, 0, 0);
            break;
        }
        case '%':
            if (out < max) buf[out++] = '%';
            break;
        case '\0':
            goto done;
        default:
            if (out < max) buf[out++] = '%';
            if (out < max) buf[out++] = *fmt;
            break;
        }
        fmt++;
    }
done:
    if (n > 0) buf[out < (int)n ? out : (int)n - 1] = '\0';
    return out;
}

int snprintf(char *buf, size_t n, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int len = vsnprintf(buf, n, fmt, ap);
    va_end(ap);
    return len;
}

int sprintf(char *buf, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int len = vsnprintf(buf, SPRINTF_MAX, fmt, ap);
    va_end(ap);
    return len;
}

int vfprintf(FILE *f, const char *fmt, va_list ap)
{
    char buf[PRINTF_BUFSZ];
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    if (n > 0)
    {
        if (f == stderr || f == stdout) dbg_log(buf);
        if (f && f->fd >= 0) fwrite(buf, 1, n, f);
    }
    return n;
}

int vprintf(const char *fmt, va_list ap)
{
    char buf[PRINTF_BUFSZ];
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    dbg_log(buf);
    return n;
}

int printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vprintf(fmt, ap);
    va_end(ap);
    return n;
}

int fprintf(FILE *f, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vfprintf(f, fmt, ap);
    va_end(ap);
    return n;
}

int putchar(int c)
{
    char ch = (char)c;
    dbg_log(&ch);
    return c;
}

int puts(const char *s)
{
    dbg_log(s);
    dbg_log("\n");
    return 0;
}

int sscanf(const char *str, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int matched = 0;

    while (*fmt && *str)
    {
        if (isspace(*fmt)) { fmt++; while (isspace(*str)) str++; continue; }
        if (*fmt != '%')
        {
            if (*fmt != *str) break;
            fmt++; str++; continue;
        }
        fmt++;

        while (*fmt >= '0' && *fmt <= '9') fmt++;

        switch (*fmt)
        {
        case 'd': case 'i':
        {
            int *dest = va_arg(ap, int*);
            while (isspace(*str)) str++;
            int neg = 0, val = 0, got_digits = 0;
            if (*str == '-') { neg = 1; str++; }
            else if (*str == '+') str++;
            while (isdigit(*str))
            {
                val = val * 10 + (*str - '0');
                str++;
                got_digits = 1;
            }
            if (!got_digits) goto end;
            *dest = neg ? -val : val;
            matched++;
            break;
        }
        case 'x': case 'X':
        {
            unsigned int *dest = va_arg(ap, unsigned int*);
            while (isspace(*str)) str++;
            if (str[0]=='0' && (str[1]=='x'||str[1]=='X')) str += 2;
            unsigned int val = 0;
            int got_digits = 0;
            for (;;)
            {
                int digit = -1;
                if (*str >= '0' && *str <= '9') digit = *str - '0';
                else if (*str >= 'a' && *str <= 'f') digit = *str - 'a' + 10;
                else if (*str >= 'A' && *str <= 'F') digit = *str - 'A' + 10;
                if (digit < 0) break;
                val = val * 16 + digit;
                str++;
                got_digits = 1;
            }
            if (!got_digits) goto end;
            *dest = val;
            matched++;
            break;
        }
        case 's':
        {
            char *dest = va_arg(ap, char*);
            while (isspace(*str)) str++;
            while (*str && !isspace(*str)) *dest++ = *str++;
            *dest = '\0';
            matched++;
            break;
        }
        case 'c':
        {
            char *dest = va_arg(ap, char*);
            *dest = *str++;
            matched++;
            break;
        }
        default: goto end;
        }
        fmt++;
    }
end:
    va_end(ap);
    return matched;
}

#define TIME_STEP_SECONDS 60

static u32 time_counter = 0;
time_t time(time_t *t)
{
    time_counter += TIME_STEP_SECONDS;
    if (t) *t = (time_t)time_counter;
    return (time_t)time_counter;
}

static struct tm tm_buf;
struct tm *localtime(const time_t *t)
{
    (void)t;
    memset(&tm_buf, 0, sizeof(tm_buf));
    return &tm_buf;
}

sighandler_t signal(int signum, sighandler_t handler)
{
    (void)signum; (void)handler;
    return SIG_DFL;
}

int access(const char *path, int mode)
{
    (void)mode;
    if (!fn_kopen) return -1;
    s32 fd = (s32)NC(gadget, fn_kopen, (u64)path, 0, 0, 0, 0, 0);
    if (fd < 0) return -1;
    NC(gadget, fn_kclose, (u64)fd, 0,0,0,0,0);
    return 0;
}

void __stack_chk_fail(void) { abort(); }
void *__stack_chk_guard = (void*)0xDEADBEEF;

/* GCC redirects printf, memcpy and sscanf to these __*_chk and __isoc99_ forms
   as soon as a system header is seen, even under -ffreestanding. */
int __printf_chk(int flag, const char *fmt, ...)
{
    (void)flag;
    va_list ap;
    va_start(ap, fmt);
    char buf[PRINTF_BUFSZ];
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    dbg_log(buf);
    return n;
}

int __fprintf_chk(FILE *f, int flag, const char *fmt, ...)
{
    (void)flag;
    va_list ap;
    va_start(ap, fmt);
    int n = vfprintf(f, fmt, ap);
    va_end(ap);
    return n;
}

int __vfprintf_chk(FILE *f, int flag, const char *fmt, va_list ap)
{
    (void)flag;
    return vfprintf(f, fmt, ap);
}

int __snprintf_chk(char *buf, size_t maxlen, int flag, size_t slen,
                   const char *fmt, ...)
{
    (void)flag; (void)slen;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, maxlen, fmt, ap);
    va_end(ap);
    return n;
}

int __vsnprintf_chk(char *buf, size_t maxlen, int flag, size_t slen,
                    const char *fmt, va_list ap)
{
    (void)flag; (void)slen;
    return vsnprintf(buf, maxlen, fmt, ap);
}

void *__memcpy_chk(void *dst, const void *src, size_t n, size_t dstlen)
{
    (void)dstlen;
    return memcpy(dst, src, n);
}

void *__memset_chk(void *s, int c, size_t n, size_t slen)
{
    (void)slen;
    return memset(s, c, n);
}

int __isoc99_sscanf(const char *str, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int matched = 0;
    const char *input = str;
    const char *format = fmt;

    while (*format && *input)
    {
        if (isspace(*format))
        {
            format++;
            while (isspace(*input)) input++;
            continue;
        }
        if (*format != '%')
        {
            if (*format != *input) break;
            format++; input++; continue;
        }
        format++;
        while (*format >= '0' && *format <= '9') format++;
        switch (*format)
        {
        case 'd': case 'i':
        {
            int *dest = va_arg(ap, int*);
            while (isspace(*input)) input++;
            int neg = 0, val = 0, got_digits = 0;
            if (*input == '-') { neg = 1; input++; }
            else if (*input == '+') input++;
            while (*input >= '0' && *input <= '9')
            {
                val = val * 10 + (*input - '0');
                input++;
                got_digits = 1;
            }
            if (!got_digits) goto done;
            *dest = neg ? -val : val;
            matched++;
            break;
        }
        case 'x': case 'X':
        {
            unsigned int *dest = va_arg(ap, unsigned int*);
            while (isspace(*input)) input++;
            if (input[0]=='0' && (input[1]=='x'||input[1]=='X')) input += 2;
            unsigned int val = 0;
            int got_digits = 0;
            for (;;)
            {
                int digit = -1;
                if (*input >= '0' && *input <= '9') digit = *input - '0';
                else if (*input >= 'a' && *input <= 'f') digit = *input - 'a' + 10;
                else if (*input >= 'A' && *input <= 'F') digit = *input - 'A' + 10;
                if (digit < 0) break;
                val = val * 16 + digit;
                input++;
                got_digits = 1;
            }
            if (!got_digits) goto done;
            *dest = val;
            matched++;
            break;
        }
        case 's':
        {
            char *dest = va_arg(ap, char*);
            while (isspace(*input)) input++;
            while (*input && !isspace(*input)) *dest++ = *input++;
            *dest = '\0';
            matched++;
            break;
        }
        case 'c':
        {
            char *dest = va_arg(ap, char*);
            *dest = *input++;
            matched++;
            break;
        }
        default: goto done;
        }
        format++;
    }
done:
    va_end(ap);
    return matched;
}

int *__errno_location(void)
{
    return &errno;
}

#define CTYPE_TABLE_SIZE 384
#define CTYPE_TABLE_BIAS 128

static int toupper_table[CTYPE_TABLE_SIZE];
static const int *toupper_table_ptr;
static int toupper_table_inited = 0;

static void init_toupper_table(void)
{
    if (toupper_table_inited) return;
    for (int i = 0; i < CTYPE_TABLE_SIZE; i++)
    {
        int c = i - CTYPE_TABLE_BIAS;
        if (c >= 'a' && c <= 'z') toupper_table[i] = c - 32;
        else toupper_table[i] = c;
    }
    toupper_table_ptr = toupper_table + CTYPE_TABLE_BIAS;
    toupper_table_inited = 1;
}

const int **__ctype_toupper_loc(void)
{
    init_toupper_table();
    return (const int **)&toupper_table_ptr;
}

#define CT_UPPER  0x0100
#define CT_LOWER  0x0200
#define CT_ALPHA  0x0400
#define CT_DIGIT  0x0800
#define CT_XDIGIT 0x1000
#define CT_SPACE  0x2000
#define CT_PRINT  0x4000
#define CT_GRAPH  0x8000
#define CT_BLANK  0x0001
#define CT_CNTRL  0x0002
#define CT_PUNCT  0x0004
#define CT_ALNUM  0x0008

static unsigned short ctype_table[CTYPE_TABLE_SIZE];
static const unsigned short *ctype_table_ptr;
static int ctype_table_inited = 0;

static void init_ctype_table(void)
{
    if (ctype_table_inited) return;
    for (int i = 0; i < CTYPE_TABLE_SIZE; i++)
    {
        int c = i - CTYPE_TABLE_BIAS;
        unsigned short flags = 0;

        if (c >= 0 && c <= 127)
        {
            if (c >= 'A' && c <= 'Z') flags |= CT_UPPER | CT_ALPHA;
            if (c >= 'a' && c <= 'z') flags |= CT_LOWER | CT_ALPHA;
            if (c >= '0' && c <= '9') flags |= CT_DIGIT | CT_XDIGIT;
            if ((c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))
                flags |= CT_XDIGIT;
            if (c == ' ' || (c >= '\t' && c <= '\r')) flags |= CT_SPACE;
            if (c == ' ' || c == '\t') flags |= CT_BLANK;
            if (c < ' ' || c == 127) flags |= CT_CNTRL;
            if (c >= ' ' && c < 127) flags |= CT_PRINT;
            if (c > ' ' && c < 127) flags |= CT_GRAPH;
            if (flags & (CT_ALPHA | CT_DIGIT)) flags |= CT_ALNUM;
            if ((flags & CT_GRAPH) && !(flags & CT_ALNUM)) flags |= CT_PUNCT;
        }

        ctype_table[i] = flags;
    }
    ctype_table_ptr = ctype_table + CTYPE_TABLE_BIAS;
    ctype_table_inited = 1;
}

const unsigned short **__ctype_b_loc(void)
{
    init_ctype_table();
    return (const unsigned short **)&ctype_table_ptr;
}

int system(const char *cmd)
{
    (void)cmd;
    return -1;
}

double atof(const char *s)
{
    double val = 0.0, frac = 0.0, div = 1.0;
    int neg = 0;
    while (isspace(*s)) s++;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') { val = val * 10.0 + (*s - '0'); s++; }
    if (*s == '.')
    {
        s++;
        while (*s >= '0' && *s <= '9')
        {
            frac = frac * 10.0 + (*s - '0');
            div *= 10.0;
            s++;
        }
        val += frac / div;
    }
    return neg ? -val : val;
}

double strtod(const char *s, char **endptr)
{
    const char *start = s;
    double val = atof(s);
    while (isspace(*s)) s++;
    if (*s == '-' || *s == '+') s++;
    while (*s >= '0' && *s <= '9') s++;
    if (*s == '.') { s++; while (*s >= '0' && *s <= '9') s++; }
    if (endptr) *endptr = (s == start) ? (char *)start : (char *)s;
    return val;
}

double fabs(double x) { return x < 0 ? -x : x; }

int mkdir(const char *path, unsigned int mode)
{
    if (!fn_kmkdir) return -1;
    return (s32)NC(gadget, fn_kmkdir, (u64)path, (u64)mode, 0, 0, 0, 0);
}
