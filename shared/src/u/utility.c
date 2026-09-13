#include "utility.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

_Noreturn void fatal(const char* msg) {
	puts(msg);
	exit(EXIT_FAILURE);
}

_Noreturn void fatalf(const char* fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);

	putc('\n', stdout);

	exit(EXIT_FAILURE);
}

char* hexstr(char* buf, size_t bufsize, const void* data, size_t datalen) {
	static const char hexmap[] = {'0', '1', '2', '3', '4', '5', '6', '7',
								  '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};

	const size_t requiredsize = datalen * 2 + 1;
	if (requiredsize > bufsize) {
		return NULL;
	}

	for (size_t i = 0; i < datalen; i += 1) {
		const uint8_t upper = (((const uint8_t*)data)[i] & 0xf0) >> 4;
		const uint8_t lower = ((const uint8_t*)data)[i] & 0xf;
		buf[i * 2] = hexmap[upper];
		buf[i * 2 + 1] = hexmap[lower];
	}
	buf[datalen * 2] = 0;

	return buf;
}

char* ahexstr(const void* data, size_t datalen) {
	const size_t requiredsize = datalen * 2 + 1;
	char* str = malloc(requiredsize);
	return hexstr(str, requiredsize, data, datalen);
}
