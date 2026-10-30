// SPDX-License-Identifier: GPL-2.0-or-later
//
// Force-included into the engine and csrc/libc_shim.c.
//
// Device: newlib implements some stdio calls as macros that read fields of
// its own FILE struct, which the shim's FILE objects don't have. Those are
// turned back into calls to the shim.
//
// Simulator (DG_HOSTED): the engine uses the shim's stdio, formatting and
// string functions, renamed to shim_*, instead of the host libc, so it runs
// the same file and formatting code as the device. Memory allocation stays
// with the host libc.

#ifndef DG_LIBC_H
#define DG_LIBC_H

#ifndef DG_HOSTED

#include <stdio.h>

#undef feof
int feof(FILE *f);

#else

// Pull in the real declarations first; the renames below only affect code
// that follows.
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>

FILE *shim_fopen(const char *path, const char *mode);
int shim_fclose(FILE *f);
size_t shim_fread(void *buf, size_t size, size_t count, FILE *f);
size_t shim_fwrite(const void *buf, size_t size, size_t count, FILE *f);
int shim_fseek(FILE *f, long offset, int whence);
long shim_ftell(FILE *f);
int shim_fflush(FILE *f);
int shim_fputc(int c, FILE *f);
int shim_fputs(const char *s, FILE *f);
int shim_feof(FILE *f);
int shim_fscanf(FILE *f, const char *fmt, ...);
int shim_vfprintf(FILE *f, const char *fmt, va_list ap);
int shim_fprintf(FILE *f, const char *fmt, ...);
int shim_printf(const char *fmt, ...);
int shim_puts(const char *s);
int shim_putchar(int c);
int shim_remove(const char *path);
int shim_rename(const char *from, const char *to);
int shim_mkdir(const char *path, mode_t mode);
int shim_snprintf(char *buf, size_t cap, const char *fmt, ...);
int shim_vsnprintf(char *buf, size_t cap, const char *fmt, va_list ap);
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
char *shim_strstr(const char *haystack, const char *needle);
char *shim_strdup(const char *s);
size_t shim_strlen(const char *s);
void shim_exit(int status) __attribute__((noreturn));

#define DG_REDIRECT(name) shim_##name

#undef fopen
#define fopen DG_REDIRECT(fopen)
#undef fclose
#define fclose DG_REDIRECT(fclose)
#undef fread
#define fread DG_REDIRECT(fread)
#undef fwrite
#define fwrite DG_REDIRECT(fwrite)
#undef fseek
#define fseek DG_REDIRECT(fseek)
#undef ftell
#define ftell DG_REDIRECT(ftell)
#undef fflush
#define fflush DG_REDIRECT(fflush)
#undef fputc
#define fputc DG_REDIRECT(fputc)
#undef fputs
#define fputs DG_REDIRECT(fputs)
#undef feof
#define feof DG_REDIRECT(feof)
#undef fscanf
#define fscanf DG_REDIRECT(fscanf)
#undef vfprintf
#define vfprintf DG_REDIRECT(vfprintf)
#undef fprintf
#define fprintf DG_REDIRECT(fprintf)
#undef printf
#define printf DG_REDIRECT(printf)
#undef puts
#define puts DG_REDIRECT(puts)
#undef putchar
#define putchar DG_REDIRECT(putchar)
#undef remove
#define remove DG_REDIRECT(remove)
#undef rename
#define rename DG_REDIRECT(rename)
#undef mkdir
#define mkdir DG_REDIRECT(mkdir)
#undef snprintf
#define snprintf DG_REDIRECT(snprintf)
#undef vsnprintf
#define vsnprintf DG_REDIRECT(vsnprintf)
#undef sscanf
#define sscanf DG_REDIRECT(sscanf)
#undef atoi
#define atoi DG_REDIRECT(atoi)
#undef atof
#define atof DG_REDIRECT(atof)
#undef strcasecmp
#define strcasecmp DG_REDIRECT(strcasecmp)
#undef strncasecmp
#define strncasecmp DG_REDIRECT(strncasecmp)
#undef strcmp
#define strcmp DG_REDIRECT(strcmp)
#undef strncmp
#define strncmp DG_REDIRECT(strncmp)
#undef strncpy
#define strncpy DG_REDIRECT(strncpy)
#undef strchr
#define strchr DG_REDIRECT(strchr)
#undef strrchr
#define strrchr DG_REDIRECT(strrchr)
#undef strstr
#define strstr DG_REDIRECT(strstr)
#undef strdup
#define strdup DG_REDIRECT(strdup)
#undef strlen
#define strlen DG_REDIRECT(strlen)
#undef exit
#define exit DG_REDIRECT(exit)

#endif // DG_HOSTED

#endif // DG_LIBC_H
