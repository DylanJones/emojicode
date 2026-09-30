#include <stdio.h>

void ejcInit(int argc, char **argv);

int ejcTestAdd(int a, int b);
void ejcTestGreet(const char *name);
short ejcTestNegate(short value);
int ejcTestApply(int (*function)(int), int value);

double ejcTestScale(double value, double factor);
float ejcTestHalve(float value);
_Bool ejcTestIsPositive(int value);
unsigned char ejcTestAddByte(unsigned char a, unsigned char b);
unsigned long long ejcTestIncrement(unsigned long long value);
int ejcTestMeasure(const char *string, int length);

static int triple(int value) {
    return value * 3;
}

int main(int argc, char **argv) {
    ejcInit(argc, argv);
    printf("%d\n", ejcTestAdd(2, 40));
    fflush(stdout);
    ejcTestGreet("C host");
    printf("%d\n", ejcTestNegate(-1234));
    printf("%d\n", ejcTestApply(triple, 14));
    printf("%.3f\n", ejcTestScale(1.5, -4.25));
    printf("%.3f\n", ejcTestHalve(5.0f));
    printf("%d %d\n", ejcTestIsPositive(7), ejcTestIsPositive(-7));
    printf("%u\n", ejcTestAddByte(200, 100));
    printf("%llu\n", ejcTestIncrement(18446744073709551614ull));
    printf("%llu\n", ejcTestIncrement(9223372036854775807ull));
    fflush(stdout);
    printf("%d\n", ejcTestMeasure("ab\0c\xc3\xa9", 6));
    return 0;
}
