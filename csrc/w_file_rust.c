// SPDX-License-Identifier: GPL-2.0-or-later
//
// WAD file backend for the Passport Prime port. Replaces doomgeneric's
// w_file_stdc.c and forwards to src/doom.rs, which reads the WAD through
// std::fs in the simulator and through the KeyOS filesystem on device.
// Lumps are streamed on demand, so the WAD never has to fit in RAM.

#include "w_file.h"
#include "z_zone.h"

// Implemented in src/doom.rs. dg_wad_open returns a handle >= 0 and the
// file length, or -1 if the WAD cannot be opened.
extern int dg_wad_open(const char *path, unsigned int *len);
extern size_t dg_wad_read(int handle, unsigned int offset, void *buffer, size_t len);
extern void dg_wad_close(int handle);

typedef struct
{
    wad_file_t wad;
    int handle;
} rust_wad_file_t;

extern wad_file_class_t stdc_wad_file;

static wad_file_t *W_Rust_OpenFile(char *path)
{
    unsigned int len = 0;
    int handle = dg_wad_open(path, &len);
    rust_wad_file_t *result;

    if (handle < 0)
    {
        return NULL;
    }

    result = Z_Malloc(sizeof(rust_wad_file_t), PU_STATIC, 0);
    result->wad.file_class = &stdc_wad_file;
    result->wad.mapped = NULL;
    result->wad.length = len;
    result->handle = handle;

    return &result->wad;
}

static void W_Rust_CloseFile(wad_file_t *wad)
{
    rust_wad_file_t *rust_wad = (rust_wad_file_t *) wad;

    dg_wad_close(rust_wad->handle);
    Z_Free(rust_wad);
}

static size_t W_Rust_Read(wad_file_t *wad, unsigned int offset,
                          void *buffer, size_t buffer_len)
{
    rust_wad_file_t *rust_wad = (rust_wad_file_t *) wad;

    return dg_wad_read(rust_wad->handle, offset, buffer, buffer_len);
}

// Keeps the name w_file.c looks up.
wad_file_class_t stdc_wad_file =
{
    W_Rust_OpenFile,
    W_Rust_CloseFile,
    W_Rust_Read,
};
