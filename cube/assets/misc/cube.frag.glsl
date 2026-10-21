#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec2 inuv;

layout(location = 0) out vec4 outcol;

layout(set = 0, binding = 0) uniform sampler2D tex;

void main() {
	const vec3 col = texture(tex, inuv).rgb;
	outcol = vec4(col, 1.0);
}
