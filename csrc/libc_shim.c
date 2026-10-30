// SPDX-License-Identifier: GPL-2.0-or-later
//
// The slice of the C library doomgeneric needs.
//
// KeyOS has no libc, and newlib's prebuilt libc.a is not position independent,
// which the PIE KeyOS target requires. The engine is still compiled against
// newlib's headers, so on device this file provides the symbols those headers
// expect (_impure_ptr, _ctype_, __errno) plus every libc function the engine
// calls. Memory, files, console output and exit are forwarded to src/libc.rs.
//
// The simulator builds this file too, with DG_HOSTED and csrc/dg_libc.h
// renaming the functions to shim_*, so the engine exercises the same code
// there. Memory, exit and the newlib globals then come from the host libc.

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifndef DG_HOSTED
#include <reent.h>
#endif

// Implemented in src/libc.rs.
extern void dg_console_write(const char *buf, size_t len);
extern int dg_file_open(const char *path, int mode);
extern size_t dg_file_read(int handle, void *buf, size_t len);
extern size_t dg_file_write(int handle, const void *buf, size_t len);
extern long dg_file_seek(int handle, long offset, int whence);
extern int dg_file_close(int handle);
extern int dg_file_remove(const char *path);
extern int dg_file_rename(const char *from, const char *to);
extern int dg_mkdir(const char *path);
extern void dg_exit(int status) __attribute__((noreturn));

enum { MODE_READ, MODE_WRITE, MODE_APPEND, MODE_READ_WRITE };

// What a FILE * from fopen points to. The engine only passes it back to us.
struct shim_file
{
    int handle;
    int eof;
    int pushback;   // a character fscanf read ahead, or -1
};

#define HANDLE(f) (((struct shim_file *) (f))->handle)

#ifndef DG_HOSTED

// --- newlib globals --------------------------------------------------------

static FILE console_out = { ._file = 1 };
static FILE console_err = { ._file = 2 };
static struct _reent reent = { ._stdout = &console_out, ._stderr = &console_err };
struct _reent *_impure_ptr = &reent;

static int errno_value;

int *__errno(void)
{
    return &errno_value;
}

// newlib's ctype table: entry 0 is EOF, entry c + 1 describes character c.
#define CT(c) ((char) ( \
    (((c) < 32 || (c) == 127) ? _C : 0) | \
    (((c) >= 9 && (c) <= 13) ? _S : 0) | \
    ((c) == ' ' ? (_S | _B) : 0) | \
    (((c) >= '0' && (c) <= '9') ? _N : 0) | \
    (((c) >= 'A' && (c) <= 'Z') ? _U : 0) | \
    (((c) >= 'a' && (c) <= 'z') ? _L : 0) | \
    ((((c) >= 'A' && (c) <= 'F') || ((c) >= 'a' && (c) <= 'f')) ? _X : 0) | \
    ((((c) > 32 && (c) < 48) || ((c) > 57 && (c) < 65) || ((c) > 90 && (c) < 97) \
      || ((c) > 122 && (c) < 127)) ? _P : 0)))
#define CT4(c) CT(c), CT((c) + 1), CT((c) + 2), CT((c) + 3)
#define CT16(c) CT4(c), CT4((c) + 4), CT4((c) + 8), CT4((c) + 12)

const char _ctype_[1 + 256] = {
    0,
    CT16(0), CT16(16), CT16(32), CT16(48), CT16(64), CT16(80), CT16(96), CT16(112),
};

int system(const char *command)
{
    (void) command;
    return -1;
}

int abs(int x)
{
    return x < 0 ? -x : x;
}

#endif // !DG_HOSTED

// --- process ---------------------------------------------------------------

void exit(int status)
{
    fflush(stdout);
    dg_exit(status);
}

// --- strings ---------------------------------------------------------------

size_t strlen(const char *s)
{
    const char *p = s;
    while (*p)
        p++;
    return p - s;
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b)
        a++, b++;
    return (unsigned char) *a - (unsigned char) *b;
}

int strncmp(const char *a, const char *b, size_t n)
{
    for (; n > 0; n--, a++, b++)
    {
        if (*a != *b || *a == '\0')
            return (unsigned char) *a - (unsigned char) *b;
    }
    return 0;
}

int strcasecmp(const char *a, const char *b)
{
    int ca, cb;
    do
    {
        ca = tolower((unsigned char) *a++);
        cb = tolower((unsigned char) *b++);
    } while (ca && ca == cb);
    return ca - cb;
}

int strncasecmp(const char *a, const char *b, size_t n)
{
    int ca = 0, cb = 0;
    while (n-- > 0)
    {
        ca = tolower((unsigned char) *a++);
        cb = tolower((unsigned char) *b++);
        if (!ca || ca != cb)
            break;
    }
    return ca - cb;
}

char *strncpy(char *dst, const char *src, size_t n)
{
    size_t i = 0;
    for (; i < n && src[i]; i++)
        dst[i] = src[i];
    for (; i < n; i++)
        dst[i] = '\0';
    return dst;
}

char *strchr(const char *s, int c)
{
    for (;; s++)
    {
        if (*s == (char) c)
            return (char *) s;
        if (!*s)
            return NULL;
    }
}

char *strrchr(const char *s, int c)
{
    const char *found = NULL;
    for (;; s++)
    {
        if (*s == (char) c)
            found = s;
        if (!*s)
            return (char *) found;
    }
}

char *strstr(const char *haystack, const char *needle)
{
    size_t n = strlen(needle);
    for (; *haystack; haystack++)
    {
        if (!strncmp(haystack, needle, n))
            return (char *) haystack;
    }
    return n == 0 ? (char *) haystack : NULL;
}

char *strdup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *copy = malloc(n);
    if (copy)
        memcpy(copy, s, n);
    return copy;
}

// --- number parsing --------------------------------------------------------

// Parses an integer in `base` (0 picks it from a 0x / 0 prefix like %i).
// Returns the number of characters consumed, 0 if none.
static int parse_int(const char *s, int base, long *out)
{
    const char *p = s;
    int neg = 0;
    long value = 0;
    int digits = 0;

    if (*p == '+' || *p == '-')
        neg = *p++ == '-';
    if (base == 0)
    {
        if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X'))
            base = 16, p += 2;
        else if (p[0] == '0')
            base = 8;
        else
            base = 10;
    }
    else if (base == 16 && p[0] == '0' && (p[1] == 'x' || p[1] == 'X'))
    {
        p += 2;
    }
    for (;; p++, digits++)
    {
        int d;
        if (*p >= '0' && *p <= '9')
            d = *p - '0';
        else if (*p >= 'a' && *p <= 'z')
            d = *p - 'a' + 10;
        else if (*p >= 'A' && *p <= 'Z')
            d = *p - 'A' + 10;
        else
            break;
        if (d >= base)
            break;
        value = value * base + d;
    }
    if (digits == 0)
        return 0;
    *out = neg ? -value : value;
    return p - s;
}

int atoi(const char *s)
{
    long value = 0;
    while (isspace((unsigned char) *s))
        s++;
    parse_int(s, 10, &value);
    return (int) value;
}

double atof(const char *s)
{
    double value = 0, scale = 1;
    int neg = 0;

    while (isspace((unsigned char) *s))
        s++;
    if (*s == '+' || *s == '-')
        neg = *s++ == '-';
    for (; *s >= '0' && *s <= '9'; s++)
        value = value * 10 + (*s - '0');
    if (*s == '.')
    {
        for (s++; *s >= '0' && *s <= '9'; s++)
        {
            scale /= 10;
            value += (*s - '0') * scale;
        }
    }
    return neg ? -value : value;
}

// --- scanning --------------------------------------------------------------

// Characters come from a string (sscanf) or a FILE (fscanf).
struct scan_src
{
    const char *str;
    FILE *file;
};

static int src_get(struct scan_src *src);
static void src_unget(struct scan_src *src, int c);

static int src_peek(struct scan_src *src)
{
    int c = src_get(src);
    src_unget(src, c);
    return c;
}

static int is_space(int c)
{
    return c == ' ' || (c >= '\t' && c <= '\r');
}

// Reads an integer in `base` (0 picks it from a 0x / 0 prefix like %i).
// Returns 0 if no digits were read.
static int scan_int(struct scan_src *src, int base, long *out)
{
    long value = 0;
    int neg = 0, digits = 0, c = src_get(src);

    if (c == '+' || c == '-')
    {
        neg = c == '-';
        c = src_get(src);
    }
    if ((base == 0 || base == 16) && c == '0')
    {
        int next = src_get(src);
        if (next == 'x' || next == 'X')
        {
            base = 16;
            c = src_get(src);
        }
        else
        {
            src_unget(src, next);
            if (base == 0)
                base = 8;
        }
    }
    if (base == 0)
        base = 10;
    for (;; c = src_get(src), digits++)
    {
        int d;
        if (c >= '0' && c <= '9')
            d = c - '0';
        else if (c >= 'a' && c <= 'z')
            d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'Z')
            d = c - 'A' + 10;
        else
            break;
        if (d >= base)
            break;
        value = value * base + d;
    }
    src_unget(src, c);
    if (digits == 0)
        return 0;
    *out = neg ? -value : value;
    return 1;
}

// Supports what the engine uses: whitespace, literal characters, the integer
// conversions %d %i %u %x %o into int pointers, and %s / %[...] with an
// optional width (m_config.c reads "%79s %99[^\n]\n").
static int vscan(struct scan_src *src, const char *fmt, va_list ap)
{
    int assigned = 0;
    int input_failure = 0;

    while (*fmt)
    {
        if (is_space((unsigned char) *fmt))
        {
            while (is_space(src_peek(src)))
                src_get(src);
            fmt++;
            continue;
        }
        if (*fmt != '%')
        {
            int c = src_get(src);
            if (c != (unsigned char) *fmt)
            {
                src_unget(src, c);
                input_failure = c == EOF;
                break;
            }
            fmt++;
            continue;
        }

        fmt++;
        int width = 0;
        while (*fmt >= '0' && *fmt <= '9')
            width = width * 10 + (*fmt++ - '0');

        if (*fmt == 's' || *fmt == '[')
        {
            char set[256] = { 0 };
            int negate = 0, n = 0, c;
            char *out = va_arg(ap, char *);

            if (*fmt == '[')
            {
                fmt++;
                if (*fmt == '^')
                    negate = 1, fmt++;
                if (*fmt == ']')
                    set[(unsigned char) *fmt++] = 1;
                while (*fmt && *fmt != ']')
                    set[(unsigned char) *fmt++] = 1;
            }
            else
            {
                while (is_space(src_peek(src)))
                    src_get(src);
            }
            if (*fmt)
                fmt++;
            if (width == 0)
                width = INT_MAX;
            for (c = src_get(src); c != EOF && n < width; c = src_get(src))
            {
                if (fmt[-1] == 's' ? is_space(c) : (set[c] ? negate : !negate))
                    break;
                out[n++] = (char) c;
            }
            src_unget(src, c);
            if (n == 0)
            {
                input_failure = c == EOF;
                break;
            }
            out[n] = '\0';
            assigned++;
            continue;
        }

        int base;
        long value;
        switch (*fmt++)
        {
            case 'd': case 'u': base = 10; break;
            case 'i': base = 0; break;
            case 'x': case 'X': base = 16; break;
            case 'o': base = 8; break;
            default: goto done;
        }
        while (is_space(src_peek(src)))
            src_get(src);
        if (src_peek(src) == EOF)
        {
            input_failure = 1;
            break;
        }
        if (!scan_int(src, base, &value))
            break;
        *va_arg(ap, int *) = (int) value;
        assigned++;
    }
done:
    // Running out of input before the first conversion is EOF, not 0.
    if (assigned == 0 && input_failure)
        return EOF;
    return assigned;
}

int sscanf(const char *str, const char *fmt, ...)
{
    struct scan_src src = { str, NULL };
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vscan(&src, fmt, ap);
    va_end(ap);
    return n;
}

// --- formatted output ------------------------------------------------------

struct out
{
    char *buf;
    size_t cap;
    size_t len;
};

static void out_char(struct out *o, char c)
{
    if (o->len + 1 < o->cap)
        o->buf[o->len] = c;
    o->len++;
}

static void out_padded(struct out *o, const char *s, size_t n, int width, int left, char pad)
{
    int fill = width > (int) n ? width - (int) n : 0;

    if (!left)
        while (fill-- > 0)
            out_char(o, pad);
    while (n--)
        out_char(o, *s++);
    if (left)
        while (fill-- > 0)
            out_char(o, ' ');
}

static void out_number(struct out *o, unsigned long long value, int neg, int base,
                       int upper, int width, int precision, int left, char pad)
{
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char tmp[32];
    int n = 0;

    do
    {
        tmp[sizeof(tmp) - 1 - n++] = digits[value % base];
        value /= base;
    } while (value);
    while (n < precision && n < (int) sizeof(tmp) - 1)
        tmp[sizeof(tmp) - 1 - n++] = '0';
    if (neg)
    {
        if (pad == '0' && width > 0)
        {
            out_char(o, '-');
            width--;
        }
        else
        {
            tmp[sizeof(tmp) - 1 - n++] = '-';
        }
    }
    out_padded(o, tmp + sizeof(tmp) - n, n, width, left, pad);
}

static void out_float(struct out *o, double value, int width, int precision, int left)
{
    char tmp[48];
    int n = 0;
    unsigned long long whole;
    double frac;
    int i;

    if (value != value)
    {
        out_padded(o, "nan", 3, width, left, ' ');
        return;
    }
    if (value > 1e18 || value < -1e18)
    {
        out_padded(o, value < 0 ? "-inf" : "inf", value < 0 ? 4 : 3, width, left, ' ');
        return;
    }
    if (precision < 0)
        precision = 6;
    if (precision > 9)
        precision = 9;
    if (value < 0)
    {
        tmp[n++] = '-';
        value = -value;
    }
    for (i = 0, frac = 0.5; i < precision; i++)
        frac /= 10;
    value += frac;
    whole = (unsigned long long) value;
    frac = value - (double) whole;
    {
        char digits[24];
        int d = 0;
        do
        {
            digits[d++] = '0' + whole % 10;
            whole /= 10;
        } while (whole && d < (int) sizeof(digits));
        while (d)
            tmp[n++] = digits[--d];
    }
    if (precision > 0)
    {
        tmp[n++] = '.';
        for (i = 0; i < precision; i++)
        {
            frac *= 10;
            tmp[n++] = '0' + (int) frac;
            frac -= (int) frac;
        }
    }
    out_padded(o, tmp, n, width, left, ' ');
}

int vsnprintf(char *buf, size_t cap, const char *fmt, va_list ap)
{
    struct out o = { buf, cap, 0 };

    for (; *fmt; fmt++)
    {
        int left = 0, width = 0, precision = -1, longs = 0;
        char pad = ' ';

        if (*fmt != '%')
        {
            out_char(&o, *fmt);
            continue;
        }
        fmt++;
        for (;; fmt++)
        {
            if (*fmt == '-')
                left = 1;
            else if (*fmt == '0')
                pad = '0';
            else if (*fmt != '+' && *fmt != ' ' && *fmt != '#')
                break;
        }
        if (*fmt == '*')
        {
            width = va_arg(ap, int);
            fmt++;
        }
        for (; *fmt >= '0' && *fmt <= '9'; fmt++)
            width = width * 10 + (*fmt - '0');
        if (*fmt == '.')
        {
            precision = 0;
            fmt++;
            if (*fmt == '*')
            {
                precision = va_arg(ap, int);
                fmt++;
            }
            for (; *fmt >= '0' && *fmt <= '9'; fmt++)
                precision = precision * 10 + (*fmt - '0');
        }
        for (;; fmt++)
        {
            if (*fmt == 'l')
                longs++;
            else if (*fmt == 'j')
                longs = 2; // intmax_t is 64-bit everywhere we run
            else if (*fmt != 'h' && *fmt != 'z' && *fmt != 't')
                break;
        }
        if (left)
            pad = ' ';

        switch (*fmt)
        {
            case 'd':
            case 'i':
            {
                long long v = longs >= 2 ? va_arg(ap, long long)
                            : longs == 1 ? va_arg(ap, long) : va_arg(ap, int);
                out_number(&o, v < 0 ? -(unsigned long long) v : (unsigned long long) v,
                           v < 0, 10, 0, width, precision, left, pad);
                break;
            }
            case 'u':
            case 'x':
            case 'X':
            case 'o':
            {
                unsigned long long v = longs >= 2 ? va_arg(ap, unsigned long long)
                                     : longs == 1 ? va_arg(ap, unsigned long)
                                     : va_arg(ap, unsigned int);
                int base = *fmt == 'u' ? 10 : *fmt == 'o' ? 8 : 16;
                out_number(&o, v, 0, base, *fmt == 'X', width, precision, left, pad);
                break;
            }
            case 'p':
                out_char(&o, '0');
                out_char(&o, 'x');
                out_number(&o, (uintptr_t) va_arg(ap, void *), 0, 16, 0, 0, 0, 0, ' ');
                break;
            case 'c':
            {
                char c = (char) va_arg(ap, int);
                out_padded(&o, &c, 1, width, left, ' ');
                break;
            }
            case 's':
            {
                const char *s = va_arg(ap, const char *);
                size_t n;
                if (!s)
                    s = "(null)";
                n = strlen(s);
                if (precision >= 0 && (size_t) precision < n)
                    n = precision;
                out_padded(&o, s, n, width, left, ' ');
                break;
            }
            case 'f':
            case 'g':
            case 'e':
                out_float(&o, va_arg(ap, double), width, precision, left);
                break;
            case '%':
                out_char(&o, '%');
                break;
            case '\0':
                fmt--;
                break;
            default:
                out_char(&o, '%');
                out_char(&o, *fmt);
                break;
        }
    }
    if (cap > 0)
        buf[o.len < cap ? o.len : cap - 1] = '\0';
    return (int) o.len;
}

int snprintf(char *buf, size_t cap, const char *fmt, ...)
{
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(buf, cap, fmt, ap);
    va_end(ap);
    return n;
}

// --- stdio -----------------------------------------------------------------

static int is_console(FILE *f)
{
    return f == stdout || f == stderr;
}

FILE *fopen(const char *path, const char *mode)
{
    int m = mode[0] == 'w' ? MODE_WRITE : mode[0] == 'a' ? MODE_APPEND : MODE_READ;
    int handle;
    struct shim_file *f;

    if (strchr(mode, '+'))
        m = m == MODE_READ ? MODE_READ_WRITE : m;
    handle = dg_file_open(path, m);
    if (handle < 0)
    {
        errno = ENOENT;
        return NULL;
    }
    f = calloc(1, sizeof(*f));
    if (!f)
    {
        dg_file_close(handle);
        return NULL;
    }
    f->handle = handle;
    f->pushback = -1;
    return (FILE *) f;
}

int fclose(FILE *f)
{
    int status;

    if (is_console(f))
        return 0;
    // Writes are buffered on the Rust side, so a failed write shows up here.
    status = dg_file_close(HANDLE(f));
    free(f);
    return status == 0 ? 0 : EOF;
}

#define SHIM_FILE(f) ((struct shim_file *) (f))

size_t fread(void *buf, size_t size, size_t count, FILE *f)
{
    struct shim_file *sf = SHIM_FILE(f);
    size_t want = size * count, got = 0;

    if (is_console(f) || want == 0)
        return 0;
    if (sf->pushback >= 0)
    {
        *(unsigned char *) buf = (unsigned char) sf->pushback;
        sf->pushback = -1;
        got = 1;
    }
    got += dg_file_read(sf->handle, (char *) buf + got, want - got);
    if (got < want)
        sf->eof = 1;
    return got / size;
}

int feof(FILE *f)
{
    return is_console(f) ? 0 : SHIM_FILE(f)->eof;
}

static int src_get(struct scan_src *src)
{
    unsigned char c;

    if (src->str)
        return *src->str ? (unsigned char) *src->str++ : EOF;
    return fread(&c, 1, 1, src->file) == 1 ? c : EOF;
}

static void src_unget(struct scan_src *src, int c)
{
    if (c == EOF)
        return;
    if (src->str)
        src->str--;
    else
    {
        SHIM_FILE(src->file)->pushback = c;
        SHIM_FILE(src->file)->eof = 0;
    }
}

int fscanf(FILE *f, const char *fmt, ...)
{
    struct scan_src src = { NULL, f };
    va_list ap;
    int n;

    if (is_console(f))
        return EOF;
    va_start(ap, fmt);
    n = vscan(&src, fmt, ap);
    va_end(ap);
    return n;
}

size_t fwrite(const void *buf, size_t size, size_t count, FILE *f)
{
    if (size == 0)
        return 0;
    if (is_console(f))
    {
        dg_console_write(buf, size * count);
        return count;
    }
    return dg_file_write(HANDLE(f), buf, size * count) / size;
}

int fseek(FILE *f, long offset, int whence)
{
    struct shim_file *sf = SHIM_FILE(f);

    if (is_console(f))
        return -1;
    if (whence == SEEK_CUR && sf->pushback >= 0)
        offset--;
    sf->pushback = -1;
    sf->eof = 0;
    return dg_file_seek(sf->handle, offset, whence) < 0 ? -1 : 0;
}

long ftell(FILE *f)
{
    long pos;

    if (is_console(f))
        return -1;
    pos = dg_file_seek(HANDLE(f), 0, SEEK_CUR);
    return pos >= 0 && SHIM_FILE(f)->pushback >= 0 ? pos - 1 : pos;
}

int fflush(FILE *f)
{
    (void) f;
    return 0;
}

int vfprintf(FILE *f, const char *fmt, va_list ap)
{
    char buf[512];
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    size_t len = n < (int) sizeof(buf) ? (size_t) n : sizeof(buf) - 1;

    fwrite(buf, 1, len, f);
    return n;
}

int fprintf(FILE *f, const char *fmt, ...)
{
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vfprintf(f, fmt, ap);
    va_end(ap);
    return n;
}

int printf(const char *fmt, ...)
{
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vfprintf(stdout, fmt, ap);
    va_end(ap);
    return n;
}

int fputc(int c, FILE *f)
{
    unsigned char ch = (unsigned char) c;
    return fwrite(&ch, 1, 1, f) == 1 ? ch : EOF;
}

int fputs(const char *s, FILE *f)
{
    size_t n = strlen(s);
    return fwrite(s, 1, n, f) == n ? 0 : EOF;
}

int puts(const char *s)
{
    dg_console_write(s, strlen(s));
    dg_console_write("\n", 1);
    return 0;
}

int putchar(int c)
{
    char ch = (char) c;
    dg_console_write(&ch, 1);
    return c;
}

int remove(const char *path)
{
    return dg_file_remove(path);
}

int rename(const char *from, const char *to)
{
    return dg_file_rename(from, to);
}

int mkdir(const char *path, mode_t mode)
{
    (void) mode;
    return dg_mkdir(path);
}
