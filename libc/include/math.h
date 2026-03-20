#ifndef _MATH_H
#define _MATH_H

#define M_PI        3.14159265358979323846
#define M_PI_2      1.57079632679489661923
#define HUGE_VAL    __builtin_huge_val()
#define INFINITY    __builtin_inff()
#define NAN         __builtin_nanf("")
#ifdef __cplusplus
extern "C" {
#endif

double fabs(double x);
float  fabsf(float x);

double floor(double x);
double ceil(double x);
double sqrt(double x);

double sin(double x);
double cos(double x);
double tan(double x);
double atan(double x);
double atan2(double y, double x);

double pow(double base, double exp);
double fmod(double x, double y);
double log(double x);
#ifdef __cplusplus
}
#endif
#endif /* _MATH_H */