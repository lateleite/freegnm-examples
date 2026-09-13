#ifndef _MESH_H_
#define _MESH_H_

#include <cglm/types-struct.h>

#include <gnm/buffer.h>
#include <gnm/texture.h>

typedef struct {
	vec3s Position;
	vec3s Normal;
	vec2s TextureUV;
	vec2s metallic_roughness;
} R_Vertex;

typedef struct {
	GnmTexture albedo;
} R_Material;

typedef struct {
	GnmBuffer* vertbufs;
	R_Vertex* vertmem;
	uint32_t* indexmem;
	uint32_t numvertices;
	uint32_t numindices;

	R_Material material;
	float metallic;
	float roughness;
} R_Mesh;

#endif	// _MESH_H_
