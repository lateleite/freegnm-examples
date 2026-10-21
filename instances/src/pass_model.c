#include "pass_model.h"

#include <cglm/struct.h>

#include "u/utility.h"

#include "gltfmodel.h"
#include "misc.h"

static inline float calcsquaredfalloffinv(const float falloff) {
	const float sqr = falloff * falloff;
	return sqr > 0.0 ? (1 / sqr) : 0;
}

// from https://easings.net
static inline float easeinout(float x) {
	return -(cosf(M_PI * x) - 1) / 2;
}

static const uint32_t NUM_INSTANCES = 6;
static const float GAP_INSTANCES = 4.0;
static const float Y_MOVING_DIST = 4.0;

typedef struct {
	mat4s c_mproj;
	mat4s c_mview;
	mat4s c_mmodel;
	vec3s c_viewpos;
	uint32_t c_numlights;

	PointLight c_lights[3];
} ConstBuffer;
_Static_assert(sizeof(ConstBuffer) == 0x130, "");

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

	vec3s* instmem;
	GnmBuffer instbuf;

	void* fetchshmem;

	vec3s location;
	vec3s rotation;

	uint32_t viewwidth;
	uint32_t viewheight;
	float curtime;
} PassModel;
static PassModel s_mpass = {0};

void pass_model_init(
	MemoryAllocator* glrmem, uint32_t screenwidth, uint32_t screenheight
) {
	s_mpass = (PassModel){
		.viewwidth = screenwidth,
		.viewheight = screenheight,
	};

	// load shaders
	if (!loadvshader(
			&s_mpass.vsh, glrmem, "/app0/assets/misc/instances.vert.sb"
		)) {
		fatal("Failed to load GLTF vertex shader");
	}
	if (!loadpshader(
			&s_mpass.psh, glrmem, "/app0/assets/misc/instances.frag.sb"
		)) {
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

	// init instanced buffer
	s_mpass.instmem = memalloc_alloc(
		glrmem, sizeof(vec3s) * NUM_INSTANCES, GNM_ALIGNMENT_BUFFER_BYTES
	);
	if (!s_mpass.instmem) {
		fatal("Failed to allocate instanced buffer");
	}
	s_mpass.instbuf = gnmCreateVertexBuffer(
		s_mpass.instmem, GNM_FMT_R32G32B32_FLOAT, sizeof(vec3s), NUM_INSTANCES
	);

	const float xstart = -((NUM_INSTANCES * GAP_INSTANCES) / 2);
	for (uint32_t i = 0; i < NUM_INSTANCES; i += 1) {
		s_mpass.instmem[i] = (vec3s){{xstart + GAP_INSTANCES * i, 0.0, 0.0}};
	}

	// prepare fetch shader
	const GnmFetchShaderInstancingMode instancedata[5] = {
		GNM_FETCH_MODE_VERTEXINDEX, GNM_FETCH_MODE_VERTEXINDEX,
		GNM_FETCH_MODE_VERTEXINDEX, GNM_FETCH_MODE_VERTEXINDEX,
		GNM_FETCH_MODE_INSTANCEID,
	};
	const GnmFetchShaderCreateInfo fetchci = {
		.regs = &s_mpass.vsh->registers,
		.vtxinputs = gnmVsShaderInputSemanticTable(s_mpass.vsh),
		.numvtxinputs = s_mpass.vsh->numinputsemantics,
		.inputusages = gnmVsShaderInputUsageSlotTable(s_mpass.vsh),
		.numinputusages = s_mpass.vsh->common.numinputusageslots,
		.instancedata = instancedata,
		.numinstancedata = uasize(instancedata),
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
		.z = 4.0,
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

	// 99.0 scale should be enough to make the apple 2m tall
	const float scaleval = 297.0;
	s_mpass.cbufmem->c_mmodel = glms_scale(
		GLMS_MAT4_IDENTITY,
		(vec3s){
			.x = scaleval,
			.y = scaleval,
			.z = scaleval,
		}
	);

	s_mpass.cbufmem->c_numlights = uasize(s_mpass.cbufmem->c_lights);
	s_mpass.cbufmem->c_lights[0] = (PointLight){
		.position = (vec3s){{-6.0, 2.0, 0.0}},
		.falloff = calcsquaredfalloffinv(15.0),
		.color = (vec3s){{0.0, 0.85, 0.0}},
		.colorintensity = 1500.0,
	};
	s_mpass.cbufmem->c_lights[1] = (PointLight){
		.position = (vec3s){{6.0, 2.0, 4.0}},
		.falloff = calcsquaredfalloffinv(10.0),
		.color = (vec3s){{0.55, 0.0, 0.5}},
		.colorintensity = 1500.0,
	};
	s_mpass.cbufmem->c_lights[2] = (PointLight){
		.position = (vec3s){{0.0, 2.0, -12.0}},
		.falloff = calcsquaredfalloffinv(16.0),
		.color = (vec3s){{0.1, 0.1, 0.9}},
		.colorintensity = 1500.0,
	};
}

void pass_model_update(float frametime) {
	s_mpass.curtime += frametime;

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
	s_mpass.cbufmem->c_viewpos = glms_vec3(glms_mat4_inv(matview).col[3]);

	const float progress = easeinout(s_mpass.curtime);

	// move instances on the Y axis (up and down),
	// odd instances go down,
	// even instances go up
	for (uint32_t i = 0; i < NUM_INSTANCES; i += 1) {
		const bool even = i % 2 == 0;
		s_mpass.instmem[i].y =
			(even ? Y_MOVING_DIST : -Y_MOVING_DIST) * progress;
	}
}

void pass_model_render(
	MemoryAllocator* glrmem, GnmCommandBuffer* cmd, gltfModel* model,
	GnmRenderTarget* rt, GnmDepthRenderTarget* drt
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

	// set instance count
	gnmDrawCmdSetNumInstances(cmd, NUM_INSTANCES);

	// create vertex buffer pointers for each model's mesh,
	// this is needed because they need an extra pointer for the instance buffer
	// TODO: this sucks, improve it
	const size_t numvbufs = 5;
	const size_t vbarraylen =
		sizeof(GnmBuffer) * numvbufs * model->PrimMeshes.numelements;
	GnmBuffer* vertbufs =
		memalloc_alloc(glrmem, vbarraylen, GNM_ALIGNMENT_BUFFER_BYTES);
	if (!vertbufs) {
		fatal("Failed to allocate vertex buffer");
	}

	// draw meshes
	gnmDrawCmdSetPrimitiveType(cmd, GNM_PT_TRILIST);
	gnmDrawCmdSetIndexSize(cmd, GNM_INDEX_32, GNM_POLICY_LRU);
	for (size_t i = 0; i < uvlen(&model->PrimMeshes); i += 1) {
		R_Mesh* curmesh = uvdata(&model->PrimMeshes, i);

		GnmBuffer* curvbufs = &vertbufs[i * numvbufs];

		// set vertex buffers to be exposed to the shader
		// buffer 0 is position
		// buffer 1 is normal
		// buffer 2 is UV
		// buffer 3 are metallic and roughness
		// buffer 4 is INSTANCED position offset
		curvbufs[0] = curmesh->vertbufs[0];
		curvbufs[1] = curmesh->vertbufs[1];
		curvbufs[2] = curmesh->vertbufs[2];
		curvbufs[3] = curmesh->vertbufs[3];
		curvbufs[4] = s_mpass.instbuf;

		gnmDrawCmdSetPointerUserData(cmd, GNM_STAGE_VS, 2, curvbufs);

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

	// reset instances
	gnmDrawCmdSetNumInstances(cmd, 1);
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
