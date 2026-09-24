#include <stdint.h>

typedef struct { int32_t x, y; } Point;
typedef struct { double re, im; } Complex;
typedef struct { int64_t a, b, c; float d; } Big;
typedef struct { Point min; Point max; } Rect;
typedef struct { char tag; short value; } Small;

Point ffiPointAdd(Point a, Point b) {
    return (Point){ a.x + b.x, a.y + b.y };
}

Complex ffiComplexMultiply(Complex a, Complex b) {
    return (Complex){ a.re * b.re - a.im * b.im, a.re * b.im + a.im * b.re };
}

Big ffiBigMake(int64_t a, int64_t b, int64_t c, float d) {
    return (Big){ a, b, c, d };
}

double ffiBigSum(Big big) {
    return big.a + big.b + big.c + big.d;
}

int32_t ffiRectArea(Rect rect) {
    return (rect.max.x - rect.min.x) * (rect.max.y - rect.min.y);
}

Small ffiSmallNext(Small small) {
    return (Small){ small.tag + 1, small.value * 2 };
}
