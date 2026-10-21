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
	// only one is needed since this program flips a frame only once
	GnmRenderTarget fbtarget = {0};
	if (!initcolortarget(
			&fbtarget, &garlicmem, display.screenw, display.screenh,
			GNM_FMT_R8G8B8A8_SRGB, gnmGpuMode()
		)) {
		fatal("Failed to init color render target");
	}

	if (!displayctx_setrts(&display, &fbtarget, 1)) {
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

	// wait for clears to write
	gnmDrawCmdWaitGraphicsWrite(
		&cmdbuf, GNM_ACQUIRE_TARGET_CB0 | GNM_ACQUIRE_TARGET_DB
	);

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

	// setup viewport
	setupviewport(&cmdbuf, 0, 0, display.screenw, display.screenh, 0.5f, 0.5f);

	GnmVsShader* vshader = NULL;
	GnmPsShader* pshader = NULL;
	if (!loadvshader(&vshader, &garlicmem, "/app0/assets/misc/tri.vert.sb")) {
		fatal("Failed to load vertex shader");
	}
	if (!loadpshader(&pshader, &garlicmem, "/app0/assets/misc/tri.frag.sb")) {
		fatal("Failed to load fragment shader");
	}

	gnmDrawCmdSetVsShader(&cmdbuf, &vshader->registers, 0);
	gnmDrawCmdSetPsShader(&cmdbuf, &pshader->registers);

	gnmDrawCmdSetPsInputUsage(
		&cmdbuf, gnmVsShaderExportSemanticTable(vshader),
		vshader->numexportsemantics, gnmPsShaderInputSemanticTable(pshader),
		pshader->numinputsemantics
	);

	// draw triangle
	gnmDrawCmdSetPrimitiveType(&cmdbuf, GNM_PT_TRILIST);
	gnmDrawCmdDrawIndexAuto(&cmdbuf, 3);

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
			printf("sceGnmSubmitCommandBuffers failed with 0x%x\n", res);
			break;
		}

		while (*label != 1) {
			continue;
		}

		if (!displayctx_flip(&display, 0)) {
			puts("displayflip failed");
			break;
		}

		res = sceGnmSubmitDone();
		if (res != 0) {
			printf("sceGnmSubmitDone failed with 0x%x\n", res);
			break;
		}
	}

	displayctx_destroy(&display);
	memalloc_destroy(&garlicmem);

	return 0;
}
