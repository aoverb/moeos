#include <stdlib.h>
#include <math.h>
int abs(int n) {
    return n < 0 ? -n : n;
}

long labs(long n) {
    return n < 0 ? -n : n;
}

double fabs(double x) {
    return x < 0 ? -x : x;
}
 
float fabsf(float x) {
    return x < 0 ? -x : x;
}

double atof(const char *s) {
    double result = 0.0;
    double sign = 1.0;
    
    while (*s == ' ') s++;
    if (*s == '-') { sign = -1.0; s++; }
    else if (*s == '+') { s++; }
    
    while (*s >= '0' && *s <= '9') {
        result = result * 10.0 + (*s - '0');
        s++;
    }
    
    if (*s == '.') {
        s++;
        double frac = 0.1;
        while (*s >= '0' && *s <= '9') {
            result += (*s - '0') * frac;
            frac *= 0.1;
            s++;
        }
    }
    
    return sign * result;
}