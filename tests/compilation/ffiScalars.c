#include <stdbool.h>

short ffiScalarsShortAdd(short a, short b) {
    return a + b;
}

unsigned int ffiScalarsWiden(unsigned char value) {
    return value * 100u;
}

bool ffiScalarsIsEven(int value) {
    return value % 2 == 0;
}
