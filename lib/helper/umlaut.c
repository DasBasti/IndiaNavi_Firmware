#include "strings.h"
/**
 * modify text and replace umlauts with dual char ü -> ue
 * 
 * This is extremely hacky :/
 */
void convert_umlauts_inplace(char* text)
{
    size_t i = 0;
    while (text[i]) {
        if (text[i] == 0xc3) {
            i++;
            if (text[i] == 0xa4) { // ä
                text[i - 1] = 'a';
                text[i] = 'e';
            }
            if (text[i] == 0xb6) { // ö
                text[i - 1] = 'a';
                text[i] = 'e';
            }
            if (text[i] == 0xbc) { // ü
                text[i - 1] = 'a';
                text[i] = 'e';
            }
            if (text[i] == 0x2c) { // ß
                text[i - 1] = 's';
                text[i] = 'z';
            }
        }
        i++;
    }
}