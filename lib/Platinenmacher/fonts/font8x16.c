/** Thin 8x8 font **/

#include "font8x8.h"

// Constant: font8x8_basic
// Contains an 8x8 font map for unicode points U+0020 - U+007F (basic latin)
const uint8_t font8x16_offset = 32;
const char * font8x16_name = "8x16";
const uint8_t font8x16[] = {
    8, 16, 0x20, 0,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,// U+0020 (space)
    0x18, 0x3C, 0x3C, 0x18, 0x18, 0x00, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,// U+0021 (!) noch nicht erzeugt nur offsets
    0x36, 0x36, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,// U+0022 (") noch nicht erzeugt nur offsets
    0x36, 0x36, 0x7F, 0x36, 0x7F, 0x36, 0x36, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,// U+0023 (#) noch nicht erzeugt nur offsets
    0x0C, 0x3E, 0x03, 0x1E, 0x30, 0x1F, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,// U+0024 ($) noch nicht erzeugt nur offsets
    0x00, 0x63, 0x33, 0x18, 0x0C, 0x66, 0x63, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,// U+0025 (%) noch nicht erzeugt nur offsets
    0x1C, 0x36, 0x1C, 0x6E, 0x3B, 0x33, 0x6E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,// U+0026 (&) noch nicht erzeugt nur offsets
    0x06, 0x06, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,// U+0027 (') noch nicht erzeugt nur offsets
    0x18, 0x0C, 0x06, 0x06, 0x06, 0x0C, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,// U+0028 (() noch nicht erzeugt nur offsets
    0x06, 0x0C, 0x18, 0x18, 0x18, 0x0C, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,// U+0029 ()) noch nicht erzeugt nur offsets
    0x00, 0x66, 0x3C, 0xFF, 0x3C, 0x66, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,// U+002A (*) noch nicht erzeugt nur offsets
    0x00, 0x0C, 0x0C, 0x3F, 0x0C, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,// U+002B (+) noch nicht erzeugt nur offsets
    0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,// U+002C (,) noch nicht erzeugt nur offsets
    0x00, 0x00, 0x00, 0x3F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,// U+002D (-) noch nicht erzeugt nur offsets
    0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,// U+002E (.) noch nicht erzeugt nur offsets
    0x60, 0x30, 0x18, 0x0C, 0x06, 0x03, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,// U+002F (/) noch nicht erzeugt nur offsets
    0x3E, 0x63, 0x73, 0x7B, 0x6F, 0x67, 0x3E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,// U+0030 (0) noch nicht erzeugt nur offsets
    0x0C, 0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x3F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,// U+0031 (1) noch nicht erzeugt nur offsets
    0x1E, 0x33, 0x30, 0x1C, 0x06, 0x33, 0x3F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,// U+0032 (2) noch nicht erzeugt nur offsets
    0x1E, 0x33, 0x30, 0x1C, 0x30, 0x33, 0x1E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,// U+0033 (3) noch nicht erzeugt nur offsets
    0x38, 0x3C, 0x36, 0x33, 0x7F, 0x30, 0x78, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,// U+0034 (4) noch nicht erzeugt nur offsets
    0x3F, 0x03, 0x1F, 0x30, 0x30, 0x33, 0x1E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,// U+0035 (5) noch nicht erzeugt nur offsets
    0x1C, 0x06, 0x03, 0x1F, 0x33, 0x33, 0x1E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,// U+0036 (6) noch nicht erzeugt nur offsets
    0x3F, 0x33, 0x30, 0x18, 0x0C, 0x0C, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,// U+0037 (7) noch nicht erzeugt nur offsets
    0x1E, 0x33, 0x33, 0x1E, 0x33, 0x33, 0x1E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,// U+0038 (8) noch nicht erzeugt nur offsets
    0x1E, 0x33, 0x33, 0x3E, 0x30, 0x18, 0x0E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,// U+0039 (9) noch nicht erzeugt nur offsets
    0x00, 0x0C, 0x0C, 0x00, 0x00, 0x0C, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,// U+003A (:) noch nicht erzeugt nur offsets
    0x00, 0x0C, 0x0C, 0x00, 0x00, 0x0C, 0x0C, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,// U+003B (//) noch nicht erzeugt nur offsets
    0x18, 0x0C, 0x06, 0x03, 0x06, 0x0C, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,// U+003C (<) noch nicht erzeugt nur offsets
    0x00, 0x00, 0x3F, 0x00, 0x00, 0x3F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,// U+003D (=) noch nicht erzeugt nur offsets
    0x06, 0x0C, 0x18, 0x30, 0x18, 0x0C, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,// U+003E (>) noch nicht erzeugt nur offsets
    0x1E, 0x33, 0x30, 0x18, 0x0C, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,// U+003F (?) noch nicht erzeugt nur offsets
    0x3E, 0x63, 0x7B, 0x7B, 0x7B, 0x03, 0x1E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,// U+0040 (@) noch nicht erzeugt nur offsets
    0x00, 0x80, 0xE0, 0xF8, 0x7C, 0x3F, 0xFF, 0xFC, 0x7C, 0x7F, 0x0F, 0x0F, 0x07, 0x17, 0x1F, 0x1F, // U+0041 (A)
    0xC6, 0xFE, 0xFE, 0xFE, 0xDE, 0xFE, 0xFE, 0x7C, 0xFF, 0xFF, 0x7F, 0x7B, 0x7F, 0x3F, 0x1F, 0x0F, // U+0042 (B)
    0xE0, 0xF8, 0x3C, 0x0E, 0x0E, 0x0E, 0x3C, 0x1C, 0x3F, 0x7F, 0x70, 0x60, 0x70, 0x38, 0x10, 0x00, // U+0043 (C)
    0x00, 0xC2, 0xFE, 0x7F, 0x0F, 0x06, 0xFE, 0xFC, 0x7C, 0x7F, 0x3F, 0x3C, 0x1C, 0x0F, 0x07, 0x01, // U+0044 (D)
    0x00, 0xE6, 0xFE, 0xFE, 0xFE, 0xCE, 0xCE, 0x0E, 0xFE, 0xFF, 0xFF, 0x7F, 0x7B, 0x39, 0x3D, 0x1C, // U+0045 (E)
    0x00, 0xC4, 0xFE, 0xFE, 0x8E, 0x86, 0x06, 0x06, 0x7C, 0x7F, 0x1F, 0x03, 0x03, 0x01, 0x00, 0x00, // U+0046 (F)
    0xF8, 0xFC, 0x7E, 0x1E, 0x0E, 0x9E, 0x7E, 0x7E, 0x7F, 0xFF, 0xF0, 0xF0, 0xFB, 0x7F, 0xFF, 0x7F, // U+0047 (G)
    0x80, 0xFC, 0xFE, 0x9E, 0x80, 0xF0, 0xFE, 0x3E, 0x7F, 0x3F, 0x03, 0x01, 0x1F, 0x1F, 0x07, 0x00, // U+0048 (H)
    0x00, 0x00, 0x0E, 0xEE, 0xFE, 0x7E, 0x06, 0x06, 0x60, 0x70, 0x3E, 0x3F, 0x3F, 0x18, 0x08, 0x00, // U+0049 (I)
    0x00, 0x00, 0x00, 0x06, 0xC6, 0xFE, 0x7E, 0x06, 0x70, 0xE0, 0xE0, 0x78, 0x3F, 0x0F, 0x00, 0x00, // U+004A (J)
    0x00, 0xF8, 0xFE, 0xDE, 0xF0, 0x78, 0x1E, 0x0E, 0x7F, 0x7F, 0x03, 0x0F, 0x3F, 0x1C, 0x00, 0x00, // U+004B (K)
    0x00, 0x00, 0xF0, 0xFE, 0x7F, 0x03, 0x00, 0x00, 0x40, 0x7E, 0x7F, 0x3F, 0x38, 0x18, 0x1C, 0x00, // U+004C (L)
    0xC8, 0xFC, 0xFC, 0x80, 0xC0, 0xF0, 0xFC, 0xFE, 0x7F, 0x0F, 0x0F, 0x1F, 0x07, 0x1F, 0x0F, 0x07, // U+004D (M)
    0x00, 0xE4, 0xFE, 0xFC, 0xC0, 0xE0, 0xFE, 0x7E, 0x7E, 0x7F, 0x07, 0x0F, 0x3F, 0x1F, 0x1F, 0x00, // U+004E (N)
    0xE0, 0xF8, 0x7C, 0x1C, 0x0E, 0x0E, 0xFE, 0xFC, 0x7F, 0xFF, 0xE0, 0xE0, 0xE0, 0x78, 0x3F, 0x1F, // U+004F (O)
    0x00, 0xC2, 0xFE, 0xFF, 0x8F, 0xC7, 0xFE, 0xFE, 0x7C, 0x7F, 0x0F, 0x03, 0x03, 0x01, 0x01, 0x00, // U+0050 (P)
    0xE0, 0xF8, 0x3C, 0x1C, 0x0E, 0x0E, 0x1E, 0xFC, 0x7F, 0x7F, 0xE0, 0xE0, 0xFC, 0x7C, 0xFF, 0xFF, // U+0051 (Q)
    0x00, 0x84, 0xFC, 0xFE, 0x3E, 0x0E, 0xFC, 0xFC, 0xF8, 0xFF, 0x1F, 0x07, 0x07, 0x7F, 0x3F, 0x01, // U+0052 (R)
    0x00, 0xF0, 0xF8, 0xFC, 0x0C, 0x0C, 0x0E, 0x08, 0xC0, 0xE0, 0xE1, 0x73, 0x7F, 0x3E, 0x00, 0x00, // U+0053 (S)
    0x1C, 0x1C, 0xFC, 0xFC, 0xFC, 0x0C, 0x0E, 0x0E, 0x80, 0xFC, 0xFF, 0x1F, 0x00, 0x00, 0x00, 0x00, // U+0054 (T)
    0xF0, 0xFC, 0x7E, 0x02, 0x00, 0xF0, 0xFE, 0x7E, 0xFF, 0xFF, 0xE0, 0xF0, 0x7C, 0x3F, 0x0F, 0x00, // U+0055 (U)
    0xFC, 0xFE, 0xFE, 0x00, 0xC0, 0xF0, 0xFE, 0x3E, 0x0F, 0xFF, 0x7F, 0x3E, 0x0F, 0x03, 0x00, 0x00, // U+0056 (V)
    0xFC, 0x00, 0x80, 0x80, 0x00, 0xE0, 0xFE, 0x3E, 0x7F, 0x3E, 0x3F, 0x3F, 0x3F, 0x0F, 0x01, 0x00, // U+0057 (W)
    0x00, 0x00, 0x78, 0xFC, 0xE0, 0xE0, 0xF8, 0x3C, 0xE0, 0xF0, 0x3C, 0x1F, 0x0F, 0x7F, 0x3C, 0x00, // U+0058 (X)
    0x7C, 0xFE, 0xF0, 0xE0, 0xF8, 0x7E, 0x1E, 0x03, 0xC0, 0x7F, 0x7F, 0x07, 0x01, 0x00, 0x00, 0x00, // U+0059 (Y)
    0x00, 0x1C, 0x1C, 0x8C, 0xCC, 0xFC, 0x7C, 0x1E, 0xE0, 0xF8, 0x7E, 0x7F, 0x77, 0x31, 0x38, 0x00, // U+005A (Z)
    0x1E, 0x06, 0x06, 0x06, 0x06, 0x06, 0x1E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // U+005B ([) noch nicht erzeugt nur offsets
    0x03, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // U+005C (\) noch nicht erzeugt nur offsets
    0x1E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x1E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // U+005D (]) noch nicht erzeugt nur offsets
    0x08, 0x1C, 0x36, 0x63, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // U+005E (^) noch nicht erzeugt nur offsets
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // U+005F (_) noch nicht erzeugt nur offsets
    0x0C, 0x0C, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // U+0060 (`) noch nicht erzeugt nur offsets
    0x00, 0x00, 0xC0, 0xF0, 0xB8, 0x9E, 0xF8, 0x00, 0x00, 0x0C, 0x07, 0x01, 0x01, 0x03, 0x03, 0x00, // U+0061 (a) 
    0x00, 0x04, 0xFC, 0xFC, 0xEC, 0xFC, 0xB8, 0x00, 0x00, 0x1F, 0x0F, 0x0D, 0x0F, 0x07, 0x03, 0x00, // U+0062 (b)
    0x00, 0xC0, 0xF0, 0x78, 0x18, 0x38, 0x30, 0x00, 0x00, 0x1F, 0x3F, 0x30, 0x30, 0x18, 0x08, 0x00, // U+0063 (c)
    0x00, 0x04, 0xFC, 0x7E, 0x0C, 0xFC, 0xF8, 0x00, 0x00, 0x1C, 0x1F, 0x0C, 0x07, 0x07, 0x01, 0x00, // U+0064 (d)
    0x00, 0xFC, 0xFC, 0xFC, 0xFC, 0xEC, 0x0E, 0x00, 0x10, 0x1F, 0x1F, 0x0F, 0x0D, 0x06, 0x00, 0x00, // U+0065 (e)
    0x00, 0x00, 0xE8, 0xFC, 0xCC, 0xCC, 0x0C, 0x00, 0x00, 0x0E, 0x0F, 0x01, 0x01, 0x00, 0x00, 0x00, // U+0066 (f)
    0x00, 0xF0, 0xFC, 0x3E, 0x86, 0xBE, 0xBE, 0x00, 0x00, 0x0F, 0x3F, 0x3C, 0x3D, 0x3F, 0x1F, 0x00, // U+0067 (g)
    0x00, 0x00, 0xFC, 0xFE, 0xC0, 0xF8, 0xFE, 0x00, 0x00, 0x1C, 0x0F, 0x01, 0x07, 0x07, 0x03, 0x00, // U+0068 (h)
    0x00, 0x00, 0x0C, 0xFC, 0x7C, 0x04, 0x00, 0x00, 0x00, 0x18, 0x0F, 0x0F, 0x0C, 0x04, 0x00, 0x00, // U+0069 (i)
    0x00, 0x00, 0x00, 0x00, 0xC4, 0xFC, 0x7C, 0x00, 0x00, 0x18, 0x30, 0x30, 0x0F, 0x07, 0x00, 0x00, // U+006A (j)
    0x00, 0xFC, 0xFE, 0xF8, 0x3C, 0x0E, 0x00, 0x00, 0x18, 0x0F, 0x00, 0x07, 0x07, 0x00, 0x00, 0x00, // U+006B (k)
    0x00, 0x00, 0xF0, 0xFC, 0x3E, 0x00, 0x00, 0x00, 0x00, 0x08, 0x0F, 0x07, 0x06, 0x07, 0x00, 0x00, // U+006C (l)
    0x00, 0xFC, 0xFC, 0xC0, 0xF0, 0xFC, 0xFC, 0x00, 0x0F, 0x03, 0x03, 0x07, 0x07, 0x03, 0x01, 0x00, // U+006D (m)
    0x00, 0x00, 0xFC, 0xFC, 0xE0, 0xE0, 0x3C, 0x02, 0x18, 0x0F, 0x01, 0x03, 0x07, 0x07, 0x00, 0x00, // U+006E (n)
    0x00, 0x80, 0xF8, 0x3C, 0x0C, 0xFC, 0xFC, 0x00, 0x00, 0x03, 0x0F, 0x0C, 0x0C, 0x07, 0x03, 0x00, // U+006F (o)
    0x00, 0xE0, 0xFC, 0xCE, 0xFC, 0x7C, 0x00, 0x00, 0x00, 0x0F, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, // U+0070 (p)
    0x00, 0xC0, 0xF8, 0x18, 0x0C, 0x9C, 0xF8, 0x00, 0x00, 0x03, 0x07, 0x0C, 0x0F, 0x0F, 0x05, 0x00, // U+0071 (q)
    0x00, 0xE0, 0xFC, 0xDE, 0xFC, 0x7C, 0x00, 0x00, 0x00, 0x0F, 0x03, 0x00, 0x07, 0x00, 0x00, 0x00, // U+0072 (r)
    0x00, 0x00, 0x7C, 0xFC, 0xC4, 0x06, 0x04, 0x00, 0x00, 0x08, 0x0C, 0x06, 0x07, 0x00, 0x00, 0x00, // U+0073 (s)
    0x00, 0x8E, 0xFE, 0x3E, 0x06, 0x06, 0x00, 0x00, 0x00, 0x0F, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x00, // U+0074 (t)
    0x00, 0xF8, 0xFC, 0x1E, 0x80, 0xF8, 0xFE, 0x02, 0x00, 0x07, 0x07, 0x06, 0x03, 0x03, 0x00, 0x00, // U+0075 (u)
    0x00, 0xFC, 0xFC, 0x00, 0xE0, 0xF0, 0x1C, 0x00, 0x00, 0x03, 0x0F, 0x07, 0x03, 0x00, 0x00, 0x00, // U+0076 (v)
    0xE0, 0xFC, 0x00, 0xC0, 0xC0, 0x80, 0xFC, 0x04, 0x01, 0x1F, 0x0F, 0x0F, 0x0F, 0x0F, 0x00, 0x00, // U+0077 (w)
    0x00, 0x00, 0x1C, 0xFE, 0xF0, 0xF0, 0x0E, 0x02, 0x00, 0x0C, 0x07, 0x03, 0x03, 0x0F, 0x00, 0x00, // U+0078 (x)
    0x00, 0x3C, 0xF8, 0xF0, 0x7C, 0x0E, 0x00, 0x00, 0x00, 0x18, 0x0F, 0x01, 0x00, 0x00, 0x00, 0x00, // U+0079 (y)
    0x00, 0x30, 0x30, 0x90, 0xD0, 0xF0, 0x70, 0x30, 0x08, 0x0E, 0x07, 0x07, 0x05, 0x04, 0x06, 0x00, // U+007A (z)
    0x38, 0x0C, 0x0C, 0x07, 0x0C, 0x0C, 0x38, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // U+007B ({) noch nicht erzeugt nur offsets
    0x18, 0x18, 0x18, 0x00, 0x18, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // U+007C (|) noch nicht erzeugt nur offsets
    0x07, 0x0C, 0x0C, 0x38, 0x0C, 0x0C, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // U+007D (}) noch nicht erzeugt nur offsets
    0x6E, 0x3B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // U+007E (~) noch nicht erzeugt nur offsets
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 // U+007F
};
