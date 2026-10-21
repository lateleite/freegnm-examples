#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <cglm/struct.h>
#include <orbis/GnmDriver.h>

#include <gnm/drawcommandbuffer.h>
#include <gnm/gpuaddr/gpuaddr.h>
#include <gnm/platform.h>

#include "u/utility.h"

#include "displayctx.h"
#include "misc.h"

typedef struct {
	vec4s c_color;
	vec4s c_position;
} ConstBuffer;
_Static_assert(sizeof(ConstBuffer) == 0x20, "");

typedef struct {
	GnmBuffer constbuf;
} VsDescSet;
_Static_assert(sizeof(VsDescSet) == 0x10, "");

static const float s_vertices[] = {
	-0.25, 0.0,	   // top-left
	0.0,   0.0,	   // top-right
	0.0,   -0.25,  // bottom-right
	-0.25, -0.25,  // bottom-left

	0.0,   0.25,  // top-left
	0.25,  0.25,  // top-right
	0.25,  0.0,	  // bottom-right
	0.0,   0.0,	  // bottom-left
};

static void prepsquare(GnmCommandBuffer* cmd, vec2s pos, vec4s color) {
	// setup const buffer
	ConstBuffer* cbufmem =
		gnmCmdAllocInside(cmd, sizeof(ConstBuffer), GNM_ALIGNMENT_BUFFER_BYTES);
	if (!cbufmem) {
		fatal("Failed to allocate const buffer");
	}
	*cbufmem = (ConstBuffer){
		.c_color = color,
		.c_position = (vec4s){{pos.x, pos.y, 0.0, 1.0}},
	};

	// setup descriptor sets
	VsDescSet* vsdescset =
		gnmCmdAllocInside(cmd, sizeof(VsDescSet), GNM_ALIGNMENT_BUFFER_BYTES);
	if (!vsdescset) {
		fatal("Failed to allocate VS descriptor set");
	}
	*vsdescset = (VsDescSet){
		.constbuf = gnmCreateConstBuffer(cbufmem, sizeof(ConstBuffer)),
	};

	gnmDrawCmdSetPointerUserData(cmd, GNM_STAGE_VS, 6, vsdescset);
}

int main(void) {
	MemoryAllocator garlicmem = memalloc_init(
		64 * 1024 * 1024,  // 64mB
		ORBIS_KERNEL_PROT_CPU_READ | ORBIS_KERNEL_PROT_CPU_RW |
			ORBIS_KERNEL_PROT_GPU_READ | ORBIS_KERNEL_PROT_GPU_WRITE,
		ORBIS_KERNEL_WC_GARLIC
	);

	if (!initclearutility(&garlicmem)) {
		fatal("Failed to init clear utility");
	}

	// init video
	DisplayContext display = {0};
	if (!displayctx_init(&display)) {
		fatal("Failed to init video");
	}

	printf("Display resolution: %ux%u\n", display.screenw, display.screenh);

	// create color render target
	GnmRenderTarget fbtarget = {0};
	if (!initcolortarget(
			&fbtarget, &garlicmem, display.screenw, display.screenh,
			GNM_FMT_R8G8B8A8_SRGB, gnmGpuMode()
		)) {
		fatal("Failed to init color render target");
	}

	if (!displayctx_setrt(&display, &fbtarget)) {
		fatal("Failed to init video");
	}

	// create command buffer
	const size_t cmdmemsize = GNM_INDIRECT_BUFFER_MAX_BYTESIZE;
	void* cmdmem =
		memalloc_alloc(&garlicmem, cmdmemsize, GNM_ALIGNMENT_BUFFER_BYTES);
	if (!cmdmem) {
		fatal("Failed to allocate cmdbuffer");
	}

	GnmCommandBuffer cmdbuf = gnmCmdInit(cmdmem, cmdmemsize, NULL, NULL);

	// clear both render targets right away, since clearing sets some controls
	const float clearcolor[4] = {1.0, 1.0, 1.0, 1.0};
	clearcolortarget(&cmdbuf, &fbtarget, clearcolor);

	const GnmPrimitiveSetup primsetup = {
		.cullmode = GNM_CULL_NONE,
		.frontface = GNM_FACE_CCW,
		.frontmode = GNM_FILL_SOLID,
		.backmode = GNM_FILL_SOLID,
		.frontoffsetmode = false,
		.backoffsetmode = false,
		.vertexwindowoffsetenable = false,
		.provokemode = GNM_PROVOKINGVTX_FIRST,
		.perspectivecorrectiondisable = false,
	};
	gnmDrawCmdSetPrimitiveSetup(&cmdbuf, &primsetup);

	gnmDrawCmdSetRenderTarget(&cmdbuf, 0, &fbtarget);
	gnmDrawCmdSetRenderTargetMask(&cmdbuf, 0xf);

	// setup controls
	const GnmDbRenderControl dbrenderctrl = {0};
	gnmDrawCmdSetDbRenderControl(&cmdbuf, &dbrenderctrl);

	// setup viewport
	setupviewport(&cmdbuf, 0, 0, display.screenw, display.screenh, 0.5f, 0.5f);

	GnmVsShader* vshader = NULL;
	GnmPsShader* pshader = NULL;
	if (!loadvshader(&vshader, &garlicmem, "/app0/assets/misc/prim.vert.sb")) {
		fatal("Failed to load vertex shader");
	}
	if (!loadpshader(&pshader, &garlicmem, "/app0/assets/misc/prim.frag.sb")) {
		fatal("Failed to load fragment shader");
	}

	// setup vertex buffer
	uint8_t* vertmem = memalloc_alloc(
		&garlicmem, sizeof(s_vertices), GNM_ALIGNMENT_BUFFER_BYTES
	);
	if (!vertmem) {
		fatal("alloc for vertex data failed");
	}
	memcpy(vertmem, s_vertices, sizeof(s_vertices));

	GnmBuffer* vertbuffers = memalloc_alloc(
		&garlicmem, sizeof(GnmBuffer) * 1, GNM_ALIGNMENT_BUFFER_BYTES
	);
	if (!vertbuffers) {
		fatal("alloc for vertex buffers failed");
	}

	// these buffer descriptors must be visible to the GPU.
	// buffer 0 is position
	vertbuffers[0] = gnmCreateVertexBuffer(
		vertmem, GNM_FMT_R32G32_FLOAT, 2 * sizeof(float), 8
	);

	// prepare fetch shader
	const GnmFetchShaderCreateInfo fetchci = {
		.regs = &vshader->registers,
		.vtxinputs = gnmVsShaderInputSemanticTable(vshader),
		.numvtxinputs = vshader->numinputsemantics,
		.inputusages = gnmVsShaderInputUsageSlotTable(vshader),
		.numinputusages = vshader->common.numinputusageslots,
	};
	uint32_t fetchsize = 0;
	GnmError err = gnmFetchShaderCalcSize(&fetchsize, &fetchci);
	if (err != GNM_ERROR_OK) {
		fatalf("Failed to calc fetch shader size with %s", gnmStrError(err));
	}

	void* fetchshmem =
		memalloc_alloc(&garlicmem, fetchsize, GNM_ALIGNMENT_FETCHSHADER_BYTES);
	if (!fetchshmem) {
		fatal("allocgarlicmem for fetch shader failed");
	}

	GnmFetchShaderResults fetchres = {0};
	err = gnmCreateFetchShader(fetchshmem, fetchsize, &fetchci, &fetchres);
	if (err != GNM_ERROR_OK) {
		fatal("Failed to create fetch shader");
	}

	gnmVsRegsSetFetchShaderModifier(&vshader->registers, &fetchres);

	gnmDrawCmdSetVsShader(&cmdbuf, &vshader->registers, 0);
	gnmDrawCmdSetPsShader(&cmdbuf, &pshader->registers);

	// set fetch shader, vertex buffers and desciprtor set in vertex shader
	gnmDrawCmdSetPointerUserData(&cmdbuf, GNM_STAGE_VS, 0, fetchshmem);
	gnmDrawCmdSetPointerUserData(&cmdbuf, GNM_STAGE_VS, 2, vertbuffers);

	gnmDrawCmdSetPsInputUsage(
		&cmdbuf, gnmVsShaderExportSemanticTable(vshader),
		vshader->numexportsemantics, gnmPsShaderInputSemanticTable(pshader),
		pshader->numinputsemantics
	);

	static const uint16_t rectindices[6] = {0, 1, 2, 4, 5, 6};
	uint16_t* indices = gnmCmdAllocInside(
		&cmdbuf, sizeof(rectindices), GNM_ALIGNMENT_BUFFER_BYTES
	);
	if (!indices) {
		fatal("Failed to alloc indices");
	}
	memcpy(indices, rectindices, sizeof(rectindices));

	gnmDrawCmdSetPrimitiveType(&cmdbuf, GNM_PT_LINELIST);
	prepsquare(&cmdbuf, (vec2s){{-0.5, -0.5}}, (vec4s){{0.0, 1.0, 0.0, 1.0}});
	gnmDrawCmdDrawIndexAuto(&cmdbuf, 8);

	gnmDrawCmdSetPrimitiveType(&cmdbuf, GNM_PT_RECTLIST);
	prepsquare(&cmdbuf, (vec2s){{0.0, 0.0}}, (vec4s){{0.0, 0.0, 0.0, 1.0}});
	gnmDrawCmdSetIndexSize(&cmdbuf, GNM_INDEX_16, GNM_POLICY_LRU);
	gnmDrawCmdDrawIndex(&cmdbuf, uasize(rectindices), indices);

	gnmDrawCmdSetPrimitiveType(&cmdbuf, GNM_PT_QUADLIST);
	prepsquare(&cmdbuf, (vec2s){{0.5, 0.5}}, (vec4s){{1.0, 0.0, 0.0, 1.0}});
	gnmDrawCmdDrawIndexAuto(&cmdbuf, 8);

	gnmDrawCmdSetPrimitiveType(&cmdbuf, GNM_PT_QUADSTRIP);
	prepsquare(&cmdbuf, (vec2s){{-0.5, 0.5}}, (vec4s){{0.0, 0.0, 1.0, 1.0}});
	gnmDrawCmdDrawIndexAuto(&cmdbuf, 7);

	gnmDrawCmdSetPrimitiveType(&cmdbuf, GNM_PT_POLYGON);
	prepsquare(&cmdbuf, (vec2s){{0.5, -0.5}}, (vec4s){{0.5, 0.5, 0.0, 1.0}});
	gnmDrawCmdDrawIndexAuto(&cmdbuf, 8);

	// setup sync point
	uint64_t* label =
		memalloc_alloc(&garlicmem, sizeof(uint64_t), sizeof(uint64_t));
	if (!label) {
		fatal("Failed to allocate sync label");
	}

	gnmDrawCmdEventWriteEop(
		&cmdbuf, GNM_CACHE_FLUSH_TS, (uint64_t)label, GNM_DATA_SEL_SEND_DATA64,
		1
	);

	void* dcbaddr = cmdbuf.beginptr;
	uint32_t dcbsize = (cmdbuf.cmdptr - cmdbuf.beginptr) * sizeof(uint32_t);
	void* ccbaddr = NULL;
	uint32_t ccbsize = 0;

	while (1) {
		*label = 9;

		int res = sceGnmSubmitCommandBuffers(
			1, &dcbaddr, &dcbsize, &ccbaddr, &ccbsize
		);
		if (res != 0) {
			printf("sceGnmSubmitCommandBuffers failed with %i\n", res);
			break;
		}

		while (*label != 1) {
			continue;
		}

		if (!displayctx_flip(&display)) {
			puts("displayflip failed");
			break;
		}

		res = sceGnmSubmitDone();
		if (res != 0) {
			printf("sceGnmSubmitDone failed with %i\n", res);
			break;
		}
	}

	displayctx_destroy(&display);
	memalloc_destroy(&garlicmem);

	return 0;
}
