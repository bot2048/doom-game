// SPDX-License-Identifier: GPL-2.0-or-later
//
// Checks csrc/libc_shim.c against the host libc. Run with tests/run_shim_test.sh.

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

int shim_snprintf(char *buf, size_t cap, const char *fmt, ...);
FILE *shim_fopen(const char *path, const char *mode);
int shim_fclose(FILE *f);
int shim_feof(FILE *f);
int shim_fscanf(FILE *f, const char *fmt, ...);
long shim_ftell(FILE *f);
int shim_fseek(FILE *f, long offset, int whence);
size_t shim_fread(void *buf, size_t size, size_t count, FILE *f);
int shim_sscanf(const char *str, const char *fmt, ...);
int shim_atoi(const char *s);
double shim_atof(const char *s);
int shim_strcasecmp(const char *a, const char *b);
int shim_strncasecmp(const char *a, const char *b, size_t n);
int shim_strcmp(const char *a, const char *b);
int shim_strncmp(const char *a, const char *b, size_t n);
char *shim_strncpy(char *dst, const char *src, size_t n);
char *shim_strchr(const char *s, int c);
char *shim_strrchr(const char *s, int c);
char *shim_strstr(const char *h, const char *n);
size_t shim_strlen(const char *s);

// The shim's Rust-side hooks. Files are one in-memory buffer.
static const char *mem_data;
static long mem_pos;

void dg_console_write(const char *buf, size_t len) { fwrite(buf, 1, len, stdout); }
int dg_file_open(const char *path, int mode) { mem_pos = 0; return mem_data ? 0 : -1; }
size_t dg_file_read(int h, void *buf, size_t len)
{
    size_t left = strlen(mem_data) - mem_pos;
    if (len > left)
        len = left;
    memcpy(buf, mem_data + mem_pos, len);
    mem_pos += len;
    return len;
}
size_t dg_file_write(int h, const void *buf, size_t len) { return 0; }
long dg_file_seek(int h, long offset, int whence)
{
    mem_pos = (whence == SEEK_SET ? 0 : whence == SEEK_CUR ? mem_pos : (long) strlen(mem_data)) + offset;
    return mem_pos;
}
int dg_file_close(int h) { return 0; }
int dg_file_remove(const char *path) { return -1; }
int dg_file_rename(const char *from, const char *to) { return -1; }
int dg_mkdir(const char *path) { return -1; }
void dg_exit(int status) { exit(status); }

static int failures;

static int sign(int x) { return (x > 0) - (x < 0); }

#define CHECK(cond, ...)                      \
    do                                        \
    {                                         \
        if (!(cond))                          \
        {                                     \
            printf("FAIL %s:%d: ", __FILE__, __LINE__); \
            printf(__VA_ARGS__);              \
            printf("\n");                     \
            failures++;                       \
        }                                     \
    } while (0)

// Formats with both implementations and compares text and return value.
#define FMT(cap, ...)                                                       \
    do                                                                      \
    {                                                                       \
        char want[256], got[256];                                           \
        memset(want, 'X', sizeof(want));                                    \
        memset(got, 'X', sizeof(got));                                      \
        int rw = snprintf(want, cap, __VA_ARGS__);                          \
        int rg = shim_snprintf(got, cap, __VA_ARGS__);                      \
        CHECK(rw == rg && !memcmp(want, got, cap), "snprintf(%d, %s): want [%s] %d, got [%s] %d", \
              cap, #__VA_ARGS__, want, rw, got, rg);                        \
    } while (0)

static void test_snprintf(void)
{
    FMT(64, "%i", 0);
    FMT(64, "%i", -2147483647 - 1);
    FMT(64, "%d|%d", 12345, -42);
    FMT(64, "%s and %s", "lump", "");
    FMT(64, "[%7i] [%3i] [%02i] [%02i]", 42, 1234, 5, -5);
    FMT(64, "STCFN%.3d", 33);
    FMT(64, "%.2d:%2.2d", 3, 7);
    FMT(64, "%x %02x %X %o %u", 255, 10, 48879, 8, 4000000000u);
    FMT(64, "%c%c%c", 'D', 'O', 'M');
    FMT(64, "[%79s]", "right");
    FMT(64, "[%-6s] [%.8s]", "ab", "WIMAP0LONGNAME");
    FMT(64, "%f %0.2f %.1f", 3.25, 2.005, -0.75);
    FMT(64, "100%% done");
    FMT(64, "%ld %lu %lld", -7L, 9UL, -123456789012LL);
    FMT(64, "%5c|%-3c|", 'a', 'b');
    FMT(8, "truncated %s", "string");
    FMT(1, "%d", 99);
    FMT(64, "%s%s%s", "DOOM", "SAV", "0.dsg");
    FMT(64, "E%iM%i", 1, 9);
    FMT(64, "%-5d|%05d|%-05d", 42, -42, 7);
    FMT(64, "%jd|%jd", (long long) -5, (long long) 1 << 40);
    FMT(64, "%f", 1.0 / 0.0);

    {
        char got[16];
        int n = shim_snprintf(got, sizeof(got), "%p", (void *) 0x1234);
        CHECK(n == 6 && !strcmp(got, "0x1234"), "%%p: got [%s]", got);
    }
}

#define SCAN(str, fmt)                                                  \
    do                                                                  \
    {                                                                   \
        int want = -1, got = -1;                                        \
        int rw = sscanf(str, fmt, &want);                               \
        int rg = shim_sscanf(str, fmt, &got);                           \
        CHECK(rw == rg && (rw != 1 || want == got),                     \
              "sscanf(\"%s\", \"%s\"): want %d (%d), got %d (%d)", str, fmt, want, rw, got, rg); \
    } while (0)

static void test_sscanf(void)
{
    // m_misc.c M_StrToInt and m_config.c key parsing.
    const char *inputs[] = { "42", "  -17", "0x1f", "0X1F", "017", "0", "abc", "", "  0x", "300 x" };
    for (size_t i = 0; i < sizeof(inputs) / sizeof(*inputs); i++)
    {
        SCAN(inputs[i], " 0x%x");
        SCAN(inputs[i], " 0X%x");
        SCAN(inputs[i], " 0%o");
        SCAN(inputs[i], " %d");
        SCAN(inputs[i], "%x");
        SCAN(inputs[i], "%i");
    }
}

#define SCAN2(str, fmt)                                                         \
    do                                                                          \
    {                                                                           \
        char wa[80] = "-", wb[100] = "-", ga[80] = "-", gb[100] = "-";          \
        int rw = sscanf(str, fmt, wa, wb);                                      \
        int rg = shim_sscanf(str, fmt, ga, gb);                                 \
        CHECK(rw == rg && !strcmp(wa, ga) && !strcmp(wb, gb),                   \
              "sscanf(\"%s\", \"%s\"): want %d [%s] [%s], got %d [%s] [%s]", \
              str, fmt, rw, wa, wb, rg, ga, gb);                                \
    } while (0)

static void test_string_scans(void)
{
    SCAN2("mouse_sensitivity 5\n", "%79s %99[^\n]\n");
    SCAN2("chatmacro0 \"No\"\n", "%79s %99[^\n]\n");
    SCAN2("key_up 72", "%79s %99[^\n]\n");
    SCAN2("lonely\n", "%79s %99[^\n]\n");
    SCAN2("", "%79s %99[^\n]\n");
    SCAN2("   \n", "%79s %99[^\n]\n");
    SCAN2("abc]def x", "%[]abc] %s");
    SCAN2("toolongname value", "%4s %3s");
}

// Reads a config file with the shim's stdio exactly like m_config.c's
// LoadDefaultCollection, and with glibc, and compares the parsed pairs.
static void test_config_load(void)
{
    static const char cfg[] =
        "mouse_sensitivity             5\n"
        "sfx_volume                    8\n"
        "show_messages                 1\n"
        "key_right                     0xae\n"
        "chatmacro0                    \"No\"\n"
        "\n"
        "garbage\n"
        "usegamma 2";
    char want[64][200], got[64][200];
    int nw = 0, ng = 0;

    FILE *g = fmemopen((void *) cfg, sizeof(cfg) - 1, "r");
    while (!feof(g))
    {
        char a[80], b[100];
        if (fscanf(g, "%79s %99[^\n]\n", a, b) != 2)
            continue;
        snprintf(want[nw++], 200, "%s=%s", a, b);
    }
    fclose(g);

    mem_data = cfg;
    FILE *f = shim_fopen("cfg", "r");
    CHECK(f != NULL, "shim_fopen");
    for (int guard = 0; f && !shim_feof(f) && guard < 100; guard++)
    {
        char a[80], b[100];
        if (shim_fscanf(f, "%79s %99[^\n]\n", a, b) != 2)
            continue;
        snprintf(got[ng++], 200, "%s=%s", a, b);
    }
    CHECK(f && shim_feof(f), "config read did not reach EOF");
    if (f)
        shim_fclose(f);
    mem_data = NULL;

    CHECK(nw == ng, "config entries: want %d, got %d", nw, ng);
    for (int i = 0; i < nw && i < ng; i++)
        CHECK(!strcmp(want[i], got[i]), "config entry %d: want [%s], got [%s]", i, want[i], got[i]);
}

static void test_file_position(void)
{
    mem_data = "0123456789";
    FILE *f = shim_fopen("x", "rb");
    char buf[4] = { 0 };
    int n;
    CHECK(shim_fscanf(f, "%d", &n) == 1 && n == 123456789, "fscanf int: %d", n);
    CHECK(shim_feof(f), "feof after reading to end");
    CHECK(shim_ftell(f) == 10, "ftell at end: %ld", shim_ftell(f));
    shim_fseek(f, 2, SEEK_SET);
    CHECK(!shim_feof(f), "fseek clears eof");
    CHECK(shim_fread(buf, 1, 3, f) == 3 && !strcmp(buf, "234"), "fread after seek: %s", buf);
    CHECK(shim_ftell(f) == 5, "ftell after read: %ld", shim_ftell(f));
    shim_fclose(f);
    mem_data = NULL;
}

static void test_numbers(void)
{
    const char *ints[] = { "0", "12", "-34", "  56", "+7", "8x", "x" };
    for (size_t i = 0; i < sizeof(ints) / sizeof(*ints); i++)
        CHECK(atoi(ints[i]) == shim_atoi(ints[i]), "atoi(%s)", ints[i]);

    const char *floats[] = { "0", "1.5", "-2.25", "  3.125", "4.", ".5" };
    for (size_t i = 0; i < sizeof(floats) / sizeof(*floats); i++)
        CHECK(atof(floats[i]) == shim_atof(floats[i]), "atof(%s): %f vs %f", floats[i],
              atof(floats[i]), shim_atof(floats[i]));
}

static void test_strings(void)
{
    const char *words[] = { "", "a", "MAP01", "map01", "E1M1", "e1m10", "DOOM2.WAD", "doom2.wad" };
    size_t n = sizeof(words) / sizeof(*words);

    for (size_t i = 0; i < n; i++)
    {
        CHECK(strlen(words[i]) == shim_strlen(words[i]), "strlen(%s)", words[i]);
        for (size_t j = 0; j < n; j++)
        {
            const char *a = words[i], *b = words[j];
            CHECK(sign(strcmp(a, b)) == sign(shim_strcmp(a, b)), "strcmp(%s, %s)", a, b);
            CHECK(sign(strcasecmp(a, b)) == sign(shim_strcasecmp(a, b)), "strcasecmp(%s, %s)", a, b);
            for (size_t k = 0; k < 6; k++)
            {
                CHECK(sign(strncmp(a, b, k)) == sign(shim_strncmp(a, b, k)), "strncmp(%s, %s, %zu)", a, b, k);
                CHECK(sign(strncasecmp(a, b, k)) == sign(shim_strncasecmp(a, b, k)),
                      "strncasecmp(%s, %s, %zu)", a, b, k);
            }
            CHECK(strstr(a, b) == shim_strstr(a, b), "strstr(%s, %s)", a, b);
        }
        for (const char *c = "AM1.\0"; ; c++)
        {
            CHECK(strchr(words[i], *c) == shim_strchr(words[i], *c), "strchr(%s, %d)", words[i], *c);
            CHECK(strrchr(words[i], *c) == shim_strrchr(words[i], *c), "strrchr(%s, %d)", words[i], *c);
            if (!*c)
                break;
        }
    }

    {
        char want[10], got[10];
        memset(want, 'X', sizeof(want));
        memset(got, 'X', sizeof(got));
        strncpy(want, "E1M1", 8);
        shim_strncpy(got, "E1M1", 8);
        CHECK(!memcmp(want, got, sizeof(want)), "strncpy padding");
        strncpy(want, "TOOLONGNAME", 8);
        shim_strncpy(got, "TOOLONGNAME", 8);
        CHECK(!memcmp(want, got, sizeof(want)), "strncpy truncation");
    }
}

int main(void)
{
    test_snprintf();
    test_sscanf();
    test_string_scans();
    test_config_load();
    test_file_position();
    test_numbers();
    test_strings();
    if (failures)
    {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("all shim tests passed\n");
    return 0;
}
