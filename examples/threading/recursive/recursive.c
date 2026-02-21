#include <stdio.h>

struct Struct {
    int m0;
    int m1;
    int m2;
} global;

int recursive(int depth) {
    static int count = 0;
    int call = count++;
    printf("recursive[%d]: depth=%d\n", call, depth);

    if (depth < 2)
        return 1;

    // Do NOT use tail-call to force actual recursion to occur!
    int next = recursive(depth - 1);

    if (global.m1 <= 0)
        global.m1 += next;
    else
        global.m1 -= next;

    int ret = next + depth;
    printf("recursive[%d]: m1=%d, next=%d, ret=%d\n", call, global.m1, next, ret);
    return ret;
}

int main() {
    global.m0 = 5;
    global.m1 = 0;
    global.m2 = recursive(global.m0);

    printf("Results:\n");
    printf("m0: %d\n", global.m0);
    printf("m1: %d\n", global.m1);
    printf("m2: %d\n", global.m2);

    return 0;
}
