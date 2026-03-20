#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <ctype.h>

size_t strlen(const char* str) {
	size_t len = 0;
	while (str[len])
		len++;
	return len;
}

char* strchr(const char* s, int c) {
    while (*s) {
        if (*s == (char)c) return (char*)s;
        s++;
    }
    return c == '\0' ? (char*)s : nullptr;
}

int atoi(const char* s) {
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

char *strrchr(const char *s, int c) {
    const char *last = NULL;
    while (*s) {
        if (*s == (char)c) last = s;
        s++;
    }
    if ((char)c == '\0') return (char *)s;
    return (char *)last;
}

char *strdup(const char *s) {
    size_t len = strlen(s) + 1;
    char *d = (char*)malloc(len);
    if (d) memcpy(d, s, len);
    return d;
}

int strcasecmp(const char *s1, const char *s2) {
    while (*s1 && *s2) {
        int d = tolower((unsigned char)*s1) - tolower((unsigned char)*s2);
        if (d) return d;
        s1++; s2++;
    }
    return tolower((unsigned char)*s1) - tolower((unsigned char)*s2);
}

int strncasecmp(const char *s1, const char *s2, unsigned int n) {
    while (n-- && *s1 && *s2) {
        int d = tolower((unsigned char)*s1) - tolower((unsigned char)*s2);
        if (d) return d;
        s1++; s2++;
    }
    return n == (unsigned int)-1 ? 0 :
           tolower((unsigned char)*s1) - tolower((unsigned char)*s2);
}