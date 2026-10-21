#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(binding = 0) uniform Constants {
	mat4 mvp;
} c;

layout(location = 0) in vec3 inpos;
layout(location = 1) in vec2 inuv;

layout(location = 0) out vec2 outuv;

out gl_PerVertex {
	vec4 gl_Position;
};

void main() {
	gl_Position = c.mvp * vec4(inpos, 1.0);
	outuv = inuv;
}
