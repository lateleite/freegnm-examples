#include "pass_ui.h"

#include <stdarg.h>

#include <cglm/struct.h>

#include <gnm/drawcommandbuffer.h>

#include "font2d.h"
#include "misc.h"
#include "u/utility.h"

static const uint32_t MAX_FONT_VERTICES = 8196;

typedef struct {
	vec2s pos;
	vec2s uv;
	vec4s color;
} FontVertex;
_Static_assert(sizeof(FontVertex) == 0x20, "");

typedef struct {
	mat4s c_mproj;
} FontConstants;
_Static_assert(sizeof(FontConstants) == 0x40, "");

typedef struct {
	GnmBuffer constbuf;	 // FontConstants
} FontVsDescSet;
_Static_assert(sizeof(FontVsDescSet) == 0x10, "");

typedef struct {
	GnmTexture tex;
	GnmSampler samp;
} FontPsDescSet;
_Static_assert(sizeof(FontPsDescSet) == 0x30, "");

typedef struct {
	GnmVsShader* fontvs;
	GnmPsShader* fontps;

	FontConstants* fontcbufmem;
	GnmBuffer fontcbuf;
	GnmSampler fontsampler;

	void* fontfetchmem;

	FontVertex* fontvertmem;
	size_t fontnumverts;
	GnmBuffer* fontvertbufs;

	R_Font2D font;
} PassUi;

static PassUi s_ui = {0};

// append character quad as 2 triangles
static inline void appendquad(
	PassUi* b, const stbtt_aligned_quad* q, ui_color col
) {
	if (b->fontnumverts + 6 > MAX_FONT_VERTICES) {
		fatal("ui: vertices have overflown");
	}
	const uint32_t curverts = b->fontnumverts;
	b->fontvertmem[curverts + 0] = (FontVertex){
		.pos = {{q->x0, q->y0}},
		.uv = {{q->s0, q->t0}},
		.color = {{col.r, col.g, col.b, col.a}},
	};
	b->fontvertmem[curverts + 1] = (FontVertex){
		.pos = {{q->x0, q->y1}},
		.uv = {{q->s0, q->t1}},
		.color = {{col.r, col.g, col.b, col.a}},
	};
	b->fontvertmem[curverts + 2] = (FontVertex){
		.pos = {{q->x1, q->y1}},
		.uv = {{q->s1, q->t1}},
		.color = {{col.r, col.g, col.b, col.a}},
	};
	b->fontvertmem[curverts + 3] = (FontVertex){
		.pos = {{q->x1, q->y0}},
		.uv = {{q->s1, q->t0}},
		.color = {{col.r, col.g, col.b, col.a}},
	};
	b->fontvertmem[curverts + 4] = (FontVertex){
		.pos = {{q->x0, q->y0}},
		.uv = {{q->s0, q->t0}},
		.color = {{col.r, col.g, col.b, col.a}},
	};
	b->fontvertmem[curverts + 5] = (FontVertex){
		.pos = {{q->x1, q->y1}},
		.uv = {{q->s1, q->t1}},
		.color = {{col.r, col.g, col.b, col.a}},
	};
	b->fontnumverts += 6;
}

void pass_ui_init(
	uint32_t viewwidth, uint32_t viewheight, MemoryAllocator* glrmem
) {
	// load font
	s_ui.font =
		r_LoadFontFromFile("/app0/assets/fonts/Hack-Regular.ttf", 16, glrmem);

	// load shaders
	if (!loadvshader(&s_ui.fontvs, glrmem, "/app0/assets/misc/font.vert.sb")) {
		fatal("ui: failed to load font VS");
	}
	if (!loadpshader(&s_ui.fontps, glrmem, "/app0/assets/misc/font.frag.sb")) {
		fatal("ui: failed to load font PS");
	}

	// init sampler
	s_ui.fontsampler = (GnmSampler){
		.xyminfilter = GNM_FILTER_POINT,
		.xymagfilter = GNM_FILTER_POINT,
		.mipfilter = GNM_MIPFILTER_POINT,
		.zfilter = GNM_ZFILTER_POINT,
		.minlod = 0,
		.maxlod = 0xfff,
	};

	// init const buffer
	s_ui.fontcbufmem = memalloc_alloc(
		glrmem, sizeof(FontConstants), GNM_ALIGNMENT_BUFFER_BYTES
	);
	assert(s_ui.fontcbufmem);
	s_ui.fontcbuf =
		gnmCreateConstBuffer(s_ui.fontcbufmem, sizeof(FontConstants));

	// setup vertex buffer
	s_ui.fontvertmem = memalloc_alloc(
		glrmem, sizeof(FontVertex) * MAX_FONT_VERTICES,
		GNM_ALIGNMENT_BUFFER_BYTES
	);
	assert(s_ui.fontvertmem);

	s_ui.fontvertbufs = memalloc_alloc(
		glrmem, sizeof(GnmBuffer) * 3, GNM_ALIGNMENT_BUFFER_BYTES
	);
	assert(s_ui.fontvertbufs);
	s_ui.fontvertbufs[0] = gnmCreateVertexBuffer(
		s_ui.fontvertmem, GNM_FMT_R32G32_FLOAT, sizeof(FontVertex),
		MAX_FONT_VERTICES
	);
	s_ui.fontvertbufs[1] = gnmCreateVertexBuffer(
		(uint8_t*)s_ui.fontvertmem + 8, GNM_FMT_R32G32_FLOAT,
		sizeof(FontVertex), MAX_FONT_VERTICES
	);
	s_ui.fontvertbufs[2] = gnmCreateVertexBuffer(
		(uint8_t*)s_ui.fontvertmem + 16, GNM_FMT_R32G32B32A32_FLOAT,
		sizeof(FontVertex), MAX_FONT_VERTICES
	);

	// prepare fetch shader
	const GnmFetchShaderCreateInfo fetchci = {
		.regs = &s_ui.fontvs->registers,
		.vtxinputs = gnmVsShaderInputSemanticTable(s_ui.fontvs),
		.numvtxinputs = s_ui.fontvs->numinputsemantics,
		.inputusages = gnmVsShaderInputUsageSlotTable(s_ui.fontvs),
		.numinputusages = s_ui.fontvs->common.numinputusageslots,
	};
	uint32_t fetchsize = 0;
	GnmError err = gnmFetchShaderCalcSize(&fetchsize, &fetchci);
	if (err != GNM_ERROR_OK) {
		fatalf("Failed to calc fetch shader size with %s", gnmStrError(err));
	}

	s_ui.fontfetchmem =
		memalloc_alloc(glrmem, fetchsize, GNM_ALIGNMENT_FETCHSHADER_BYTES);
	assert(s_ui.fontfetchmem);

	GnmFetchShaderResults fetchres = {0};
	err =
		gnmCreateFetchShader(s_ui.fontfetchmem, fetchsize, &fetchci, &fetchres);
	if (err != GNM_ERROR_OK) {
		fatalf("Failed to create fetch shader with %s", gnmStrError(err));
	}

	gnmVsRegsSetFetchShaderModifier(&s_ui.fontvs->registers, &fetchres);

	// init const buffer data
	*s_ui.fontcbufmem = (FontConstants){
		.c_mproj = glms_ortho(0.0, viewwidth, viewheight, 0.0, 0.0, 1.0),
	};
}

void pass_ui_begin(GnmCommandBuffer* cmd) {
	//
	// bind font resources like shaders and buffers
	//

	// reset vertices
	s_ui.fontnumverts = 0;

	// set controls
	const GnmPrimitiveSetup primsetup = {
		.cullmode = GNM_CULL_NONE,
		.frontface = GNM_FACE_CCW,
		.frontmode = GNM_FILL_SOLID,
		.backmode = GNM_FILL_SOLID,
		.provokemode = GNM_PROVOKINGVTX_FIRST,
	};
	const GnmBlendControl blendctrl = {
		.blendenabled = true,
		.colorfunc = GNM_COMB_DST_PLUS_SRC,
		.colorsrcmult = GNM_BLEND_SRC_ALPHA,
		.colordstmult = GNM_BLEND_ONE_MINUS_SRC_ALPHA,
		.separatealphaenable = false,
	};
	const GnmDbRenderControl dbrenderctrl = {0};
	const GnmDepthStencilControl depthstencilctrl = {0};
	gnmDrawCmdSetPrimitiveSetup(cmd, &primsetup);
	gnmDrawCmdSetBlendControl(cmd, 0, &blendctrl);
	gnmDrawCmdSetDbRenderControl(cmd, &dbrenderctrl);
	gnmDrawCmdSetDepthStencilControl(cmd, &depthstencilctrl);

	// set shader
	gnmDrawCmdSetVsShader(cmd, &s_ui.fontvs->registers, 0);
	gnmDrawCmdSetPsShader(cmd, &s_ui.fontps->registers);

	// VS: set fetch shader and vertex buffers
	gnmDrawCmdSetPointerUserData(cmd, GNM_STAGE_VS, 0, s_ui.fontfetchmem);
	gnmDrawCmdSetPointerUserData(cmd, GNM_STAGE_VS, 2, s_ui.fontvertbufs);

	// create and set vertex shader descriptor set
	FontVsDescSet* vsdescset = gnmCmdAllocInside(cmd, sizeof(FontVsDescSet), 4);
	assert(vsdescset);
	*vsdescset = (FontVsDescSet){
		.constbuf = s_ui.fontcbuf,
	};
	gnmDrawCmdSetPointerUserData(cmd, GNM_STAGE_VS, 6, vsdescset);

	// create and set pixel shader descriptor set
	FontPsDescSet* psdescset = gnmCmdAllocInside(cmd, sizeof(FontPsDescSet), 4);
	assert(psdescset);
	*psdescset = (FontPsDescSet){
		.tex = s_ui.font.bitmaptexture,
		.samp = s_ui.fontsampler,
	};
	gnmDrawCmdSetPointerUserData(cmd, GNM_STAGE_PS, 0, psdescset);

	gnmDrawCmdSetPsInputUsage(
		cmd, gnmVsShaderExportSemanticTable(s_ui.fontvs),
		s_ui.fontvs->numexportsemantics,
		gnmPsShaderInputSemanticTable(s_ui.fontps),
		s_ui.fontps->numinputsemantics
	);
}

void pass_ui_end(GnmCommandBuffer* cmd, GnmRenderTarget* rt) {
	// draw to render target
	gnmDrawCmdSetRenderTarget(cmd, 0, rt);
	gnmDrawCmdSetRenderTargetMask(cmd, 0xf);

	// draw font quads
	gnmDrawCmdSetPrimitiveType(cmd, GNM_PT_TRILIST);
	gnmDrawCmdDrawIndexAuto(cmd, s_ui.fontnumverts);
}

static inline void drawuitext(
	float x, float y, R_Font2D* f, ui_color color, const char* text, int textlen
) {
	for (int i = 0; i < textlen; i += 1) {
		const char c = text[i];
		if (c >= 32 && c <= 127) {
			stbtt_aligned_quad q = {0};
			stbtt_GetBakedQuad(f->chardata, 512, 512, c - 32, &x, &y, &q, 1);
			appendquad(&s_ui, &q, color);
		}
	}
}

void pass_ui_drawtext(float x, float y, ui_color color, const char* text) {
	drawuitext(x, y, &s_ui.font, color, text, strlen(text));
}

void pass_ui_drawtextf(float x, float y, ui_color color, const char* fmt, ...) {
	char buf[512] = {0};
	va_list args;
	va_start(args, fmt);
	const int textlen = vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);

	drawuitext(x, y, &s_ui.font, color, buf, textlen);
}
