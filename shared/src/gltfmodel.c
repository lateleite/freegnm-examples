#include "gltfmodel.h"

#include <cglm/struct.h>

#define CGLTF_IMPLEMENTATION
#include "libs/cgltf.h"

#include "u/fs.h"
#include "u/utility.h"

#include "misc.h"

static R_Material s_defaultmat = {0};

static bool initdefaultmat(MemoryAllocator* garlicmem) {
	static bool init = false;
	if (init) {
		return true;
	}

	const uint32_t width = 2;
	const uint32_t height = 2;
	const GnmDataFormat fmt = GNM_FMT_R8G8B8A8_SRGB;
	const uint32_t numchans = gnmDfGetNumComponents(fmt);
	assert(numchans == 4);
	const size_t datasize = width * height * numchans;

	void* texmem =
		memalloc_alloc(garlicmem, datasize, GNM_ALIGNMENT_SHADER_BYTES);
	if (!texmem) {
		printf("[gltf] Failed to allocate 0x%zx bytes\n", datasize);
		return false;
	}

	// default image is a black and white checkerboard
	static const uint8_t defaultdata[2 * 2][4] = {
		{0, 0, 0, 0},
		{255, 255, 255, 255},
		{255, 255, 255, 255},
		{0, 0, 0, 0},
	};
	static_assert(sizeof(defaultdata) == 16, "");

	assert(datasize == sizeof(defaultdata));
	memcpy(texmem, defaultdata, datasize);

	const GnmTextureCreateInfo ci = {
		.texturetype = GNM_TEXTURE_2D,
		.width = width,
		.height = height,
		.depth = 1,
		.pitch = width,
		.nummiplevels = 1,
		.numslices = 1,
		.format = fmt,
		.tilemodehint = GNM_TM_DISPLAY_LINEAR_GENERAL,
		.mingpumode = GNM_GPU_BASE,
		.numfragments = 1,
	};
	GnmTexture albedotex = {0};
	GnmError err = gnmCreateTexture(&albedotex, &ci);
	if (err != GNM_ERROR_OK) {
		printf("[gltf] Failed to create texture with %s\n", gnmStrError(err));
		memalloc_free(garlicmem, texmem);
		return false;
	}

	gnmTexSetBaseAddress(&albedotex, texmem);

	s_defaultmat = (R_Material){
		.albedo = albedotex,
	};

	init = true;
	return true;
}

static void LoadPrimitiveMaterials(
	R_Mesh* outMesh, cgltf_primitive* inPrimData, MemoryAllocator* garlicmem
) {
	cgltf_material* primMaterial = inPrimData->material;

	if (!primMaterial) {
		puts("[gltf] Primitive has no material, using default");
		outMesh->material = s_defaultmat;
		outMesh->metallic = 0.0;
		outMesh->roughness = 0.001;
		return;
	}

	if (!primMaterial->has_pbr_metallic_roughness) {
		printf("[gltf] Material has no PBR metallic/roughness\n");
		outMesh->material = s_defaultmat;
		outMesh->metallic = 0.0;
		outMesh->roughness = 0.001;
		return;
	}

	cgltf_pbr_metallic_roughness* pbrInfo =
		&primMaterial->pbr_metallic_roughness;

	cgltf_texture* baseColorTex = pbrInfo->base_color_texture.texture;
	if (baseColorTex) {
		cgltf_image* baseColorImage = baseColorTex->image;
		if (!baseColorImage) {
			outMesh->material = s_defaultmat;
			outMesh->metallic = 0.0;
			outMesh->roughness = 0.001;
			return;
		}

		void* bufdata = baseColorImage->buffer_view->data;
		if (!bufdata) {
			bufdata = (uint8_t*)baseColorImage->buffer_view->buffer->data +
				baseColorImage->buffer_view->offset;
		}
		if (!bufdata) {
			fatalf("[gltf] Failed to find albedo image data for %s", baseColorImage->name);
		}

		if (!loadlineartexturemem(
				garlicmem, &outMesh->material.albedo,
				bufdata,
				baseColorImage->buffer_view->size
			)) {
			fatalf(
				"[gltf] Failed to upload albedo image %s", baseColorImage->name
			);
		}
	} else {
		const GnmDataFormat fmt = GNM_FMT_R32G32B32A32_FLOAT;
		const size_t datasize = sizeof(pbrInfo->base_color_factor);
		static_assert(sizeof(pbrInfo->base_color_factor) == 16, "");

		void* texmem =
			memalloc_alloc(garlicmem, datasize, GNM_ALIGNMENT_SHADER_BYTES);
		if (!texmem) {
			fatalf("[gltf] Failed to allocate 0x%zx bytes", datasize);
		}

		const GnmTextureCreateInfo ci = {
			.texturetype = GNM_TEXTURE_2D,
			.width = 1,
			.height = 1,
			.depth = 1,
			.pitch = 1,
			.nummiplevels = 1,
			.numslices = 1,
			.format = fmt,
			.tilemodehint = GNM_TM_DISPLAY_LINEAR_GENERAL,
			.mingpumode = GNM_GPU_BASE,
			.numfragments = 1,
		};
		GnmError gerr = gnmCreateTexture(&outMesh->material.albedo, &ci);
		if (gerr != GNM_ERROR_OK) {
			fatalf(
				"[gltf] Failed to create color albedo image with %s",
				gnmStrError(gerr)
			);
		}

		memcpy(texmem, pbrInfo->base_color_factor, datasize);
		gnmTexSetBaseAddress(&outMesh->material.albedo, texmem);
	}

	// TODO: normal and AO maps
	/*Mat_TextureId newNormalTex;
	cgltf_texture* normTexture = primMaterial->normal_texture.texture;
	if (normTexture) {
		cgltf_image* normImage = normTexture->image;
		if (!normImage) {
			printf("[gltf] model has normal texture without image\n");
			return false;
		}

		newNormalTex = mat_Factory_LoadKtxFromMemory(
			matFactory, normImage->buffer_view->data,
			normImage->buffer_view->size, normImage->name
		);
		if (!newNormalTex) {
			printf(
				"[gltf] Failed to upload normal image %s\n", normImage->name
			);
			return false;
		}
	} else {
		newNormalTex = matFactory->DefaultNormalMap;
	}

	Mat_TextureId newAoTex;
	cgltf_texture* aoTexture = primMaterial->occlusion_texture.texture;
	if (aoTexture) {
		cgltf_image* aoImage = aoTexture->image;
		if (!aoImage) {
			printf("[gltf] model has AO texture without image\n");
			return false;
		}

		newAoTex = mat_Factory_LoadKtxFromMemory(
			matFactory, aoImage->buffer_view->data, aoImage->buffer_view->size,
			aoImage->name
		);
		if (!newAoTex) {
			printf("[gltf] Failed to upload AO image %s\n", aoImage->name);
			return false;
		}
	} else {
		newAoTex = matFactory->DefaultAoMap;
	}

	outMesh->Material = mat_Factory_CreateMaterial(
		matFactory, newAlbedoTex, newNormalTex, newAoTex,
		pbrInfo->metallic_factor, pbrInfo->roughness_factor, primMaterial->name
	);
	if (!outMesh->Material) {
		printf("[gltf] Failed to create material %s\n", primMaterial->name);
		return false;
	}*/
}

static inline bool BuildGltfNode(
	gltfModel* model, cgltf_data* modelData, cgltf_node* inNodeData,
	gltfNode* parent
) {
	// Generate local node matrix
	vec3s translation = GLMS_VEC3_ZERO;
	vec3s scale = GLMS_VEC3_ZERO;
	mat4s rotation = GLMS_MAT4_IDENTITY;
	mat4s nodeMatrix = GLMS_MAT4_IDENTITY;

	if (inNodeData->has_translation) {
		memcpy(&translation, inNodeData->translation, sizeof(translation));
	}
	if (inNodeData->has_scale) {
		memcpy(&scale, inNodeData->scale, sizeof(scale));
	}
	if (inNodeData->has_rotation) {
		versors q;
		memcpy(&q, inNodeData->rotation, sizeof(q));
		rotation = glms_quat_mat4(q);
	}
	if (inNodeData->has_matrix) {
		memcpy(&nodeMatrix, inNodeData->matrix, sizeof(nodeMatrix));
	}

	gltfNode newNode = {
		.Parent = parent,

		.Translation = translation,
		.Scale = scale,
		.Rotation = rotation,
		.Matrix = nodeMatrix,

		.PrimMeshesIndex = 0,
		.NumPrimMeshes = 0,
		.Index = inNodeData - modelData->nodes,

		.HasMesh = inNodeData->mesh != NULL,
	};

	uvappend(&model->LinearNodes, &newNode);
	gltfNode* outNode =
		uvdata(&model->LinearNodes, uvlen(&model->LinearNodes) - 1);

	for (cgltf_size i = 0; i < inNodeData->children_count; i += 1) {
		if (!BuildGltfNode(
				model, modelData, inNodeData->children[i], outNode
			)) {
			return false;
		}
	}

	return true;
}

static bool uploadvertices(
	R_Mesh* mesh, MemoryAllocator* garlicmem, const R_Vertex* vertices,
	uint32_t numvertices
) {
	if (!numvertices) {
		puts("uploadvertices: no vertices were given");
		return false;
	}

	const size_t vertsize = numvertices * sizeof(R_Vertex);

	// init vertex buffer
	uint8_t* vertmem =
		memalloc_alloc(garlicmem, vertsize, GNM_ALIGNMENT_BUFFER_BYTES);
	if (!vertmem) {
		puts("uploadvertices: failed to alloc vertex memory");
		return false;
	}
	memcpy(vertmem, vertices, vertsize);

	GnmBuffer* vertbufs = memalloc_alloc(
		garlicmem, sizeof(GnmBuffer) * 4, GNM_ALIGNMENT_BUFFER_BYTES
	);
	if (!vertbufs) {
		puts("allocgarlicmem for vertex buffers failed");
		memalloc_free(garlicmem, vertmem);
		return false;
	}

	// these buffer descriptors must be visible to the GPU.
	// buffer 0 is position
	// buffer 1 is normal
	// buffer 2 is UV
	// buffer 3 are metallic and roughness
	vertbufs[0] = gnmCreateVertexBuffer(
		vertmem, GNM_FMT_R32G32B32_FLOAT, sizeof(R_Vertex), numvertices
	);
	vertbufs[1] = gnmCreateVertexBuffer(
		vertmem + 12, GNM_FMT_R32G32B32_FLOAT, sizeof(R_Vertex), numvertices
	);
	vertbufs[2] = gnmCreateVertexBuffer(
		vertmem + 24, GNM_FMT_R32G32_FLOAT, sizeof(R_Vertex), numvertices
	);
	vertbufs[3] = gnmCreateVertexBuffer(
		vertmem + 32, GNM_FMT_R32G32_FLOAT, sizeof(R_Vertex), numvertices
	);

	mesh->vertbufs = vertbufs;
	mesh->vertmem = (R_Vertex*)vertmem;
	mesh->numvertices = numvertices;
	return true;
}

static bool uploadindices(
	R_Mesh* mesh, MemoryAllocator* garlicmem, const uint32_t* indices,
	uint32_t numindices
) {
	if (!numindices) {
		puts("uploadvertices: no vertices were given");
		return false;
	}

	const size_t indsize = numindices * sizeof(uint32_t);

	// init index buffer
	uint32_t* idxmem =
		memalloc_alloc(garlicmem, indsize, GNM_ALIGNMENT_BUFFER_BYTES);
	if (!idxmem) {
		puts("uploadvertices: failed to alloc vertex memory");
		return false;
	}
	memcpy(idxmem, indices, indsize);

	mesh->indexmem = idxmem;
	mesh->numindices = numindices;
	return true;
}

static bool GltfModel_LoadNodeMesh(
	gltfModel* model, gltfNode* outNode, cgltf_node* inNodeData,
	MemoryAllocator* garlicmem, bool preTransform, bool flipY
) {
	cgltf_mesh* inMesh = inNodeData->mesh;

	outNode->PrimMeshesIndex = uvlen(&model->PrimMeshes);
	outNode->NumPrimMeshes = inMesh->primitives_count;

	uvreserve(&model->PrimMeshes, inMesh->primitives_count);

	mat4s localMatrix = {0};
	cgltf_node_transform_world(inNodeData, (float*)&localMatrix);

	for (cgltf_size i = 0; i < inMesh->primitives_count; i += 1) {
		cgltf_primitive* prim = &inMesh->primitives[i];

		UVec Vertices = uvalloc(sizeof(R_Vertex), 0);
		UVec Indices = uvalloc(sizeof(uint32_t), 0);

		R_Mesh newPrimMesh = {0};
		LoadPrimitiveMaterials(&newPrimMesh, prim, garlicmem);

		cgltf_accessor* posAccessor = NULL;
		cgltf_accessor* normAccessor = NULL;
		cgltf_accessor* texCoordAccessor = NULL;

		for (cgltf_size y = 0; y < prim->attributes_count; y++) {
			cgltf_attribute* attr = &prim->attributes[y];
			switch (attr->type) {
			case cgltf_attribute_type_position:
				posAccessor = attr->data;
				break;
			case cgltf_attribute_type_normal:
				normAccessor = attr->data;
				break;
			case cgltf_attribute_type_texcoord:
				texCoordAccessor = attr->data;
				break;
			case cgltf_attribute_type_tangent:
				// tangentCoordAccessor = attr->data;
				break;
			default:
				printf("[gltf] skipping attribute type %i\n", attr->type);
				break;
			}
		}

		for (cgltf_size v = 0; v < posAccessor->count; v++) {
			R_Vertex vert;

			vert.metallic_roughness =
				(vec2s){{newPrimMesh.metallic, newPrimMesh.roughness}};

			if (!cgltf_accessor_read_float(
					posAccessor, v, (float*)&vert.Position, 3
				)) {
				printf(
					"GltfModel_LoadNodeMesh: failed to read position at index "
					"%lu\n",
					v
				);
				return false;
			}

			if (normAccessor) {
				if (!cgltf_accessor_read_float(
						normAccessor, v, (float*)&vert.Normal, 3
					)) {
					printf(
						"GltfModel_LoadNodeMesh: failed to read normal at "
						"index %lu\n",
						v
					);
					return false;
				}
				vert.Normal = glms_normalize(vert.Normal);
			} else {
				vert.Normal = GLMS_VEC3_ZERO;
			}

			if (texCoordAccessor) {
				if (!cgltf_accessor_read_float(
						texCoordAccessor, v, (float*)&vert.TextureUV, 2
					)) {
					printf(
						"GltfModel_LoadNodeMesh: failed to read UV at index "
						"%lu\n",
						v
					);
					return false;
				}
			} else {
				vert.TextureUV = GLMS_VEC2_ZERO;
			}

			if (preTransform) {
				vert.Position = glms_vec3(
					glms_mat4_mulv(localMatrix, glms_vec4(vert.Position, 1.0))
				);
				vert.Normal = glms_normalize(
					glms_mat3_mulv(glms_mat4_pick3(localMatrix), vert.Normal)
				);
			}

			if (flipY) {
				vert.Position.y *= -1.0;
				vert.Normal.y *= -1.0;
			}

			uvappend(&Vertices, &vert);
		}

		// indices
		cgltf_accessor* indexAccessor = prim->indices;

		/*if (!UVector_Reserve(&newPrimMesh.Indices, indexAccessor->count)) {
			printf(
				"[gltf] failed to reserve %lu indices\n", indexAccessor->count
			);
			return false;
		}*/

		for (cgltf_size index = 0; index < indexAccessor->count; index++) {
			uint32_t curIndex = cgltf_accessor_read_index(indexAccessor, index);
			uvappend(&Indices, &curIndex);
		}

		const uint32_t numvertices = uvlen(&Vertices);
		if (numvertices > 0) {
			const R_Vertex* vertptr = uvdatac(&Vertices, 0);
			if (!uploadvertices(
					&newPrimMesh, garlicmem, vertptr, numvertices
				)) {
				puts("[gltf] Failed to upload vertices");
				return false;
			}
		}
		const uint32_t numindices = uvlen(&Indices);
		if (numindices > 0) {
			const uint32_t* indptr = uvdatac(&Indices, 0);
			if (!uploadindices(&newPrimMesh, garlicmem, indptr, numindices)) {
				puts("[gltf] Failed to upload indices");
				return false;
			}
		}

		uvappend(&model->PrimMeshes, &newPrimMesh);
	}

	return true;
}

static bool GltfModel_LoadScene(
	gltfModel* model, cgltf_data* modelData, cgltf_scene* scene,
	MemoryAllocator* garlicmem, bool preTransform, bool flipY
) {
	/*if (!UVector_Reserve(&model->LinearNodes, scene->nodes_count)) {
		printf(
			"[gltf] failed to reserve memory for %lu nodes\n",
			scene->nodes_count
		);
		return false;
	}*/

	for (cgltf_size i = 0; i < scene->nodes_count; i += 1) {
		if (!BuildGltfNode(model, modelData, scene->nodes[i], NULL)) {
			printf("[gltf] failed to parse node %lu\n", i);
			return false;
		}
	}

	for (size_t i = 0; i < uvlen(&model->LinearNodes); i += 1) {
		gltfNode* curNode = uvdata(&model->LinearNodes, i);
		if (curNode->HasMesh) {
			assert(curNode->Index < modelData->nodes_count);

			cgltf_node* inNode = &modelData->nodes[curNode->Index];
			if (!GltfModel_LoadNodeMesh(
					model, curNode, inNode, garlicmem, preTransform, flipY
				)) {
				printf("[gltf] failed to load mesh of node %lu\n", i);
				return false;
			}
		}
	}

	return true;
}

static bool GltfModel_Init(
	gltfModel* model, const void* data, size_t dataSize, const char* path,
	MemoryAllocator* garlicmem, bool preTransform, bool flipY
) {
	model->LinearNodes = uvalloc(sizeof(gltfNode), 0);
	model->PrimMeshes = uvalloc(sizeof(R_Mesh), 0);

	cgltf_options loadOptions = {0};
	cgltf_data* modelData;
	cgltf_result res = cgltf_parse(&loadOptions, data, dataSize, &modelData);
	if (res != cgltf_result_success) {
		printf("[gltf] failed to parse %s with status %i\n", path, res);
		return false;
	}

	res = cgltf_load_buffers(&loadOptions, modelData, path);
	if (res == cgltf_result_success) {
		res = cgltf_validate(modelData);
		if (res == cgltf_result_success) {
			if (modelData->scenes_count > 0) {
				cgltf_scene* scene = &modelData->scenes[0];

				if (!GltfModel_LoadScene(
						model, modelData, scene, garlicmem, preTransform, flipY
					)) {
					printf("[gltf] failed to parse model %s scene\n", path);
					res = cgltf_result_invalid_gltf;
				}
			} else {
				printf("[gltf] model %s has no scenes\n", path);
			}
		} else {
			printf("[gltf] failed to validate %s with status %i\n", path, res);
		}
	} else {
		printf(
			"[gltf] failed to load buffers for %s with status %i\n", path, res
		);
	}

	cgltf_free(modelData);
	return res == cgltf_result_success;
}

bool LoadGltfModel(
	gltfModel* outModel, const char* path, MemoryAllocator* garlicmem,
	bool preTransform, bool flipY
) {
	assert(outModel);

	if (!initdefaultmat(garlicmem)) {
		puts("LoadGltfModel: failed to init default material");
		return false;
	}

	void* filebuf = NULL;
	size_t filesize = 0;
	int readres = readfile(path, &filebuf, &filesize);
	if (readres != 0) {
		printf("LoadGltfModel: failed to read %s with %i\n", path, readres);
		return false;
	}

	bool res = GltfModel_Init(
		outModel, filebuf, filesize, path, garlicmem, preTransform, flipY
	);
	free(filebuf);
	if (!res) {
		printf("Failed to parse gltf model %s\n", path);
		return false;
	}

	outModel->path = path;
	return res;
}
