#include <stdio.h>
uint8_t SD[]={
0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 
0x77, 0x77, 0x70, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x77, 0x77, 
0x77, 0x77, 0x00, 0x06, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x00, 0x07, 0x77, 
0x77, 0x77, 0x06, 0x60, 0x06, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x00, 0x66, 0x07, 0x77, 
0x77, 0x77, 0x06, 0x66, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x66, 0x66, 0x07, 0x77, 
0x77, 0x77, 0x06, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x07, 0x77, 
0x77, 0x77, 0x06, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x07, 0x77, 
0x77, 0x77, 0x06, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x07, 0x77, 
0x77, 0x77, 0x06, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x07, 0x77, 
0x77, 0x77, 0x06, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x07, 0x77, 
0x77, 0x77, 0x70, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x07, 0x77, 
0x77, 0x77, 0x77, 0x06, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x07, 0x77, 
0x77, 0x77, 0x77, 0x06, 0x66, 0x66, 0x66, 0x00, 0x66, 0x00, 0x06, 0x66, 0x66, 0x66, 0x07, 0x77, 
0x77, 0x77, 0x00, 0x06, 0x66, 0x66, 0x60, 0x66, 0x06, 0x06, 0x60, 0x66, 0x66, 0x66, 0x07, 0x77, 
0x77, 0x77, 0x06, 0x66, 0x66, 0x66, 0x06, 0x66, 0x66, 0x06, 0x66, 0x06, 0x66, 0x66, 0x07, 0x77, 
0x77, 0x77, 0x06, 0x66, 0x66, 0x66, 0x06, 0x66, 0x66, 0x06, 0x66, 0x06, 0x66, 0x66, 0x07, 0x77, 
0x77, 0x77, 0x06, 0x66, 0x66, 0x66, 0x60, 0x00, 0x66, 0x06, 0x66, 0x06, 0x66, 0x66, 0x07, 0x77, 
0x77, 0x77, 0x70, 0x66, 0x66, 0x66, 0x66, 0x66, 0x06, 0x06, 0x66, 0x06, 0x66, 0x66, 0x07, 0x77, 
0x77, 0x77, 0x77, 0x06, 0x66, 0x66, 0x66, 0x66, 0x06, 0x06, 0x66, 0x06, 0x66, 0x66, 0x07, 0x77, 
0x77, 0x77, 0x77, 0x70, 0x66, 0x66, 0x06, 0x60, 0x66, 0x06, 0x60, 0x66, 0x66, 0x66, 0x07, 0x77, 
0x77, 0x77, 0x77, 0x77, 0x06, 0x66, 0x60, 0x06, 0x66, 0x00, 0x06, 0x66, 0x66, 0x66, 0x07, 0x77, 
0x77, 0x77, 0x77, 0x77, 0x06, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x07, 0x77, 
0x77, 0x77, 0x77, 0x77, 0x06, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x07, 0x77, 
0x77, 0x77, 0x77, 0x77, 0x06, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x07, 0x77, 
0x77, 0x77, 0x77, 0x77, 0x06, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x07, 0x77, 
0x77, 0x77, 0x77, 0x77, 0x06, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x07, 0x77, 
0x77, 0x77, 0x77, 0x77, 0x06, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x07, 0x77, 
0x77, 0x77, 0x77, 0x77, 0x06, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x07, 0x77, 
0x77, 0x77, 0x77, 0x77, 0x06, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x07, 0x77, 
0x77, 0x77, 0x77, 0x77, 0x70, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x60, 0x77, 0x77, 
0x77, 0x77, 0x77, 0x77, 0x77, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0x77, 0x77, 
0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 
};