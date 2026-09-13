#ifndef _U_FS_H_
#define _U_FS_H_

#include <stddef.h>

int readfile(const char* path, void** outdata, size_t* outsize);

#endif	// _U_FS_H_
