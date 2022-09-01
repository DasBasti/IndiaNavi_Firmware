#ifndef PLATINENMACHER_HELPER_H_
#define PLATINENMACHER_HELPER_H_

/*
 * Read one line from string
 */
char *readline(char *source, char *destination);

/*
 * use Printf to print out a framebuffer with numbers
 */
void printf_fb(uint8_t *fb, uint32_t width, uint32_t height);

#endif /* PLATINENMACHER_HELPER_H_ */