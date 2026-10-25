/*
 * Place names on the map, see map_labels.h
 *
 * SPDX-License-Identifier: MIT
 */

#include "map_labels.h"

#include <string.h>

#include "colors.h"

#define MAX_LABELS 48
#define MAX_CHARS 32 // longer names are cut
#define GAP 2 // free pixels between two names

typedef struct {
    int16_t left, top, right, bottom;
} label_rect_t;

static label_rect_t placed[MAX_LABELS];
static uint8_t placed_count;

void map_labels_reset(void)
{
    placed_count = 0;
}

/* Latin-1 letters (U+00C0..U+00FF) as ASCII */
static const char* const LATIN1[64] = {
    "A", "A", "A", "A", "Ae", "A", "AE", "C", "E", "E", "E", "E", "I", "I", "I", "I", // C0
    "D", "N", "O", "O", "O", "O", "Oe", "x", "O", "U", "U", "U", "Ue", "Y", "Th", "ss", // D0
    "a", "a", "a", "a", "ae", "a", "ae", "c", "e", "e", "e", "e", "i", "i", "i", "i", // E0
    "d", "n", "o", "o", "o", "o", "oe", "-", "o", "u", "u", "u", "ue", "y", "th", "y", // F0
};

/* UTF-8 to the characters of the font, returns the length of out */
static size_t to_ascii(const char* text, uint32_t len, char* out, size_t max)
{
    size_t n = 0;
    for (uint32_t i = 0; i < len && n + 3 < max;) {
        uint8_t c = (uint8_t)text[i];
        if (c < 0x80) {
            out[n++] = c >= 0x20 ? (char)c : ' ';
            i++;
            continue;
        }
        uint32_t cp = 0;
        uint32_t extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
        cp = c & (0x3F >> extra);
        i++;
        for (uint32_t k = 0; k < extra && i < len; k++, i++)
            cp = (cp << 6) | ((uint8_t)text[i] & 0x3F);
        const char* rep = (cp >= 0xC0 && cp <= 0xFF) ? LATIN1[cp - 0xC0] : "?";
        while (*rep && n + 1 < max)
            out[n++] = *rep++;
    }
    out[n] = 0;
    return n;
}

static int overlaps(const label_rect_t* a)
{
    for (uint8_t i = 0; i < placed_count; i++) {
        const label_rect_t* b = &placed[i];
        if (a->left < b->right + GAP && a->right + GAP > b->left && a->top < b->bottom + GAP
            && a->bottom + GAP > b->top)
            return 1;
    }
    return 0;
}

int map_labels_draw(const display_t* dsp, font_t* font, int16_t x, int16_t y, const char* text, uint32_t len,
    uint8_t rank)
{
    char ascii[MAX_CHARS * 3 + 1];
    size_t n = to_ascii(text, len, ascii, sizeof(ascii));
    if (n > MAX_CHARS)
        n = MAX_CHARS, ascii[n] = 0;
    if (n == 0 || placed_count >= MAX_LABELS)
        return 0;

    int16_t width = (int16_t)(n * font->width) + (rank == 1 ? 1 : 0);
    int16_t height = font->height;
    label_rect_t r = { x - width / 2, y - height / 2, 0, 0 };
    r.right = r.left + width;
    r.bottom = r.top + height;
    // the halo needs one pixel around the text
    if (r.left < 1 || r.top < 1 || r.right > dsp->size.width - 1 || r.bottom > dsp->size.height - 1)
        return 0;
    if (overlaps(&r))
        return 0;
    placed[placed_count++] = r;

    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++)
            if (dx || dy) {
                display_text_draw(dsp, font, r.left + dx, r.top + dy, ascii, WHITE);
                if (rank == 1)
                    display_text_draw(dsp, font, r.left + dx + 1, r.top + dy, ascii, WHITE);
            }
    display_text_draw(dsp, font, r.left, r.top, ascii, BLACK);
    if (rank == 1)
        display_text_draw(dsp, font, r.left + 1, r.top, ascii, BLACK);
    return 1;
}
