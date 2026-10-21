#ifndef _MODELS_GLTFMODEL_H_
#define _MODELS_GLTFMODEL_H_

#include "u/vector.h"

#include "memalloc.h"
#include "mesh.h"

typedef struct gltfNode {
	struct gltfNode* Parent;

	vec3s Translation;
	vec3s Scale;
	mat4s Rotation;
	mat4s Matrix;

	size_t PrimMeshesIndex;
	size_t NumPrimMeshes;
	size_t Index;

	bool HasMesh;
} gltfNode;

typedef struct {
	UVec LinearNodes;  // gltfNode
	UVec PrimMeshes;   // R_Mesh
			
	const char* path;
} gltfModel;

bool LoadGltfModel(
	gltfModel* outModel, const char* path, MemoryAllocator* garlicmem,
	bool preTransform, bool flipY
);

#endif	// _MODELS_GLTFMODEL_H_
