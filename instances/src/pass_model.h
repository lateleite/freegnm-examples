#ifndef _PASS_MODEL_H_
#define _PASS_MODEL_H_

#include <cglm/types-struct.h>

#include <gnm/drawcommandbuffer.h>

#include "gltfmodel.h"
#include "memalloc.h"

typedef struct {
	vec3s position;
	float falloff;	// in centimeters
	vec3s color;
	float colorintensity;  // in lumens
} PointLight;
_Static_assert(sizeof(PointLight) == 0x20, "");

void pass_model_init(
	MemoryAllocator* glrmem, uint32_t screenwidth, uint32_t screenheight
);
void pass_model_update(float frametime);
void pass_model_render(
	MemoryAllocator* glrmem, GnmCommandBuffer* cmd, gltfModel* model,
	GnmRenderTarget* rt, GnmDepthRenderTarget* drt
);

vec3s pass_model_getloc(void);
vec3s pass_model_getrot(void);

vec3s pass_model_worldtoscreen(const vec3s worldpos);

#endif	//_PASS_MODEL_H_
