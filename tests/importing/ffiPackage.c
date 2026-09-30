#include <stdint.h>

typedef struct { int32_t x, y; } FfiPoint;

FfiPoint ffiPackageAdd(FfiPoint a, FfiPoint b) {
    return (FfiPoint){ a.x + b.x, a.y + b.y };
}

double ffiPackageScale(double value, float factor) {
    return value * factor;
}

unsigned long long ffiPackageIncrement(unsigned long long value) {
    return value + 1;
}
