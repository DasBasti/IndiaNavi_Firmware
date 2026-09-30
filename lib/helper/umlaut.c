#include <stddef.h>
/**
 * modify text and replace UTF-8 umlauts with dual char ü -> ue
 *
 * All umlauts are two bytes in UTF-8, so the replacement fits in place.
 */
void convert_umlauts_inplace(char* text)
{
    unsigned char* t = (unsigned char*)text;
    size_t i = 0;

    if (!t)
        return;

    while (t[i]) {
        if (t[i] == 0xc3 && t[i + 1]) {
            char a = 0, b = 0;
            switch (t[i + 1]) {
            case 0xa4: a = 'a'; b = 'e'; break; // ä
            case 0xb6: a = 'o'; b = 'e'; break; // ö
            case 0xbc: a = 'u'; b = 'e'; break; // ü
            case 0x84: a = 'A'; b = 'e'; break; // Ä
            case 0x96: a = 'O'; b = 'e'; break; // Ö
            case 0x9c: a = 'U'; b = 'e'; break; // Ü
            case 0x9f: a = 's'; b = 's'; break; // ß
            default: break;
            }
            if (a) {
                t[i] = a;
                t[i + 1] = b;
            }
            i++; // skip second byte of the UTF-8 sequence
        }
        i++;
    }
}
