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
	mat4s c_mvp;
} ConstBuffer;
_Static_assert(sizeof(ConstBuffer) == 0x40, "");

typedef struct {
	GnmBuffer constbuf;
} VsDescSet;
_Static_assert(sizeof(VsDescSet) == 0x10, "");

typedef struct {
	GnmTexture texture;
	GnmSampler sampler;
} PsDescSet;
_Static_assert(sizeof(PsDescSet) == 0x30, "");

// vertices layout:
// 3 floats - position
// 3 floats - normals (untested)
// 2 floats - texture coordinates
//
// clang-format off
static const float s_vertices[] = {
	// back (man in charge)
	-0.5, -0.5, -0.5,  0.0,  0.0, -1.0,  0.33, 1.0,
	 0.5, -0.5, -0.5,  0.0,  0.0, -1.0,  0.0,  1.0,
	 0.5,  0.5, -0.5,  0.0,  0.0, -1.0,  0.0,  0.5,
	 0.5,  0.5, -0.5,  0.0,  0.0, -1.0,  0.0,  0.5,
	-0.5,  0.5, -0.5,  0.0,  0.0, -1.0,  0.33, 0.5,
	-0.5, -0.5, -0.5,  0.0,  0.0, -1.0,  0.33, 1.0,

	// front (dr pavel)
	-0.5, -0.5,  0.5,  0.0,  0.0,  1.0,  0.33, 0.5,
	 0.5, -0.5,  0.5,  0.0,  0.0,  1.0,  0.66, 0.5,
	 0.5,  0.5,  0.5,  0.0,  0.0,  1.0,  0.66, 0.0,
	 0.5,  0.5,  0.5,  0.0,  0.0,  1.0,  0.66, 0.0,
	-0.5,  0.5,  0.5,  0.0,  0.0,  1.0,  0.33, 0.0,
	-0.5, -0.5,  0.5,  0.0,  0.0,  1.0,  0.33, 0.5,

	// left (hired gun)
	-0.5,  0.5,  0.5, -1.0,  0.0,  0.0,  1.0,  0.0,
	-0.5,  0.5, -0.5, -1.0,  0.0,  0.0,  0.66, 0.0,
	-0.5, -0.5, -0.5, -1.0,  0.0,  0.0,  0.66, 0.5,
	-0.5, -0.5, -0.5, -1.0,  0.0,  0.0,  0.66, 0.5,
	-0.5, -0.5,  0.5, -1.0,  0.0,  0.0,  1.0,  0.5,
	-0.5,  0.5,  0.5, -1.0,  0.0,  0.0,  1.0,  0.0,

	// right (masketta man)
	 0.5,  0.5,  0.5,  1.0,  0.0,  0.0,  0.33, 0.5,
	 0.5,  0.5, -0.5,  1.0,  0.0,  0.0,  0.66, 0.5,
	 0.5, -0.5, -0.5,  1.0,  0.0,  0.0,  0.66, 1.0,
	 0.5, -0.5, -0.5,  1.0,  0.0,  0.0,  0.66, 1.0,
	 0.5, -0.5,  0.5,  1.0,  0.0,  0.0,  0.33, 1.0,
	 0.5,  0.5,  0.5,  1.0,  0.0,  0.0,  0.33, 0.5,

	 // bottom (wreckage brother)
	-0.5, -0.5, -0.5,  0.0, -1.0,  0.0,  0.66, 0.5,
	 0.5, -0.5, -0.5,  0.0, -1.0,  0.0,  1.0,  0.5,
	 0.5, -0.5,  0.5,  0.0, -1.0,  0.0,  1.0,  1.0,
	 0.5, -0.5,  0.5,  0.0, -1.0,  0.0,  1.0,  1.0,
	-0.5, -0.5,  0.5,  0.0, -1.0,  0.0,  0.66, 1.0,
	-0.5, -0.5, -0.5,  0.0, -1.0,  0.0,  0.66, 0.5,

	// top (aircraft)
	-0.5,  0.5, -0.5,  0.0,  1.0,  0.0,  0.33, 0.5,
	 0.5,  0.5, -0.5,  0.0,  1.0,  0.0,  0.0,  0.5,
	 0.5,  0.5,  0.5,  0.0,  1.0,  0.0,  0.0,  0.0,
	 0.5,  0.5,  0.5,  0.0,  1.0,  0.0,  0.0,  0.0,
	-0.5,  0.5,  0.5,  0.0,  1.0,  0.0,  0.33, 0.0,
	-0.5,  0.5, -0.5,  0.0,  1.0,  0.0,  0.33, 0.5,
};
// clang-format on

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

	GnmVsShader* vshader = NULL;
	GnmPsShader* pshader = NULL;
	if (!loadvshader(&vshader, &garlicmem, "/app0/assets/misc/cube.vert.sb")) {
		fatal("Failed to load vertex shader");
	}
	if (!loadpshader(&pshader, &garlicmem, "/app0/assets/misc/cube.frag.sb")) {
		fatal("Failed to load fragment shader");
	}

	// setup texture
	GnmTexture texture = {0};
	if (!loadgnftexture(
			&garlicmem, &texture, "/app0/assets/images/friends.gnf"
		)) {
		fatal("Failed to load texture");
	}

	// setup sampler
	const GnmSampler sampler = {
		.xyminfilter = GNM_FILTER_BILINEAR,
		.xymagfilter = GNM_FILTER_BILINEAR,
		.mipfilter = GNM_MIPFILTER_LINEAR,
		.zfilter = GNM_ZFILTER_POINT,
		.minlod = 0,
		.maxlod = 0xfff,
	};

	// setup const buffer
	ConstBuffer* cbufmem = memalloc_alloc(
		&garlicmem, sizeof(ConstBuffer), GNM_ALIGNMENT_BUFFER_BYTES
	);
	if (!cbufmem) {
		fatal("Failed to allocate const buffer");
	}

	// setup descriptor sets
	VsDescSet* vsdescset = memalloc_alloc(
		&garlicmem, sizeof(VsDescSet), GNM_ALIGNMENT_BUFFER_BYTES
	);
	if (!vsdescset) {
		fatal("Failed to allocate VS descriptor set");
	}
	*vsdescset = (VsDescSet){
		.constbuf = gnmCreateConstBuffer(cbufmem, sizeof(ConstBuffer)),
	};

	PsDescSet* psdescset = memalloc_alloc(
		&garlicmem, sizeof(PsDescSet), GNM_ALIGNMENT_BUFFER_BYTES
	);
	if (!psdescset) {
		puts("Failed to allocate PS descriptor set");
		return 1;
	}
	*psdescset = (PsDescSet){
		.texture = texture,
		.sampler = sampler,
	};

	// setup vertex buffer
	uint8_t* vertmem = memalloc_alloc(
		&garlicmem, sizeof(s_vertices), GNM_ALIGNMENT_BUFFER_BYTES
	);
	if (!vertmem) {
		fatal("allocgarlicmem for vertex data failed");
	}
	memcpy(vertmem, s_vertices, sizeof(s_vertices));

	GnmBuffer* vertbuffers = memalloc_alloc(
		&garlicmem, sizeof(GnmBuffer) * 2, GNM_ALIGNMENT_BUFFER_BYTES
	);
	if (!vertbuffers) {
		fatal("allocgarlicmem for vertex buffers failed");
	}

	// these buffer descriptors must be visible to the GPU.
	// buffer 0 is position
	// buffer 1 is texture UV
	vertbuffers[0] = gnmCreateVertexBuffer(
		vertmem, GNM_FMT_R32G32B32_FLOAT, 8 * sizeof(float), 36
	);
	vertbuffers[1] = gnmCreateVertexBuffer(
		vertmem + 24, GNM_FMT_R32G32_FLOAT, 8 * sizeof(float), 36
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

	// setup sync point
	uint64_t* label =
		memalloc_alloc(&garlicmem, sizeof(uint64_t), sizeof(uint64_t));
	if (!label) {
		fatal("Failed to allocate sync label");
	}

	const vec3s translation = {
		.x = 0.0,
		.y = 0.0,
		.z = -1.75,
	};

	const mat4s tranm = glms_translate(GLMS_MAT4_IDENTITY, translation);

	const vec3s x_up = {{1.0, 0.0, 0.0}};
	const vec3s y_up = {{0.0, 1.0, 0.0}};
	const vec3s z_up = {{0.0, 0.0, 1.0}};
	vec3s viewrot = {
		.x = 20.0,
		.y = 0.0,
		.z = 0.0,
	};

	mat4s matproj = glms_perspective(
		glm_rad(90.0), (float)display.screenw / (float)display.screenh, 0.1,
		100.0
	);

	unsigned curfb = 0;

	while (1) {
		gnmCmdReset(&cmdbuf);
		*label = 9;

		// clear both render targets right away, since clearing sets some
		// controls
		const float clearcolor[4] = {1.0, 1.0, 1.0, 1.0};
		clearcolortarget(&cmdbuf, &fbtargets[curfb], clearcolor);
		cleardepthtarget(&cmdbuf, &depthtarget, 1.0);

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

		gnmDrawCmdSetRenderTarget(&cmdbuf, 0, &fbtargets[curfb]);
		gnmDrawCmdSetRenderTargetMask(&cmdbuf, 0xf);
		gnmDrawCmdSetDepthRenderTarget(&cmdbuf, &depthtarget);

		// setup controls
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
			.zwrite = true,
			.zfunc = GNM_DEPTH_COMPARE_LESSEQUAL,
			.stencilfunc = GNM_DEPTH_COMPARE_NEVER,
			.stencilbackfunc = GNM_DEPTH_COMPARE_NEVER,
			.separatestencilenable = false,
			.depthenable = true,
			.stencilenable = false,
			.depthboundsenable = false,
		};
		gnmDrawCmdSetDbRenderControl(&cmdbuf, &dbrenderctrl);
		gnmDrawCmdSetDepthStencilControl(&cmdbuf, &depthstencilctrl);

		// setup viewport
		setupviewport(
			&cmdbuf, 0, 0, display.screenw, display.screenh, 0.5f, 0.5f
		);

		gnmDrawCmdSetVsShader(&cmdbuf, &vshader->registers, 0);
		gnmDrawCmdSetPsShader(&cmdbuf, &pshader->registers);

		// set fetch shader, vertex buffers and desciprtor set in vertex shader
		gnmDrawCmdSetPointerUserData(&cmdbuf, GNM_STAGE_VS, 0, fetchshmem);
		gnmDrawCmdSetPointerUserData(&cmdbuf, GNM_STAGE_VS, 2, vertbuffers);
		gnmDrawCmdSetPointerUserData(&cmdbuf, GNM_STAGE_VS, 6, vsdescset);

		// set desciprtor set in pixel shader
		gnmDrawCmdSetPointerUserData(&cmdbuf, GNM_STAGE_PS, 0, psdescset);

		gnmDrawCmdSetPsInputUsage(
			&cmdbuf, gnmVsShaderExportSemanticTable(vshader),
			vshader->numexportsemantics, gnmPsShaderInputSemanticTable(pshader),
			pshader->numinputsemantics
		);

		// draw triangle
		gnmDrawCmdSetPrimitiveType(&cmdbuf, GNM_PT_TRILIST);

		gnmDrawCmdDrawIndexAuto(&cmdbuf, 36);

		// write label when all commands are done
		gnmDrawCmdEventWriteEop(
			&cmdbuf, GNM_CACHE_FLUSH_TS, (uint64_t)label,
			GNM_DATA_SEL_SEND_DATA64, 1
		);

		// rotate cube
		viewrot.x += 0.25;
		if (viewrot.x >= 360.0) {
			viewrot.x = 0.0;
		}
		viewrot.y += 1.0;
		if (viewrot.y >= 360.0) {
			viewrot.y = 0.0;
		}

		// setup camera before render work
		mat4s rotm = GLMS_MAT4_IDENTITY;
		rotm = glms_rotate(rotm, glm_rad(viewrot.x), x_up);
		rotm = glms_rotate(rotm, glm_rad(viewrot.y), y_up);
		rotm = glms_rotate(rotm, glm_rad(viewrot.z), z_up);
		// third person camera.
		const mat4s matview = glms_mat4_mul(tranm, rotm);

		cbufmem->c_mvp = glms_mat4_mul(matproj, matview);

		// submit command buffer
		void* dcbaddr = cmdbuf.beginptr;
		uint32_t dcbsize = (cmdbuf.cmdptr - cmdbuf.beginptr) * sizeof(uint32_t);
		int res = sceGnmSubmitCommandBuffers(1, &dcbaddr, &dcbsize, NULL, 0);
		if (res != 0) {
			printf("sceGnmSubmitCommandBuffers failed with %i\n", res);
			break;
		}

		// wait for label to be written by GPU
		while (*label != 1) {
			continue;
		}

		// flip current framebuffer to display
		if (!displayctx_flip(&display, curfb)) {
			puts("displayflip failed");
			break;
		}

		// tell system that this frame is done
		res = sceGnmSubmitDone();
		if (res != 0) {
			printf("sceGnmSubmitDone failed with %i\n", res);
			break;
		}

		// go to next available framebuffer
		curfb += 1;
		if (curfb >= uasize(fbtargets)) {
			curfb = 0;
		}
	}

	displayctx_destroy(&display);
	memalloc_destroy(&garlicmem);

	return 0;
}
