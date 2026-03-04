#include <stdio.h>
int my_atoi(const char* s) {
    int result = 0;
    int sign = 1;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') { s++; }
    while (*s >= '0' && *s <= '9') {
        result = result * 10 + (*s - '0');
        s++;
    }
    return result * sign;
}

int main(int argc, char** argv) {
    if (argc > 2) {
        int a = my_atoi(argv[1]);
        int b = my_atoi(argv[2]);
        printf("%d", a + b);
        return a + b;
    } else {
        printf("-1");
        return -1;
    }
}