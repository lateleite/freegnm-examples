#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <cglm/struct.h>
#include <orbis/GnmDriver.h>

#include <gnm/drawcommandbuffer.h>
#include <gnm/gpuaddr/gpuaddr.h>
#include <gnm/platform.h>

#include "u/utility.h"

#include "displayctx.h"
#include "misc.h"

#include "pass_model.h"
#include "pass_ui.h"

static inline struct timespec difftimespec(
	struct timespec start, struct timespec end
) {
	struct timespec res = {0};

	if ((end.tv_nsec - start.tv_nsec) < 0) {
		res.tv_sec = end.tv_sec - start.tv_sec - 1;
		res.tv_nsec = 1000000000 + end.tv_nsec - start.tv_nsec;
	} else {
		res.tv_sec = end.tv_sec - start.tv_sec;
		res.tv_nsec = end.tv_nsec - start.tv_nsec;
	}

	return res;
}

static void onmessagegnm(
	GnmMessageSeverity severity, const char* message, void* userdata
) {
	(void)severity;	 // unused
	(void)userdata;	 // unused
	fprintf(stdout, "[gnm] %s\n", message);
}

static inline int submitcmd(GnmCommandBuffer* cmd) {
	void* dcbaddr = cmd->beginptr;
	uint32_t dcbsize = (cmd->cmdptr - cmd->beginptr) * sizeof(uint32_t);
	void* ccbaddr = NULL;
	uint32_t ccbsize = 0;
	return sceGnmSubmitCommandBuffers(
		1, &dcbaddr, &dcbsize, &ccbaddr, &ccbsize
	);
}

int main(void) {
	MemoryAllocator garlicmem = memalloc_init(
		64 * 1024 * 1024,  // 64mB
		ORBIS_KERNEL_PROT_CPU_READ | ORBIS_KERNEL_PROT_CPU_RW |
			ORBIS_KERNEL_PROT_GPU_READ | ORBIS_KERNEL_PROT_GPU_WRITE,
		ORBIS_KERNEL_WC_GARLIC
	);

	gnmSetMessageHandler(&onmessagegnm, NULL);

	if (!initclearutility(&garlicmem)) {
		fatal("Failed to init clear utility");
	}

	// init video
	DisplayContext display = {0};
	if (!displayctx_init(&display)) {
		fatal("Failed to init video");
	}

	printf("Display resolution: %ux%u\n", display.screenw, display.screenh);

	pass_model_init(&garlicmem, display.screenw, display.screenh);
	pass_ui_init(display.screenw, display.screenh, &garlicmem);

	// create 2 color render targets for double frame buffering
	GnmRenderTarget fbtargets[2] = {0};
	for (unsigned i = 0; i < uasize(fbtargets); i += 1) {
		if (!initcolortarget(
				&fbtargets[i], &garlicmem, display.screenw, display.screenh,
				GNM_FMT_R8G8B8A8_SRGB, gnmGpuMode()
			)) {
			fatalf("Failed to init color render target %u", i);
		}
	}

	// create depth render target
	GnmDepthRenderTarget depthtarget = {0};
	if (!initdepthtarget(
			&depthtarget, &garlicmem, display.screenw, display.screenh, 0, 1, 1,
			GNM_Z_32_FLOAT, GNM_STENCIL_INVALID, gnmGpuMode()
		)) {
		fatal("Failed to init depth render target");
	}

	if (!displayctx_setrts(&display, fbtargets, uasize(fbtargets))) {
		fatal("Failed to set video render target");
	}

	const char* modelpath = "/app0/assets/misc/venus.gltf";
	gltfModel model = {0};
	if (!LoadGltfModel(&model, modelpath, &garlicmem, true, false)) {
		fatalf("Failed to load GLTF model %s", modelpath);
	}

	// create command buffer
	const size_t cmdmemsize = GNM_INDIRECT_BUFFER_MAX_BYTESIZE;
	void* cmdmem =
		memalloc_alloc(&garlicmem, cmdmemsize, GNM_ALIGNMENT_BUFFER_BYTES);
	if (!cmdmem) {
		fatal("Failed to allocate cmdbuffer");
	}

	GnmCommandBuffer cmdbuf = gnmCmdInit(cmdmem, cmdmemsize, NULL, NULL);

	// setup sync point
	uint64_t* label =
		memalloc_alloc(&garlicmem, sizeof(uint64_t), sizeof(uint64_t));
	if (!label) {
		fatal("Failed to allocate sync label");
	}

	// get some model stats
	const size_t numprimitives = uvlen(&model.PrimMeshes);
	size_t totalverts = 0;
	size_t totalindices = 0;
	for (size_t i = 0; i < numprimitives; i += 1) {
		const R_Mesh* m = uvdatac(&model.PrimMeshes, i);
		totalverts += m->numvertices;
		totalindices += m->numindices;
	}

	unsigned curfb = 0;
	float frametime = 0.0;
	struct timespec timestart = {0};
	struct timespec timeend = {0};

	while (1) {
		gnmCmdReset(&cmdbuf);
		*label = 9;

		gnmDrawCmdWaitUntilSafeForRendering(
			&cmdbuf, display.videohandle, curfb
		);

		pass_model_update();

		clock_gettime(CLOCK_MONOTONIC, &timestart);

		// clear color and depth right away, since they set some controls
		const float clearcolor[4] = {0.9, 0.9, 0.9, 1.0};
		clearcolortarget(&cmdbuf, &fbtargets[curfb], clearcolor);
		cleardepthtarget(&cmdbuf, &depthtarget, 1.0);

		// wait for clears to write
		// TODO: is any of this stuff doing graphics-graphics synchronization?
		// the color render target shows tear when playing this sample.
		// is that even the issue?
		gnmDrawCmdEventWriteEop(
			&cmdbuf, GNM_CACHE_FLUSH_AND_INV_TS_EVENT, (uint64_t)label,
			GNM_DATA_SEL_SEND_DATA64, 1
		);
		gnmDrawCmdWaitMem(
			&cmdbuf, GNM_WAIT_REG_MEM_FUNC_EQUAL, (uint64_t)label, 1, 0xffffffff
		);
		gnmDrawCmdWaitGraphicsWrite(
			&cmdbuf, GNM_ACQUIRE_TARGET_CB0 | GNM_ACQUIRE_TARGET_DB
		);

		// model pass
		pass_model_render(&cmdbuf, &model, &fbtargets[curfb], &depthtarget);

		// ui pass
		const vec3s loc = pass_model_getloc();
		const vec3s rot = pass_model_getrot();
		const ui_color blackcol = {0, 0, 0, 1.0};
		float cury = 36.0;
		pass_ui_begin(&cmdbuf);
		pass_ui_drawtextf(20.0, cury, blackcol, "frametime: %f", frametime);
		cury += 20.0;
		pass_ui_drawtextf(
			20.0, cury, blackcol, "location: %f %f %f", loc.x, loc.y, loc.z
		);
		cury += 20.0;
		pass_ui_drawtextf(
			20.0, cury, blackcol, "rotation: %f %f %f", rot.x, rot.y, rot.z
		);
		cury += 20.0;
		pass_ui_drawtextf(20.0, cury, blackcol, "modelpath: %s", model.path);
		cury += 20.0;
		pass_ui_drawtextf(
			20.0, cury, blackcol, "total vertices: %lu", totalverts
		);
		cury += 20.0;
		pass_ui_drawtextf(
			20.0, cury, blackcol, "total indices: %lu", totalindices
		);
		cury += 20.0;

		pass_ui_end(&cmdbuf, &fbtargets[curfb]);

		gnmDrawCmdEventWriteEop(
			&cmdbuf, GNM_CACHE_FLUSH_TS, (uint64_t)label,
			GNM_DATA_SEL_SEND_DATA64, 2
		);

		int res = submitcmd(&cmdbuf);
		if (res != 0) {
			printf("submitcmd failed with %i\n", res);
			break;
		}

		while (*label != 2) {
			continue;
		}

		clock_gettime(CLOCK_MONOTONIC, &timeend);
		struct timespec timediff = difftimespec(timestart, timeend);
		frametime = timediff.tv_sec +
					((float)timediff.tv_nsec / (1000.0 * 1000.0 * 1000.0)
					);	// nano to seconds

		if (!displayctx_flip(&display, curfb)) {
			puts("displayflip failed");
			break;
		}

		res = sceGnmSubmitDone();
		if (res != 0) {
			printf("sceGnmSubmitDone failed with %i\n", res);
			break;
		}

		curfb += 1;
		if (curfb >= uasize(fbtargets)) {
			curfb = 0;
		}
	}

	displayctx_destroy(&display);
	memalloc_destroy(&garlicmem);

	return 0;
}
