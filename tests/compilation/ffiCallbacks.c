short ffiCallbacksApplyShort(short (*apply)(short), short value) {
    return apply(value) + 1;
}

static int square(int value) {
    return value * value;
}

int (*ffiCallbacksGetSquare(void))(int) {
    return square;
}
