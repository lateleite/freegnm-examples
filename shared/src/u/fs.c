#include "fs.h"

#include <assert.h>
#include <stdlib.h>

#include <fcntl.h>
#include <orbis/libkernel.h>

int readfile(const char* path, void** outdata, size_t* outsize) {
	assert(outdata);
	assert(outsize);

	int realfile = sceKernelOpen(path, O_RDONLY, 0);
	if (realfile < 0) {
		return realfile;
	}

	const off_t filesize = sceKernelLseek(realfile, 0, SEEK_END);
	sceKernelLseek(realfile, 0, SEEK_SET);

	void* filedata = malloc(filesize);
	assert(filedata);

	int res = sceKernelRead(realfile, filedata, filesize);
	if (res < 0) {
		free(filedata);
	}

	sceKernelClose(realfile);

	*outdata = filedata;
	*outsize = filesize;
	return res >= 0 ? 0 : res;
}
