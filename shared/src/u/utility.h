#ifndef _U_UTILITY_H_
#define _U_UTILITY_H_

#include <stddef.h>

#define uasize(_array) (sizeof(_array) / sizeof(_array[0]))

#define umin(l, r) (l > r ? r : l)
#define umax(l, r) (l > r ? l : r)
#define uclamp(val, l, h) (val < l ? l : (val > h ? h : val))

_Noreturn void fatal(const char* msg);
_Noreturn void fatalf(const char* fmt, ...);

char* hexstr(char* buf, size_t bufsize, const void* data, size_t datalen);
char* ahexstr(const void* data, size_t datalen);

#endif	// _U_UTILITY_H_
