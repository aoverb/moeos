#ifndef _CTYPE_H
#define _CTYPE_H

static inline int isdigit(int c)  { return c >= '0' && c <= '9'; }
static inline int isspace(int c)  { return c == ' ' || (c >= '\t' && c <= '\r'); }
static inline int iscntrl(int c)  { return (c >= 0 && c < 32) || c == 127; }
static inline int isalpha(int c)  { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }
static inline int isalnum(int c)  { return isalpha(c) || isdigit(c); }
static inline int toupper(int c)  { return (c >= 'a' && c <= 'z') ? c - 32 : c; }
static inline int tolower(int c)  { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

#endif