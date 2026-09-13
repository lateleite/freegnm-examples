#ifndef NK_GNM_H_
#define NK_GNM_H_

#include <gnm/commandbuffer.h>

NK_API struct nk_context* nk_gnm_init(
	uint32_t screen_width, uint32_t screen_height, uint32_t max_vertex_memory,
	uint32_t max_element_memory
);
NK_API void nk_gnm_font_stash_begin(struct nk_font_atlas** atlas);
NK_API void nk_gnm_font_stash_end(void);
NK_API void nk_gnm_render(GnmCommandBuffer* cmdbuf, enum nk_anti_aliasing AA);
NK_API void nk_gnm_shutdown(void);

#endif

/*
 * ==============================================================
 *
 *                          IMPLEMENTATION
 *
 * ===============================================================
 */
#ifdef NK_GNM_IMPLEMENTATION

#include <math.h>
#include <stddef.h>
#include <string.h>

#include <orbis/libkernel.h>

#include <gnm/drawcommandbuffer.h>
#include <gnm/pssl/types.h>
#include <gnm/shaderbinary.h>

struct nk_gnm_memoryblock {
	off_t dmem;
	void* mapped;
	size_t len;
	size_t freeoff;
};

struct nk_gnm_vsdescset {
	GnmBuffer constbuf;
};

struct nk_gnm_psdescset {
	GnmTexture texture;
	GnmSampler sampler;
};

struct nk_gnm_device {
	struct nk_buffer cmds;
	struct nk_draw_null_texture tex_null;
	GnmVsShader* vsh;
	GnmPsShader* psh;
	void* vertmem;
	GnmBuffer* vertbuffers;
	void* indexmem;
	void* fetchshmem;
	float* cbufmem;
	GnmTexture font_tex;
	GnmSampler font_samp;
	nk_size max_vertex_memory;
	nk_size max_element_memory;
};

struct nk_gnm_vertex {
	float position[2];
	float uv[2];
	float col[4];
};

static struct nk_gnm {
	struct nk_gnm_device dev;
	struct nk_gnm_memoryblock garlicmem;
	struct nk_gnm_memoryblock onionmem;
	struct nk_context ctx;
	struct nk_font_atlas atlas;
	uint32_t screen_width;
	uint32_t screen_height;
} gnm;

NK_INTERN void nk_gnm_allocmemory(
	struct nk_gnm_memoryblock* outblock, size_t blocksize, int memtype
) {
	const size_t blockalign = 64 * 1024;  // 64 kB
	const size_t alignedlen =
		(blocksize + blockalign - 1) / blockalign * blockalign;

	int res = sceKernelAllocateDirectMemory(
		0, sceKernelGetDirectMemorySize(), alignedlen, blockalign, memtype,
		&outblock->dmem
	);
	NK_ASSERT(res == 0);

	res = sceKernelMapDirectMemory(
		&outblock->mapped, alignedlen,
		ORBIS_KERNEL_PROT_CPU_READ | ORBIS_KERNEL_PROT_CPU_RW |
			ORBIS_KERNEL_PROT_GPU_READ | ORBIS_KERNEL_PROT_GPU_WRITE,
		0, outblock->dmem, blockalign
	);
	NK_ASSERT(res == 0);

	outblock->len = alignedlen;
	outblock->freeoff = 0;
}
NK_INTERN void nk_gnm_freememory(struct nk_gnm_memoryblock* block) {
	sceKernelReleaseDirectMemory(block->dmem, block->len);
	NK_MEMSET(block, 0, sizeof(*block));
}

NK_INTERN void nk_gnm_parseshader(
	const GnmShaderCommonData** outdata, const uint32_t** outcode,
	const void* data, size_t datasize
) {
	const size_t expectedheadersize =
		sizeof(PsslBinaryHeader) + sizeof(GnmShaderFileHeader) +
		sizeof(GnmShaderCommonData) + sizeof(uint32_t) * 2;
	NK_ASSERT(datasize >= expectedheadersize);

	const GnmShaderFileHeader* shheader =
		(const GnmShaderFileHeader*)((const uint8_t*)data +
									 sizeof(PsslBinaryHeader));
	const GnmShaderCommonData* shcommon =
		(const GnmShaderCommonData*)((const uint8_t*)shheader +
									 sizeof(GnmShaderFileHeader));
	const uint32_t* sbaddr = (const uint32_t*)((const uint8_t*)shcommon +
											   sizeof(GnmShaderCommonData));

	NK_ASSERT(shheader->magic == GNM_SHADER_FILE_HEADER_ID);
	NK_ASSERT((sbaddr[0] & 3) == 0);
	NK_ASSERT(sbaddr[1] == 0xffffffff);

	*outdata = shcommon;
	*outcode = (const uint32_t*)((const uint8_t*)shcommon + sbaddr[0]);
}

NK_INTERN void* nk_gnm_reservemem(
	struct nk_gnm_memoryblock* block, size_t size, size_t alignment
) {
	const size_t alignedsize = (size + alignment - 1) / alignment * alignment;
	const size_t alignedoff =
		(block->freeoff + alignment - 1) / alignment * alignment;
	NK_ASSERT(alignedoff + alignedsize <= block->len);

	uint8_t* res = (uint8_t*)block->mapped + alignedoff;

	block->freeoff = alignedoff + alignedsize;
	return res;
}

/*
#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(binding = 0) uniform Constants {
    mat4 mproj;
} c;

layout(location = 0) in vec2 inpos;
layout(location = 1) in vec2 inuv;
layout(location = 2) in vec4 incolor;

layout(location = 0) out vec2 outuv;
layout(location = 1) out vec4 outcolor;

out gl_PerVertex {
	vec4 gl_Position;
};

void main() {
	gl_Position = c.mproj * vec4(inpos, 0.0, 1.0);
	outuv = inuv;
	outcolor = incolor;
}
*/
static const unsigned char nk_gnm_vsh[] = {
	0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x01, 0x00, 0x00, 0xe4, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x53, 0x68, 0x64, 0x72, 0x07, 0x00, 0x02, 0x00, 0x01, 0x12, 0x00, 0x01,
	0x00, 0x00, 0x00, 0x00, 0x8c, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00,
	0x48, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0x84, 0x00, 0x2c, 0x01,
	0x16, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x03, 0x02, 0x00, 0x00, 0x12, 0x00, 0x00, 0x00,
	0x17, 0x00, 0x02, 0x00, 0x1c, 0x00, 0x07, 0x00, 0x00, 0x04, 0x04, 0x00,
	0x01, 0x08, 0x04, 0x00, 0x02, 0x0c, 0x04, 0x00, 0x0f, 0x00, 0x10, 0x01,
	0x11, 0x02, 0x00, 0x00, 0x00, 0x21, 0x80, 0xbe, 0x00, 0x07, 0x80, 0xc0,
	0x7f, 0xc0, 0x8c, 0xbf, 0x04, 0x01, 0x82, 0xc2, 0x0c, 0x01, 0x84, 0xc2,
	0x00, 0x01, 0x80, 0xc2, 0x72, 0x00, 0x8c, 0xbf, 0x04, 0x0a, 0x0c, 0x10,
	0x05, 0x0a, 0x0e, 0x10, 0x71, 0x3f, 0x8c, 0xbf, 0x06, 0x0a, 0x14, 0x10,
	0x07, 0x0a, 0x0a, 0x10, 0x08, 0x0c, 0x0c, 0x06, 0x09, 0x0e, 0x0e, 0x06,
	0x0a, 0x14, 0x14, 0x06, 0x0b, 0x0a, 0x0a, 0x06, 0x00, 0x08, 0x0c, 0x3e,
	0x01, 0x08, 0x0e, 0x3e, 0x02, 0x08, 0x14, 0x3e, 0x03, 0x08, 0x0a, 0x3e,
	0xcf, 0x08, 0x00, 0xf8, 0x06, 0x07, 0x0a, 0x05, 0x03, 0x02, 0x00, 0xf8,
	0x08, 0x09, 0x80, 0x80, 0x70, 0x3f, 0x8c, 0xbf, 0x1f, 0x02, 0x00, 0xf8,
	0x0c, 0x0d, 0x0e, 0x0f, 0x00, 0x00, 0x81, 0xbf, 0x4f, 0x72, 0x62, 0x53,
	0x68, 0x64, 0x72, 0x07, 0x05, 0x70, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00,
};
/*
#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec2 inuv;
layout(location = 1) in vec4 incolor;

layout(location = 0) out vec4 outcol;

layout(set = 0, binding = 0) uniform sampler2D tex;

void main() {
	outcol = incolor * texture(tex, inuv);
}
*/
static const unsigned char nk_gnm_psh[] = {
	0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x01, 0x01, 0x00, 0x00, 0xe4, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x53, 0x68, 0x64, 0x72, 0x07, 0x00, 0x02, 0x00, 0x02, 0x11, 0x00, 0x01,
	0x00, 0x00, 0x00, 0x00, 0xa4, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
	0x44, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0x42, 0x00, 0x2c, 0x00,
	0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,
	0x02, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x01, 0x10, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x00,
	0x02, 0x00, 0x00, 0x00, 0x1c, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x10, 0x00,
	0x7e, 0x0a, 0xfe, 0xbe, 0x00, 0x01, 0xc4, 0xc0, 0x08, 0x01, 0x80, 0xc0,
	0x04, 0x03, 0xfc, 0xbe, 0x00, 0x00, 0x08, 0xc8, 0x01, 0x00, 0x09, 0xc8,
	0x00, 0x01, 0x0c, 0xc8, 0x01, 0x01, 0x0d, 0xc8, 0x7f, 0xc0, 0x8c, 0xbf,
	0x00, 0x0f, 0x80, 0xf0, 0x02, 0x04, 0x02, 0x00, 0x00, 0x04, 0x08, 0xc8,
	0x01, 0x04, 0x09, 0xc8, 0x00, 0x05, 0x0c, 0xc8, 0x01, 0x05, 0x0d, 0xc8,
	0x00, 0x06, 0x20, 0xc8, 0x01, 0x06, 0x21, 0xc8, 0x00, 0x07, 0x00, 0xc8,
	0x01, 0x07, 0x01, 0xc8, 0x70, 0x3f, 0x8c, 0xbf, 0x02, 0x09, 0x08, 0x10,
	0x03, 0x0b, 0x0a, 0x10, 0x08, 0x0d, 0x0c, 0x10, 0x00, 0x0f, 0x00, 0x10,
	0x04, 0x0b, 0x02, 0x5e, 0x06, 0x01, 0x00, 0x5e, 0x0f, 0x1c, 0x00, 0xf8,
	0x01, 0x00, 0x80, 0x80, 0x00, 0x00, 0x81, 0xbf, 0x4f, 0x72, 0x62, 0x53,
	0x68, 0x64, 0x72, 0x07, 0x01, 0x74, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00,
};

NK_INTERN void nk_gnm_device_create(
	uint32_t max_vertex_memory, uint32_t max_element_memory
) {
	struct nk_gnm_device* dev = &gnm.dev;
	nk_buffer_init_default(&dev->cmds);

	// setup GPU memory
	// 8mB
	nk_gnm_allocmemory(&gnm.garlicmem, 8 * 1024 * 1024, ORBIS_KERNEL_WC_GARLIC);
	// 2mB
	nk_gnm_allocmemory(&gnm.onionmem, 2 * 1024 * 1024, ORBIS_KERNEL_WB_ONION);

	// setup shaders
	const GnmShaderCommonData* vscommon = NULL;
	const uint32_t* vscode = NULL;
	nk_gnm_parseshader(
		&vscommon, &vscode, nk_gnm_vsh, sizeof(nk_gnm_vsh)
	);

	const uint32_t vscodesize = gnmShaderCommonCodeSize(vscommon);
	const GnmVsShader* vsshader = (const GnmVsShader*)vscommon;
	const uint32_t vsshadersize = gnmVsShaderCalcSize(vsshader);

	GnmVsShader* vs = nk_gnm_reservemem(
		&gnm.onionmem, vsshadersize, GNM_ALIGNMENT_BUFFER_BYTES
	);
	void* vsgpucode = nk_gnm_reservemem(
		&gnm.garlicmem, vscodesize, GNM_ALIGNMENT_SHADER_BYTES
	);

	NK_MEMCPY(vs, vsshader, vsshadersize);
	NK_MEMCPY(vsgpucode, vscode, vscodesize);
	gnmVsStageRegPatchGpuAddr(&vs->registers, vsgpucode);

	const GnmShaderCommonData* pscommon = NULL;
	const uint32_t* pscode = NULL;
	nk_gnm_parseshader(&pscommon, &pscode, nk_gnm_psh, sizeof(nk_gnm_psh));

	const uint32_t pscodesize = gnmShaderCommonCodeSize(pscommon);
	const GnmPsShader* psshader = (const GnmPsShader*)pscommon;
	const uint32_t psshadersize = gnmPsShaderCalcSize(psshader);

	GnmPsShader* ps = nk_gnm_reservemem(
		&gnm.onionmem, psshadersize, GNM_ALIGNMENT_BUFFER_BYTES
	);
	void* psgpucode = nk_gnm_reservemem(
		&gnm.garlicmem, pscodesize, GNM_ALIGNMENT_SHADER_BYTES
	);

	NK_MEMCPY(ps, psshader, psshadersize);
	NK_MEMCPY(psgpucode, pscode, pscodesize);
	gnmPsStageRegPatchGpuAddr(&ps->registers, psgpucode);

	dev->vsh = vs;
	dev->psh = ps;

	// setup vertex buffer
	dev->vertmem = nk_gnm_reservemem(
		&gnm.garlicmem, max_vertex_memory, GNM_ALIGNMENT_BUFFER_BYTES
	);

	dev->max_vertex_memory = max_vertex_memory;
	dev->max_element_memory = max_element_memory;

	const size_t maxvertcount =
		max_vertex_memory / sizeof(struct nk_gnm_vertex);
	const size_t posoff = offsetof(struct nk_gnm_vertex, position);
	const size_t uvoff = offsetof(struct nk_gnm_vertex, uv);
	const size_t coloff = offsetof(struct nk_gnm_vertex, col);

	// these buffer descriptors must be visible to the GPU.
	// buffer 0 is position
	// buffer 1 is UV
	// buffer 2 is color
	dev->vertbuffers = nk_gnm_reservemem(
		&gnm.garlicmem, sizeof(GnmBuffer) * 3, GNM_ALIGNMENT_BUFFER_BYTES
	);
	dev->vertbuffers[0] = gnmCreateVertexBuffer(
		(uint8_t*)dev->vertmem + posoff, GNM_FMT_R32G32_FLOAT,
		sizeof(struct nk_gnm_vertex), maxvertcount
	);
	dev->vertbuffers[1] = gnmCreateVertexBuffer(
		(uint8_t*)dev->vertmem + uvoff, GNM_FMT_R32G32_FLOAT,
		sizeof(struct nk_gnm_vertex), maxvertcount
	);
	dev->vertbuffers[2] = gnmCreateVertexBuffer(
		(uint8_t*)dev->vertmem + coloff, GNM_FMT_R32G32B32A32_FLOAT,
		sizeof(struct nk_gnm_vertex), maxvertcount
	);

	const uint32_t remapsemantictable[3] = {0, 1, 2};

	// prepare fetch shader
	GnmFetchShaderBuildState fsbs = {0};
	GnmError err = gnmGenerateFetchShaderBuildStateVs(
		&fsbs, &dev->vsh->registers, dev->vsh->numinputsemantics, NULL, 0, 0, 0
	);
	NK_ASSERT(err == GNM_ERROR_OK);

	fsbs.numinputsemantics = dev->vsh->numinputsemantics;
	fsbs.inputsemantics = gnmVsShaderInputSemanticTable(vsshader);
	fsbs.numinputusageslots = dev->vsh->common.numinputusageslots;
	fsbs.inputusageslots = gnmVsShaderInputUsageSlotTable(vsshader);
	fsbs.numelemsinremaptable = NK_LEN(remapsemantictable);
	fsbs.semanticsremaptable = remapsemantictable;

	dev->fetchshmem = nk_gnm_reservemem(
		&gnm.garlicmem, fsbs.fetchshaderbuffersize,
		GNM_ALIGNMENT_FETCHSHADER_BYTES
	);
	err = gnmGenerateFetchShader(
		dev->fetchshmem, fsbs.fetchshaderbuffersize, &fsbs
	);
	NK_ASSERT(err == GNM_ERROR_OK);

	gnmVsStageRegSetFetchShaderModifier(
		&dev->vsh->registers, fsbs.shadermodifier
	);

	// setup element (index) buffer
	dev->indexmem = nk_gnm_reservemem(
		&gnm.garlicmem, max_element_memory, GNM_ALIGNMENT_BUFFER_BYTES
	);

	// setup constant buffer
	dev->cbufmem = nk_gnm_reservemem(&gnm.onionmem, sizeof(float[4][4]), 4);
}

NK_INTERN void nk_gnm_device_upload_atlas(
	const void* image, int width, int height
) {
	struct nk_gnm_device* dev = &gnm.dev;

	// setup texture
	const GnmDataFormat texfmt = GNM_FMT_R8G8B8A8_SRGB;
	const uint32_t elemwidth = gnmDfGetTotalBytesPerElement(texfmt);
	const uint32_t texsize = width * height * elemwidth;
	void* texmem =
		nk_gnm_reservemem(&gnm.garlicmem, texsize, GNM_ALIGNMENT_SHADER_BYTES);

	memcpy(texmem, image, texsize);

	const GnmTextureCreateInfo ci = {
		.texturetype = GNM_TEXTURE_2D,
		.width = width,
		.height = height,
		.depth = 1,
		.pitch = width,
		.nummiplevels = 1,
		.numslices = 1,
		.format = texfmt,
		.tilemodehint = GNM_TM_DISPLAY_LINEAR_GENERAL,
		.mingpumode = GNM_GPU_BASE,
		.numfragments = 1,
	};
	GnmError gerr = gnmCreateTexture(&dev->font_tex, &ci);
	NK_ASSERT(gerr == GNM_ERROR_OK);
	gnmTexSetBaseAddress(&dev->font_tex, texmem);

	// setup sampler
	dev->font_samp = (GnmSampler){
		.xyminfilter = GNM_FILTER_BILINEAR,
		.xymagfilter = GNM_FILTER_BILINEAR,
		.mipfilter = GNM_MIPFILTER_LINEAR,
		.zfilter = GNM_ZFILTER_POINT,
		.minlod = 0,
		.maxlod = 0xfff,
	};
}

NK_INTERN void nk_gnm_device_destroy(void) {
	struct nk_gnm_device* dev = &gnm.dev;
	nk_gnm_freememory(&gnm.garlicmem);
	nk_gnm_freememory(&gnm.onionmem);
	nk_buffer_free(&dev->cmds);
}

NK_INTERN
void nk_gnm_setup_viewport(
	GnmCommandBuffer* cmdbuf, uint32_t left, uint32_t top, uint32_t right,
	uint32_t bottom, float zscale, float zoffset
) {
	const int32_t width = right - left;
	const int32_t height = bottom - top;

	const float scale[3] = {
		width * 0.5f,
		-height * 0.5f,
		zscale,
	};
	const float offset[3] = {
		left + width * 0.5f,
		top + height * 0.5f,
		zoffset,
	};
	gnmDrawCmdSetViewport(cmdbuf, 0, 0.0f, 1.0f, scale, offset);

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
		GNM_MIN(508, (int)floor(offset[0] / 16.0f + 0.5f)) & ~0x3;
	const int hwoffsety =
		GNM_MIN(508, (int)floor(offset[1] / 16.0f + 0.5f)) & ~0x3;
	gnmDrawCmdSetHwScreenOffset(cmdbuf, hwoffsetx, hwoffsety);

	const float hwmin = -(float)((1 << 23) - 0) / (float)(1 << 8);
	const float hwmax = (float)((1 << 23) - 1) / (float)(1 << 8);
	const float gbmaxx = GNM_MIN(
		hwmax - fabsf(scale[0]) - offset[0] + hwoffsetx * 16,
		-fabsf(scale[0]) + offset[0] - hwoffsetx * 16 - hwmin
	);
	const float gbmaxy = GNM_MIN(
		hwmax - fabsf(scale[1]) - offset[1] + hwoffsety * 16,
		-fabsf(scale[1]) + offset[1] - hwoffsety * 16 - hwmin
	);
	const float gbhorzclipadjust = gbmaxx / fabsf(scale[0]);
	const float gbvertclipadjust = gbmaxy / fabsf(scale[1]);
	gnmDrawCmdSetGuardBands(
		cmdbuf, gbhorzclipadjust, gbvertclipadjust, 1.0f, 1.0f
	);
}

NK_API void nk_gnm_render(GnmCommandBuffer* cmdbuf, enum nk_anti_aliasing AA) {
	struct nk_gnm_device* dev = &gnm.dev;
	float ortho[4][4] = {
		{2.0f, 0.0f, 0.0f, 0.0f},
		{0.0f, -2.0f, 0.0f, 0.0f},
		{0.0f, 0.0f, -1.0f, 0.0f},
		{-1.0f, 1.0f, 0.0f, 1.0f},
	};
	nk_gnm_setup_viewport(
		cmdbuf, 0, 0, gnm.screen_width, gnm.screen_height, 0.5f, 0.5f
	);
	ortho[0][0] /= (float)gnm.screen_width;
	ortho[1][1] /= (float)gnm.screen_height;

	/* setup global state */
	const GnmPrimitiveSetup primsetup = {
		.cullmode = GNM_CULL_NONE,
		.frontface = GNM_FACE_CCW,
		.frontmode = GNM_FILL_SOLID,
		.backmode = GNM_FILL_SOLID,
		.frontoffsetmode = false,
		.backoffsetmode = false,
		.vertexwindowoffsetenable = false,
		.provokemode = GNM_PROVOKINGVTX_FIRST,
		.perspectivecorrectionenable = true,
	};
	const GnmBlendControl blendctrl = {
		.blendenabled = true,
		.colorfunc = GNM_COMB_DST_PLUS_SRC,
		.colorsrcmult = GNM_BLEND_SRC_ALPHA,
		.colordstmult = GNM_BLEND_ONE_MINUS_SRC_ALPHA,
		.separatealphaenable = false,
	};
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
	gnmDrawCmdSetPrimitiveSetup(cmdbuf, &primsetup);
	gnmDrawCmdSetBlendControl(cmdbuf, 0, &blendctrl);
	gnmDrawCmdSetDbRenderControl(cmdbuf, &dbrenderctrl);
	gnmDrawCmdSetDepthStencilControl(cmdbuf, &depthstencilctrl);

	/* setup shaders */
	gnmDrawCmdSetVsShader(cmdbuf, &dev->vsh->registers, 0);
	gnmDrawCmdSetPsShader(cmdbuf, &dev->psh->registers);

	// VS: set fetch shader and vertex buffers
	gnmDrawCmdSetPointerUserData(
		cmdbuf, GNM_SHADERSTAGE_VS, 0, dev->fetchshmem
	);
	gnmDrawCmdSetPointerUserData(
		cmdbuf, GNM_SHADERSTAGE_VS, 2, dev->vertbuffers
	);

	uint32_t psusagetable[32] = {0};
	gnmCalcPsShaderUsageTable(
		psusagetable, gnmVsShaderExportSemanticTable(dev->vsh),
		dev->vsh->numexportsemantics, gnmPsShaderInputSemanticTable(dev->psh),
		dev->psh->numinputsemantics
	);
	gnmDrawCmdSetPsShaderUsage(
		cmdbuf, psusagetable, dev->psh->numinputsemantics
	);

	memcpy(dev->cbufmem, &ortho[0][0], sizeof(ortho));

	{
		/* convert from command queue into draw list and draw to screen */
		const struct nk_draw_command* cmd;
		const nk_draw_index* offset = dev->indexmem;
		struct nk_buffer vbuf, ebuf;

		/* load vertices/elements directly into vertex/element buffer */
		{
			/* fill convert configuration */
			struct nk_convert_config config;
			static const struct nk_draw_vertex_layout_element vertex_layout[] =
				{{NK_VERTEX_POSITION, NK_FORMAT_FLOAT,
				  NK_OFFSETOF(struct nk_gnm_vertex, position)},
				 {NK_VERTEX_TEXCOORD, NK_FORMAT_FLOAT,
				  NK_OFFSETOF(struct nk_gnm_vertex, uv)},
				 {NK_VERTEX_COLOR, NK_FORMAT_R32G32B32A32_FLOAT,
				  NK_OFFSETOF(struct nk_gnm_vertex, col)},
				 {NK_VERTEX_LAYOUT_END}};
			NK_MEMSET(&config, 0, sizeof(config));
			config.vertex_layout = vertex_layout;
			config.vertex_size = sizeof(struct nk_gnm_vertex);
			config.vertex_alignment = NK_ALIGNOF(struct nk_gnm_vertex);
			config.tex_null = dev->tex_null;
			config.circle_segment_count = 22;
			config.curve_segment_count = 22;
			config.arc_segment_count = 22;
			config.global_alpha = 1.0f;
			config.shape_AA = AA;
			config.line_AA = AA;

			/* setup buffers to load vertices and elements */
			nk_buffer_init_fixed(&vbuf, dev->vertmem, dev->max_vertex_memory);
			nk_buffer_init_fixed(&ebuf, dev->indexmem, dev->max_element_memory);
			nk_convert(&gnm.ctx, &dev->cmds, &vbuf, &ebuf, &config);
		}

		/* iterate over and execute each draw command */
		nk_draw_foreach(cmd, &gnm.ctx, &dev->cmds) {
			if (!cmd->elem_count)
				continue;

			// create and set vertex shader descriptor set
			struct nk_gnm_vsdescset* vsdescset =
				gnmCmdAllocInside(cmdbuf, sizeof(struct nk_gnm_vsdescset));
			NK_ASSERT(vsdescset);
			vsdescset->constbuf =
				gnmCreateConstBuffer(dev->cbufmem, sizeof(float[4][4]));
			gnmDrawCmdSetPointerUserData(cmdbuf, GNM_SHADERSTAGE_VS, 6, vsdescset);

			// create and set pixel shader descriptor set
			struct nk_gnm_psdescset* psdescset =
				gnmCmdAllocInside(cmdbuf, sizeof(struct nk_gnm_psdescset));
			NK_ASSERT(psdescset);
			psdescset->texture = *(GnmTexture*)cmd->texture.ptr;
			psdescset->sampler = dev->font_samp;
			gnmDrawCmdSetPointerUserData(cmdbuf, GNM_SHADERSTAGE_PS, 0, psdescset);

			gnmDrawCmdSetScreenScissor(
				cmdbuf, cmd->clip_rect.x, cmd->clip_rect.y, cmd->clip_rect.w,
				cmd->clip_rect.h
			);

			gnmDrawCmdSetPrimitiveType(cmdbuf, GNM_PT_TRILIST);
			gnmDrawCmdSetIndexSize(cmdbuf, GNM_INDEX_16);
			gnmDrawCmdDrawIndex(cmdbuf, cmd->elem_count, offset);

			offset += cmd->elem_count;
		}
		nk_clear(&gnm.ctx);
		nk_buffer_clear(&dev->cmds);
	}
}

static void nk_gnm_clipboard_paste(nk_handle usr, struct nk_text_edit* edit) {
	// TODO
}

static void nk_gnm_clipboard_copy(nk_handle usr, const char* text, int len) {
	// TODO
}

NK_API struct nk_context* nk_gnm_init(
	uint32_t screen_width, uint32_t screen_height, uint32_t max_vertex_memory,
	uint32_t max_element_memory
) {
	gnm.screen_width = screen_width;
	gnm.screen_height = screen_height;
	nk_init_default(&gnm.ctx, 0);
	gnm.ctx.clip.copy = nk_gnm_clipboard_copy;
	gnm.ctx.clip.paste = nk_gnm_clipboard_paste;
	gnm.ctx.clip.userdata = nk_handle_ptr(0);
	nk_gnm_device_create(max_vertex_memory, max_element_memory);
	return &gnm.ctx;
}

NK_API void nk_gnm_font_stash_begin(struct nk_font_atlas** atlas) {
	nk_font_atlas_init_default(&gnm.atlas);
	nk_font_atlas_begin(&gnm.atlas);
	*atlas = &gnm.atlas;
}

NK_API void nk_gnm_font_stash_end(void) {
	const void* image;
	int w, h;
	image = nk_font_atlas_bake(&gnm.atlas, &w, &h, NK_FONT_ATLAS_RGBA32);
	nk_gnm_device_upload_atlas(image, w, h);
	nk_font_atlas_end(
		&gnm.atlas, nk_handle_ptr(&gnm.dev.font_tex), &gnm.dev.tex_null
	);
	if (gnm.atlas.default_font)
		nk_style_set_font(&gnm.ctx, &gnm.atlas.default_font->handle);
}

NK_API void nk_gnm_shutdown(void) {
	nk_font_atlas_clear(&gnm.atlas);
	nk_free(&gnm.ctx);
	nk_gnm_device_destroy();
	NK_MEMSET(&gnm, 0, sizeof(gnm));
}

#endif
