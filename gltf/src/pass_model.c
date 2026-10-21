#include "pass_model.h"

#include <cglm/struct.h>

#include "u/utility.h"

#include "gltfmodel.h"
#include "misc.h"

static inline float calcsquaredfalloffinv(const float falloff) {
	const float sqr = falloff * falloff;
	return sqr > 0.0 ? (1 / sqr) : 0;
}

typedef struct {
	mat4s c_mproj;
	mat4s c_mview;
	mat4s c_mmodel;
	vec4s c_viewpos;
	PointLight c_lights[2];
} ConstBuffer;
_Static_assert(sizeof(ConstBuffer) == 0x110, "");

typedef struct {
	GnmBuffer constbuf;
} VsDescSet;
_Static_assert(sizeof(VsDescSet) == 0x10, "");

typedef struct {
	GnmTexture texture;
	GnmSampler sampler;
	GnmBuffer constbuf;
} PsDescSet;
_Static_assert(sizeof(PsDescSet) == 0x40, "");

typedef struct {
	GnmVsShader* vsh;
	GnmPsShader* psh;

	ConstBuffer* cbufmem;
	GnmBuffer constbuf;
	GnmSampler sampler;

	void* fetchshmem;

	vec3s location;
	vec3s rotation;

	uint32_t viewwidth;
	uint32_t viewheight;
} PassModel;
static PassModel s_mpass = {0};

void pass_model_init(
	MemoryAllocator* glrmem, uint32_t screenwidth, uint32_t screenheight
) {
	s_mpass.viewwidth = screenwidth;
	s_mpass.viewheight = screenheight;

	// load shaders
	if (!loadvshader(&s_mpass.vsh, glrmem, "/app0/assets/misc/gltf.vert.sb")) {
		fatal("Failed to load GLTF vertex shader");
	}
	if (!loadpshader(&s_mpass.psh, glrmem, "/app0/assets/misc/gltf.frag.sb")) {
		fatal("Failed to load GLTF frag shader");
	}

	// init sampler
	s_mpass.sampler = (GnmSampler){
		.xyminfilter = GNM_FILTER_BILINEAR,
		.xymagfilter = GNM_FILTER_BILINEAR,
		.mipfilter = GNM_MIPFILTER_LINEAR,
		.zfilter = GNM_ZFILTER_POINT,
		.minlod = 0,
		.maxlod = 0xfff,
	};

	// init const buffer
	s_mpass.cbufmem =
		memalloc_alloc(glrmem, sizeof(ConstBuffer), GNM_ALIGNMENT_BUFFER_BYTES);
	if (!s_mpass.cbufmem) {
		fatal("Failed to allocate const buffer");
	}
	s_mpass.constbuf =
		gnmCreateConstBuffer(s_mpass.cbufmem, sizeof(ConstBuffer));

	// prepare fetch shader
	const GnmFetchShaderCreateInfo fetchci = {
		.regs = &s_mpass.vsh->registers,
		.vtxinputs = gnmVsShaderInputSemanticTable(s_mpass.vsh),
		.numvtxinputs = s_mpass.vsh->numinputsemantics,
		.inputusages = gnmVsShaderInputUsageSlotTable(s_mpass.vsh),
		.numinputusages = s_mpass.vsh->common.numinputusageslots,
	};
	uint32_t fetchsize = 0;
	GnmError err = gnmFetchShaderCalcSize(&fetchsize, &fetchci);
	if (err != GNM_ERROR_OK) {
		fatalf("Failed to calc fetch shader size with %s", gnmStrError(err));
	}

	s_mpass.fetchshmem =
		memalloc_alloc(glrmem, fetchsize, GNM_ALIGNMENT_FETCHSHADER_BYTES);
	if (!s_mpass.fetchshmem) {
		fatal("allocgarlicmem for fetch shader failed");
	}

	GnmFetchShaderResults fetchres = {0};
	err = gnmCreateFetchShader(
		s_mpass.fetchshmem, fetchsize, &fetchci, &fetchres
	);
	if (err != GNM_ERROR_OK) {
		fatalf("Failed to create fetch shader with %s", gnmStrError(err));
	}

	gnmVsRegsSetFetchShaderModifier(&s_mpass.vsh->registers, &fetchres);

	// init const buffer data
	s_mpass.location = (vec3s){
		.x = 0.0,
		.y = 0.5,
		.z = 3.0,
	};
	s_mpass.rotation = (vec3s){
		.x = -30.0,
		.y = 0.0,
		.z = 0.0,
	};

	s_mpass.cbufmem->c_mproj = glms_perspective(
		glm_rad(90.0), (float)s_mpass.viewwidth / (float)s_mpass.viewheight,
		0.1, 100.0
	);
	s_mpass.cbufmem->c_mmodel = GLMS_MAT4_IDENTITY;

	s_mpass.cbufmem->c_lights[0] = (PointLight){
		.position = (vec3s){{2.0, 2.0, 0.0}},
		.falloff = calcsquaredfalloffinv(2.5),
		.color = (vec3s){{0.0, 0.85, 0.0}},
		.colorintensity = 1500.0,
	};
	s_mpass.cbufmem->c_lights[1] = (PointLight){
		.position = (vec3s){{2.0, 2.0, 4.0}},
		.falloff = calcsquaredfalloffinv(5.0),
		.color = (vec3s){{0.55, 0.0, 0.5}},
		.colorintensity = 1500.0,
	};
}

void pass_model_update(void) {
	vec3s translation = s_mpass.location;
	translation.y *= -1.0;
	const mat4s tranm = glms_translate(GLMS_MAT4_IDENTITY, translation);

	const vec3s xup = {{1.0, 0.0, 0.0}};
	const vec3s yup = {{0.0, 1.0, 0.0}};
	const vec3s zup = {{0.0, 0.0, 1.0}};

	s_mpass.rotation.y += 1.0;
	if (s_mpass.rotation.y >= 360.0) {
		s_mpass.rotation.y = 0.0;
	}

	// setup camera before render work
	mat4s rotm = GLMS_MAT4_IDENTITY;
	rotm = glms_rotate(rotm, glm_rad(s_mpass.rotation.x), xup);
	rotm = glms_rotate(rotm, glm_rad(s_mpass.rotation.y), yup);
	rotm = glms_rotate(rotm, glm_rad(s_mpass.rotation.z), zup);
	// third person view
	const mat4s matview = glms_mat4_mul(tranm, rotm);

	s_mpass.cbufmem->c_mview = matview;
	s_mpass.cbufmem->c_viewpos = glms_mat4_inv(matview).col[3];
}

void pass_model_render(
	GnmCommandBuffer* cmd, gltfModel* model, GnmRenderTarget* rt,
	GnmDepthRenderTarget* drt
) {
	const GnmPrimitiveSetup primsetup = {
		.cullmode = GNM_CULL_NONE,
		.frontface = GNM_FACE_CCW,
		.frontmode = GNM_FILL_SOLID,
		.backmode = GNM_FILL_SOLID,
		.provokemode = GNM_PROVOKINGVTX_FIRST,
	};
	gnmDrawCmdSetPrimitiveSetup(cmd, &primsetup);

	gnmDrawCmdSetRenderTarget(cmd, 0, rt);
	gnmDrawCmdSetRenderTargetMask(cmd, 0xf);
	gnmDrawCmdSetDepthRenderTarget(cmd, drt);

	// setup controls
	const GnmBlendControl blendctrl = {0};
	const GnmDbRenderControl dbrenderctrl = {0};
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
	gnmDrawCmdSetBlendControl(cmd, 0, &blendctrl);
	gnmDrawCmdSetDbRenderControl(cmd, &dbrenderctrl);
	gnmDrawCmdSetDepthStencilControl(cmd, &depthstencilctrl);

	// setup viewport
	setupviewport(cmd, 0, 0, s_mpass.viewwidth, s_mpass.viewheight, 0.5, 0.5);

	gnmDrawCmdSetVsShader(cmd, &s_mpass.vsh->registers, 0);
	gnmDrawCmdSetPsShader(cmd, &s_mpass.psh->registers);

	// set fetch shader in vertex shader
	// vertex buffer and descriptor set are applied later when drawing meshes
	gnmDrawCmdSetPointerUserData(cmd, GNM_STAGE_VS, 0, s_mpass.fetchshmem);

	gnmDrawCmdSetPsInputUsage(
		cmd, gnmVsShaderExportSemanticTable(s_mpass.vsh),
		s_mpass.vsh->numexportsemantics,
		gnmPsShaderInputSemanticTable(s_mpass.psh),
		s_mpass.psh->numinputsemantics
	);

	// draw meshes
	gnmDrawCmdSetPrimitiveType(cmd, GNM_PT_TRILIST);
	gnmDrawCmdSetIndexSize(cmd, GNM_INDEX_32, GNM_POLICY_LRU);
	for (size_t i = 0; i < uvlen(&model->PrimMeshes); i += 1) {
		R_Mesh* curmesh = uvdata(&model->PrimMeshes, i);

		gnmDrawCmdSetPointerUserData(cmd, GNM_STAGE_VS, 2, curmesh->vertbufs);

		// create and apply descriptor set in vertex shader
		VsDescSet* vsdescset = gnmCmdAllocInside(cmd, sizeof(VsDescSet), 4);
		assert(vsdescset);
		*vsdescset = (VsDescSet){
			.constbuf = s_mpass.constbuf,
		};
		gnmDrawCmdSetPointerUserData(cmd, GNM_STAGE_VS, 6, vsdescset);

		// create and apply descriptor set in pixel shader
		PsDescSet* psdescset = gnmCmdAllocInside(cmd, sizeof(PsDescSet), 4);
		assert(psdescset);
		*psdescset = (PsDescSet){
			.texture = curmesh->material.albedo,
			.constbuf = s_mpass.constbuf,
			.sampler = s_mpass.sampler,
		};
		gnmDrawCmdSetPointerUserData(cmd, GNM_STAGE_PS, 0, psdescset);

		gnmDrawCmdDrawIndex(cmd, curmesh->numindices, curmesh->indexmem);
	}
}

vec3s pass_model_getloc(void) {
	return s_mpass.location;
}
vec3s pass_model_getrot(void) {
	return s_mpass.rotation;
}

vec3s pass_model_worldtoscreen(const vec3s worldpos) {
	// viewport's height is inverted
	const vec4s viewport = {{0.0, 0.0, s_mpass.viewwidth, s_mpass.viewheight}};

	// cglm suggests to calculate MVP by multiping viewProj with model matrix,
	// but since we don't have one (or it's an identity matrix or something),
	// skip that calculation
	const mat4s mvp =
		glms_mat4_mul(s_mpass.cbufmem->c_mproj, s_mpass.cbufmem->c_mview);
	return glms_project(worldpos, mvp, viewport);
}
