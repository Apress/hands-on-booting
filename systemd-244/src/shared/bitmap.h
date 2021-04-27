/* SPDX-License-Identifier: LGPL-2.1+ */
#pragma once

#include <stdbool.h>

#include "hashmap.h"
#include "macro.h"

typedef struct Bitmap {
        uint64_t *bitmaps;
        size_t n_bitmaps;
        size_t bitmaps_allocated;
} Bitmap;

Bitmap *bitmap_new(void);
Bitmap *bitmap_copy(Bitmap *b);
int bitmap_ensure_allocated(Bitmap **b);
void bitmap_free(Bitmap *b);

int bitmap_set(Bitmap *b, unsigned n);
void bitmap_unset(Bitmap *b, unsigned n);
bool bitmap_isset(const Bitmap *b, unsigned n);
bool bitmap_isclear(const Bitmap *b);
void bitmap_clear(Bitmap *b);

bool bitmap_iterate(const Bitmap *b, Iterator *i, unsigned *n);

bool bitmap_equal(const Bitmap *a, const Bitmap *b);

#define BITMAP_FOREACH(n, b, i) \
        for ((i).idx = 0; bitmap_iterate((b), &(i), (unsigned*)&(n)); )

DEFINE_TRIVIAL_CLEANUP_FUNC(Bitmap*, bitmap_free);

#define _cleanup_bitmap_free_ _cleanup_(bitmap_freep)
