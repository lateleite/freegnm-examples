#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(set = 0, binding = 0) uniform Constants {
	mat4 mproj;
	mat4 mview;
	mat4 mmodel;
	vec4 viewpos;
	// PointLight lights[2]; // not needed here
} c;

layout(location = 0) in vec3 inpos;
layout(location = 1) in vec3 innorm;
layout(location = 2) in vec2 inuv;
layout(location = 3) in vec2 inmetallic_roughness;

layout(location = 0) out vec3 outnorm;
layout(location = 1) out vec2 outuv;
layout(location = 2) out vec3 outposview;
layout(location = 3) out vec2 outmetallic_roughness;

out gl_PerVertex {
	vec4 gl_Position;
};

void main() {
	const vec4 worldpos = c.mmodel * vec4(inpos, 1.0);

	gl_Position = c.mproj * c.mview * worldpos;
	outnorm = mat3(c.mmodel) * innorm;
	outuv = inuv;
	outposview = worldpos.xyz;
	outmetallic_roughness = inmetallic_roughness;
}
