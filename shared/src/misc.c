#include "misc.h"

#include <math.h>
#include <stdio.h>

#include <gnm/drawcommandbuffer.h>
#include <gnm/gnf/gnf.h>
#include <gnm/gpuaddr/gpuaddr.h>
#include <gnm/platform.h>
#include <gnm/pssl/types.h>

#define STB_IMAGE_IMPLEMENTATION
#include "libs/stb_image.h"

#include "u/fs.h"
#include "u/utility.h"

bool initcolortarget(
	GnmRenderTarget* fbtarget, MemoryAllocator* garlicmem, uint32_t width,
	uint32_t height, GnmDataFormat colorfmt, GnmGpuMode gpumode
) {
	const uint32_t colorbpp = gnmDfGetBitsPerElement(colorfmt);
	const uint32_t numfragsperpixel = 1;
	GpaSurfaceProperties surfprops = {0};
	GpaError err = gpaFindOptimalSurface(
		&surfprops, GPA_SURFACE_COLORDISPLAY, colorbpp, numfragsperpixel, false,
		gpumode
	);
	if (err != GPA_ERR_OK) {
		printf("gpaFindOptimalSurface failed with %s\n", gpaStrError(err));
		return false;
	}

	const GnmRenderTargetCreateInfo ci = {
		.width = width,
		.height = height,
		.pitch = 0,
		.numslices = 1,

		.colorfmt = colorfmt,
		.colortilemodehint = surfprops.tilemode,
		.mingpumode = gpumode,
		.numsamples = 1,
		.numfragments = 1,
	};
	GnmError gnmerr = gnmCreateRenderTarget(fbtarget, &ci);
	if (gnmerr != GNM_ERROR_OK) {
		printf("CreateRenderTarget failed with %s\n", gnmStrError(gnmerr));
		return false;
	}

	uint64_t fbsize = 0;
	uint32_t fbalign = 0;
	gnmerr = gnmRtCalcByteSize(&fbsize, &fbalign, fbtarget);
	if (gnmerr != GNM_ERROR_OK) {
		printf("initfb: failed to get rt size with %s\n", gnmStrError(gnmerr));
		return false;
	}

	void* fbmem = memalloc_alloc(garlicmem, fbsize, fbalign);
	if (!fbmem) {
		puts("initfb: failed to allocate memory");
		return false;
	}

	gnmRtSetBaseAddr(fbtarget, fbmem);

	return true;
}

bool initdepthtarget(
	GnmDepthRenderTarget* outdrt, MemoryAllocator* garlicmem, uint32_t width,
	uint32_t height, uint32_t pitch, uint32_t numslices, uint32_t numfrags,
	GnmZFormat zfmt, GnmStencilFormat stencilfmt, GnmGpuMode mingpumode
) {
	const GpaSurfaceType depthsurftype = stencilfmt != GNM_STENCIL_INVALID
											 ? GPA_SURFACE_DEPTHSTENCIL
											 : GPA_SURFACE_DEPTH;

	const GnmDataFormat depthfmt = gnmDfInitFromZ(zfmt);
	const uint32_t depthbpp = gnmDfGetBitsPerElement(depthfmt);
	GpaSurfaceProperties surfprops = {0};
	GpaError err = gpaFindOptimalSurface(
		&surfprops, depthsurftype, depthbpp, numfrags, false, mingpumode
	);
	if (err != GPA_ERR_OK) {
		printf("Failed to compute depth tile mode with %s\n", gpaStrError(err));
		return false;
	}

	const GnmDepthRenderTargetCreateInfo ci = {
		.width = width,
		.height = height,
		.pitch = pitch,
		.numslices = numslices,

		.zfmt = zfmt,
		.stencilfmt = stencilfmt,
		.tilemodehint = surfprops.tilemode,
		.mingpumode = mingpumode,
		.numfragments = numfrags,
	};
	GnmError gerr = gnmCreateDepthRenderTarget(outdrt, &ci);
	if (gerr != GNM_ERROR_OK) {
		printf("Failed to init depth target with %s\n", gnmStrError(gerr));
		return false;
	}

	uint64_t depthsize = 0;
	uint32_t depthalign = 0;
	gerr = gnmDrtCalcByteSize(&depthsize, &depthalign, outdrt);
	if (gerr != GNM_ERROR_OK) {
		printf("Failed to calc depth size with %s\n", gnmStrError(gerr));
		return false;
	}

	void* depthmem = memalloc_alloc(garlicmem, depthsize, depthalign);
	if (!depthmem) {
		puts("Failed to alloc depth memory");
		return false;
	}

	gerr = gnmDrtSetZReadAddress(outdrt, depthmem);
	if (gerr != GNM_ERROR_OK) {
		printf("Failed to set depth read address with %s\n", gnmStrError(gerr));
		return false;
	}
	gerr = gnmDrtSetZWriteAddress(outdrt, depthmem);
	if (gerr != GNM_ERROR_OK) {
		printf(
			"Failed to set depth write address with %s\n", gnmStrError(gerr)
		);
		return false;
	}

	return true;
}

void setupviewport(
	GnmCommandBuffer* cmdbuf, uint32_t left, uint32_t top, uint32_t right,
	uint32_t bottom, float zscale, float zoffset
) {
	const int32_t width = right - left;
	const int32_t height = bottom - top;

	const GnmSetViewportInfo vp = {
	    .dmin = 0.0,
	    .dmax = 1.0,
	    .scale =
		{
		    width * 0.5f,
		    -height * 0.5f,
		    zscale,
		},
	    .offset =
		{
		    left + width * 0.5f,
		    top + height * 0.5f,
		    zoffset,
		},
	};
	gnmDrawCmdSetViewport(cmdbuf, 0, &vp);

	const GnmViewportTransformControl vtctrl = {
		.scalex = 1,
		.offsetx = 1,
		.scaley = 1,
		.offsety = 1,
		.scalez = 1,
		.offsetz = 1,
		.perspectivedividexy = 0,
		.perspectivedividez = 0,
		.invertw = 1,
	};
	gnmDrawCmdSetViewportTransformControl(cmdbuf, &vtctrl);

	gnmDrawCmdSetScreenScissor(cmdbuf, left, top, right, bottom);

	const int hwoffsetx =
		umin(508, (int)floor(vp.offset[0] / 16.0f + 0.5f)) & ~0x3;
	const int hwoffsety =
		umin(508, (int)floor(vp.offset[1] / 16.0f + 0.5f)) & ~0x3;
	gnmDrawCmdSetHwScreenOffset(cmdbuf, hwoffsetx, hwoffsety);

	const float hwmin = -(float)((1 << 23) - 0) / (float)(1 << 8);
	const float hwmax = (float)((1 << 23) - 1) / (float)(1 << 8);
	const float gbmaxx = umin(
		hwmax - fabsf(vp.scale[0]) - vp.offset[0] + hwoffsetx * 16,
		-fabsf(vp.scale[0]) + vp.offset[0] - hwoffsetx * 16 - hwmin
	);
	const float gbmaxy = umin(
		hwmax - fabsf(vp.scale[1]) - vp.offset[1] + hwoffsety * 16,
		-fabsf(vp.scale[1]) + vp.offset[1] - hwoffsety * 16 - hwmin
	);
	const float gbhorzclipadjust = gbmaxx / fabsf(vp.scale[0]);
	const float gbvertclipadjust = gbmaxy / fabsf(vp.scale[1]);
	gnmDrawCmdSetGuardBands(
		cmdbuf, gbhorzclipadjust, gbvertclipadjust, 1.0f, 1.0f
	);
}

static bool parseshader(
	const GnmShaderCommonData** outdata, const uint32_t** outcode,
	const void* data, size_t datasize
) {
	const size_t expectedheadersize =
		sizeof(PsslBinaryHeader) + sizeof(GnmShaderFileHeader) +
		sizeof(GnmShaderCommonData) + sizeof(uint32_t) * 2;
	if (datasize < expectedheadersize) {
		printf(
			"parseshader: datasize %lu is smaller than expectedheadersize\n",
			datasize
		);
		return false;
	}

	const GnmShaderFileHeader* shheader =
		(const GnmShaderFileHeader*)((const uint8_t*)data +
									 sizeof(PsslBinaryHeader));
	const GnmShaderCommonData* shcommon =
		(const GnmShaderCommonData*)((const uint8_t*)shheader +
									 sizeof(GnmShaderFileHeader));
	const uint32_t* sbaddr = (const uint32_t*)((const uint8_t*)shcommon +
											   sizeof(GnmShaderCommonData));

	if (shheader->magic != GNM_SHADER_FILE_HEADER_ID) {
		printf(
			"parseshader: shheader magic 0x%x is invalid\n", shheader->magic
		);
		return false;
	}
	if (sbaddr[0] & 3) {
		puts("parseshader: shader offset must be aligned to 4 bytes");
		return false;
	}
	if (sbaddr[1] != 0xffffffff) {
		puts("parseshader: shader has already been patched");
		return false;
	}

	*outdata = shcommon;
	*outcode = (const uint32_t*)((const uint8_t*)shcommon + sbaddr[0]);
	return true;
}

bool loadvshader(
	GnmVsShader** outvs, MemoryAllocator* garlicmem, const char* vspath
) {
	void* vsdata = NULL;
	size_t vssize = 0;
	int readres = readfile(vspath, &vsdata, &vssize);
	if (readres != 0) {
		printf(
			"loadvshader: failed to load %s with 0x%x\n", vspath, readres
		);
		return false;
	}

	const GnmShaderCommonData* vscommon = NULL;
	const uint32_t* vscode = NULL;
	if (!parseshader(&vscommon, &vscode, vsdata, vssize)) {
		puts("loavshader: failed to parse shader");
		return false;
	}

	const uint32_t vscodesize = gnmShaderCommonCodeSize(vscommon);
	const GnmVsShader* vsshader = (const GnmVsShader*)vscommon;
	const uint32_t vsshadersize = gnmVsShaderCalcSize(vsshader);

	GnmVsShader* vs = malloc(vsshadersize);
	if (!vs) {
		puts("loadvshader: failed to alloc shader memory");
		return false;
	}
	void* vsgpucode =
		memalloc_alloc(garlicmem, vscodesize, GNM_ALIGNMENT_SHADER_BYTES);
	if (!vsgpucode) {
		puts("loadvshader: failed to alloc code memory");
		return false;
	}

	memcpy(vs, vsshader, vsshadersize);
	memcpy(vsgpucode, vscode, vscodesize);
	gnmVsRegsSetAddress(&vs->registers, vsgpucode);

	free(vsdata);
	*outvs = vs;
	return true;
}

bool loadpshader(
	GnmPsShader** outps, MemoryAllocator* garlicmem, const char* pspath
) {
	void* psdata = NULL;
	size_t pssize = 0;
	int readres = readfile(pspath, &psdata, &pssize);
	if (readres != 0) {
		printf(
			"loadpshader: failed to load %s with 0x%x\n", pspath, readres
		);
		return false;
	}

	const GnmShaderCommonData* pscommon = NULL;
	const uint32_t* pscode = NULL;
	if (!parseshader(&pscommon, &pscode, psdata, pssize)) {
		puts("loadpshader: failed to parse shader");
		return false;
	}

	const uint32_t pscodesize = gnmShaderCommonCodeSize(pscommon);
	const GnmPsShader* psshader = (const GnmPsShader*)pscommon;
	const uint32_t psshadersize = gnmPsShaderCalcSize(psshader);

	GnmPsShader* ps = malloc(psshadersize);
	if (!ps) {
		puts("loadpshader: failed to alloc shader memory");
		return false;
	}
	void* psgpucode =
		memalloc_alloc(garlicmem, pscodesize, GNM_ALIGNMENT_SHADER_BYTES);
	if (!psgpucode) {
		puts("loadpshader: failed to alloc code memory");
		return false;
	}

	memcpy(ps, psshader, psshadersize);
	memcpy(psgpucode, pscode, pscodesize);
	gnmPsRegsSetAddress(&ps->registers, psgpucode);

	ps->registers.spibaryccntl = 0;

	free(psdata);
	*outps = ps;
	return true;
}

bool loadgnftexture(
	MemoryAllocator* garlicmem, GnmTexture* outtexture, const char* path
) {
	void* data = NULL;
	size_t datasize = 0;
	int readres = readfile(path, &data, &datasize);
	if (readres != 0) {
		printf("loadgnftexture: failed to load file with %i\n", readres);
		return false;
	}

	const GnfHeader* header = data;
	if (sizeof(GnfHeader) + sizeof(GnfContents) > datasize) {
		puts("loadgnftexture: is too small to have a header and contents");
		return false;
	}
	if (header->magic != GNF_HEADER_MAGIC) {
		printf("loadgnftexture: magic 0x%x does not match\n", header->magic);
		return false;
	}

	const GnfContents* contents =
		(const GnfContents*)((const uint8_t*)header + sizeof(GnfHeader));
	if (contents->version != 2) {
		printf(
			"loadgnftexture: version %u is unsupported\n", contents->version
		);
		return false;
	}

	const uint32_t texsize = gnfGetTextureSize(header, 0);
	uint32_t texalign = gnfGetTextureAlignment(header, 0);
	if (texalign > GNM_ALIGNMENT_SHADER_BYTES) {
		texalign = GNM_ALIGNMENT_SHADER_BYTES;
	}
	void* texmem = memalloc_alloc(garlicmem, texsize, texalign);
	if (!texmem) {
		puts("loadgnftexture: failed to alloc texture memory");
		return false;
	}

	const uint32_t texoffset = gnfGetTextureOffset(header, 0);

	memcpy(texmem, (const uint8_t*)data + texoffset, texsize);

	*outtexture = contents->textures[0];
	gnmTexSetBaseAddress(outtexture, texmem);
	outtexture->metadataaddr = 0;

	free(data);
	return true;
}

bool loadlineartexturefile(
	MemoryAllocator* garlicmem, GnmTexture* outtexture, const char* path
) {
	int width = 0, height = 0, numchans = 0;
	stbi_uc* data = stbi_load(path, &width, &height, &numchans, STBI_rgb_alpha);
	if (!data) {
		printf("Failed to load texture %s\n", path);
		return false;
	}
	if (numchans != 4) {
		printf("Unexpected %i texture channels\n", numchans);
		free(data);
		return false;
	}

	const GnmDataFormat fmt = GNM_FMT_R8G8B8A8_SRGB;
	const size_t datasize = width * height * numchans;

	void* texmem =
		memalloc_alloc(garlicmem, datasize, GNM_ALIGNMENT_SHADER_BYTES);
	if (!texmem) {
		printf("Failed to allocate 0x%zx bytes\n", datasize);
		free(data);
		return false;
	}

	memcpy(texmem, data, datasize);
	free(data);

	const GnmTextureCreateInfo ci = {
		.texturetype = GNM_TEXTURE_2D,
		.width = width,
		.height = height,
		.depth = 1,
		.pitch = width,
		.nummiplevels = 1,
		.numslices = 1,
		.format = fmt,
		.tilemodehint = GNM_TM_DISPLAY_LINEAR_GENERAL,
		.mingpumode = GNM_GPU_BASE,
		.numfragments = 1,
	};
	GnmError gerr = gnmCreateTexture(outtexture, &ci);
	if (gerr != GNM_ERROR_OK) {
		memalloc_free(garlicmem, texmem);
		return false;
	}

	gnmTexSetBaseAddress(outtexture, texmem);
	return true;
}

bool loadlineartexturemem(
	MemoryAllocator* garlicmem, GnmTexture* outtexture, const void* indata,
	size_t indatasize
) {
	int width = 0, height = 0, numchans = 0;
	stbi_uc* data = stbi_load_from_memory(
		indata, indatasize, &width, &height, &numchans, STBI_rgb_alpha
	);
	if (!data) {
		puts("Failed to load texture in memory");
		return false;
	}
	if (numchans != 4) {
		printf("Unexpected %i texture channels\n", numchans);
		free(data);
		return false;
	}

	const GnmDataFormat fmt = GNM_FMT_R8G8B8A8_SRGB;
	const size_t datasize = width * height * numchans;

	void* texmem =
		memalloc_alloc(garlicmem, datasize, GNM_ALIGNMENT_SHADER_BYTES);
	if (!texmem) {
		printf("Failed to allocate 0x%zx bytes\n", datasize);
		free(data);
		return false;
	}

	memcpy(texmem, data, datasize);
	free(data);

	const GnmTextureCreateInfo ci = {
		.texturetype = GNM_TEXTURE_2D,
		.width = width,
		.height = height,
		.depth = 1,
		.pitch = width,
		.nummiplevels = 1,
		.numslices = 1,
		.format = fmt,
		.tilemodehint = GNM_TM_DISPLAY_LINEAR_GENERAL,
		.mingpumode = GNM_GPU_BASE,
		.numfragments = 1,
	};
	GnmError gerr = gnmCreateTexture(outtexture, &ci);
	if (gerr != GNM_ERROR_OK) {
		memalloc_free(garlicmem, texmem);
		return false;
	}

	gnmTexSetBaseAddress(outtexture, texmem);
	return true;
}

typedef struct {
	GnmBuffer buf;
} ClearDescSet;
_Static_assert(sizeof(ClearDescSet) == 0x10, "");

static GnmPsShader* s_psclear = NULL;

bool initclearutility(MemoryAllocator* garlicmem) {
	// setup pixel clear shader
	if (!loadpshader(
			&s_psclear, garlicmem, "/app0/assets/misc/clear.frag.sb"
		)) {
		puts("clear: failed to load clear pixel shader");
		return false;
	}

	return true;
}

void clearcolortarget(
	GnmCommandBuffer* cmd, GnmRenderTarget* rt, const float clearvalue[4]
) {
	if (!s_psclear) {
		fatal("clearcolortarget: utility was not initialized");
	}

	// set controls
	const GnmDbRenderControl dbrenderctrl = {
		.depthclearenable = false,
		.stencilclearenable = false,
		.htileresummarizeenable = false,
		.depthwritebackpol = false,
		.stencilwritebackpol = false,
		.forcedepthdecompress = false,
		.copycentroidenable = false,
		.copysampleindex = 0,
		.copydepthtocolorenable = false,
		.copystenciltocolorenable = false,
	};
	const GnmDepthStencilControl depthstencilctrl = {
		.zwrite = false,
		.zfunc = GNM_DEPTH_COMPARE_NEVER,
		.stencilfunc = GNM_DEPTH_COMPARE_NEVER,
		.stencilbackfunc = GNM_DEPTH_COMPARE_NEVER,
		.separatestencilenable = false,
		.depthenable = false,
		.stencilenable = false,
		.depthboundsenable = false,
	};
	gnmDrawCmdSetDbRenderControl(cmd, &dbrenderctrl);
	gnmDrawCmdSetDepthStencilControl(cmd, &depthstencilctrl);

	// set shader
	gnmDrawCmdSetEmbeddedVsShader(cmd, GNM_EMBEDDED_VSH_FULLSCREEN, 0);
	gnmDrawCmdSetPsShader(cmd, &s_psclear->registers);

	// set color const buffer
	float* colorbuf = gnmCmdAllocInside(cmd, sizeof(float) * 4, 4);
	assert(colorbuf);
	for (uint8_t i = 0; i < 4; i += 1) {
		colorbuf[i] = clearvalue[i];
	}

	ClearDescSet* descset = gnmCmdAllocInside(cmd, sizeof(ClearDescSet), 4);
	*descset = (ClearDescSet) {
		.buf = gnmCreateConstBuffer(colorbuf, sizeof(float) * 4),
	};
	gnmDrawCmdSetPointerUserData(cmd, GNM_STAGE_PS, 0, descset);

	// set viewport
	const uint32_t width = rt->size.width;
	const uint32_t height = rt->size.height;
	setupviewport(cmd, 0, 0, width, height, 0.5, 0.5);

	// draw to render target
	gnmDrawCmdSetRenderTarget(cmd, 0, rt);
	gnmDrawCmdSetRenderTargetMask(cmd, 0xf);

	// draw fullscreen quad
	gnmDrawCmdSetPrimitiveType(cmd, GNM_PT_RECTLIST);
	gnmDrawCmdDrawIndexAuto(cmd, 3);
}

void cleardepthtarget(
	GnmCommandBuffer* cmd, GnmDepthRenderTarget* drt, float clearvalue
) {
	if (!s_psclear) {
		fatal("cleardepthtarget: utility was not initialized");
	}

	// set controls
	const GnmDbRenderControl dbrenderctrl = {
		.depthclearenable = true,
		.stencilclearenable = false,
		.htileresummarizeenable = false,
		.depthwritebackpol = false,
		.stencilwritebackpol = false,
		.forcedepthdecompress = false,
		.copycentroidenable = false,
		.copysampleindex = 0,
		.copydepthtocolorenable = false,
		.copystenciltocolorenable = false,
	};
	const GnmDepthStencilControl depthstencilctrl = {
		.zwrite = true,
		.zfunc = GNM_DEPTH_COMPARE_ALWAYS,
		.stencilfunc = GNM_DEPTH_COMPARE_NEVER,
		.stencilbackfunc = GNM_DEPTH_COMPARE_NEVER,
		.separatestencilenable = false,
		.depthenable = true,
		.stencilenable = false,
		.depthboundsenable = false,
	};
	gnmDrawCmdSetDbRenderControl(cmd, &dbrenderctrl);
	gnmDrawCmdSetDepthStencilControl(cmd, &depthstencilctrl);

	gnmDrawCmdSetDepthClearValue(cmd, clearvalue);

	// don't draw to color render targets
	gnmDrawCmdSetRenderTargetMask(cmd, 0);

	// set shader
	gnmDrawCmdSetEmbeddedVsShader(cmd, GNM_EMBEDDED_VSH_FULLSCREEN, 0);
	gnmDrawCmdSetPsShader(cmd, &s_psclear->registers);

	// set color const buffer
	// it's unused here since there aren't any color render targets bound
	float* colorbuf = gnmCmdAllocInside(cmd, sizeof(float) * 4, 4);
	assert(colorbuf);
	for (uint8_t i = 0; i < 4; i += 1) {
		colorbuf[i] = 0.0;
	}

	ClearDescSet* descset = gnmCmdAllocInside(cmd, sizeof(ClearDescSet), 4);
	*descset = (ClearDescSet) {
		.buf = gnmCreateConstBuffer(colorbuf, sizeof(float) * 4),
	};
	gnmDrawCmdSetPointerUserData(cmd, GNM_STAGE_PS, 0, descset);

	// set viewport
	const uint32_t width = gnmDrtGetWidth(drt);
	const uint32_t height = gnmDrtGetHeight(drt);
	setupviewport(cmd, 0, 0, width, height, 0.5, 0.5);

	gnmDrawCmdSetDepthRenderTarget(cmd, drt);

	// draw fullscreen quad
	gnmDrawCmdSetPrimitiveType(cmd, GNM_PT_RECTLIST);
	gnmDrawCmdSetIndexSize(cmd, GNM_INDEX_16, GNM_POLICY_LRU);
	gnmDrawCmdDrawIndexAuto(cmd, 3);
}
