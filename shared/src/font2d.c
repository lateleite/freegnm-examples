#include "font2d.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include "libs/stb_truetype.h"

#include "u/fs.h"
#include "u/utility.h"

R_Font2D r_LoadFontFromFile(
	const char* path, uint32_t fontsize, MemoryAllocator* mem
) {
	void* data = NULL;
	size_t datasize = 0;
	int res = readfile(path, &data, &datasize);
	if (res != 0) {
		fatalf("font: failed to read %s with: %s\n", path, strerror(res));
	}

	const uint32_t bmwidth = 512;
	const uint32_t bmheight = 512;
	const GnmDataFormat bmfmt = GNM_FMT_A8_UNORM;
	void* bmdata =
		memalloc_alloc(mem, bmwidth * bmheight * 1, GNM_ALIGNMENT_SHADER_BYTES);
	assert(bmdata);

	R_Font2D newfont = {0};

	stbtt_BakeFontBitmap(
		data, 0, fontsize, bmdata, bmwidth, bmheight, 32, 96, newfont.chardata
	);

	free(data);

	const GnmTextureCreateInfo ci = {
		.texturetype = GNM_TEXTURE_2D,
		.width = bmwidth,
		.height = bmheight,
		.depth = 1,
		.pitch = bmwidth,
		.nummiplevels = 1,
		.numslices = 1,
		.format = bmfmt,
		.tilemodehint = GNM_TM_DISPLAY_LINEAR_GENERAL,
		.mingpumode = GNM_GPU_BASE,
		.numfragments = 1,
	};
	GnmError gerr = gnmCreateTexture(&newfont.bitmaptexture, &ci);
	if (gerr != GNM_ERROR_OK) {
		fatalf("font: failed to create texture with: %s\n", gnmStrError(gerr));
	}

	gnmTexSetBaseAddress(&newfont.bitmaptexture, bmdata);

	return newfont;
}
