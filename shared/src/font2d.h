#ifndef _RENDERER_FONT2D_H_
#define _RENDERER_FONT2D_H_

#include <gnm/texture.h>

#include "libs/stb_truetype.h"

#include "memalloc.h"

typedef struct {
	GnmTexture bitmaptexture;
	stbtt_bakedchar chardata[96];  // ASCII 32 to 126
} R_Font2D;

R_Font2D r_LoadFontFromFile(
	const char* path, uint32_t fontsize, MemoryAllocator* mem
);

#endif	// _RENDERER_FONT2D_H_
