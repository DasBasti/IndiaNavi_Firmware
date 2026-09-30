#ifndef PLATINENMACHER_HELPER_H_
#define PLATINENMACHER_HELPER_H_

#include <stddef.h>
#include <stdint.h>

/*
 * Read one line from string
 */
char *readline(char *source, char *destination);
char *readline_n(char *source, char *destination, size_t size);
size_t countline(char *source);

/*
 * use Printf to print out a framebuffer with numbers
 */
void printf_fb(uint8_t *fb, uint32_t width, uint32_t height);

void convert_umlauts_inplace(char* text);

#endif /* PLATINENMACHER_HELPER_H_ */