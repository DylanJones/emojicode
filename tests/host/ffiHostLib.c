#include <stdio.h>

void ejcInit(int argc, char **argv);

int ejcTestAdd(int a, int b);
void ejcTestGreet(const char *name);
short ejcTestNegate(short value);
int ejcTestApply(int (*function)(int), int value);

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
    return 0;
}
