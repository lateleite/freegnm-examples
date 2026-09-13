#ifndef _MISC_H_
#define _MISC_H_

#include <gnm/commandbuffer.h>
#include <gnm/depthrendertarget.h>
#include <gnm/rendertarget.h>
#include <gnm/shaderbinary.h>
#include <gnm/texture.h>

#include "memalloc.h"

bool initcolortarget(
	GnmRenderTarget* fbtarget, MemoryAllocator* garlicmem, uint32_t width,
	uint32_t height, GnmDataFormat colorfmt, GnmGpuMode gpumode
);
bool initdepthtarget(
	GnmDepthRenderTarget* outdrt, MemoryAllocator* garlicmem, uint32_t width,
	uint32_t height, uint32_t pitch, uint32_t numslices, uint32_t numfrags,
	GnmZFormat zfmt, GnmStencilFormat stencilfmt, GnmGpuMode mingpumode
);
void setupviewport(
	GnmCommandBuffer* cmdbuf, uint32_t left, uint32_t top, uint32_t right,
	uint32_t bottom, float zscale, float zoffset
);

bool loadvshader(
	GnmVsShader** outvs, MemoryAllocator* garlicmem, const char* vspath
);
bool loadpshader(
	GnmPsShader** outps, MemoryAllocator* garlicmem, const char* pspath
);

bool loadgnftexture(
	MemoryAllocator* garlicmem, GnmTexture* outtexture, const char* path
);
bool loadlineartexturefile(
	MemoryAllocator* garlicmem, GnmTexture* outtexture, const char* path
);
bool loadlineartexturemem(
	MemoryAllocator* garlicmem, GnmTexture* outtexture, const void* indata,
	size_t indatasize
);

bool initclearutility(MemoryAllocator* garlicmem);
void clearcolortarget(
	GnmCommandBuffer* cmd, GnmRenderTarget* rt, const float clearvalue[4]
);
void cleardepthtarget(
	GnmCommandBuffer* cmd, GnmDepthRenderTarget* drt, float clearvalue
);

#endif	// _MISC_H_
