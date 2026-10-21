#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(set = 0, binding = 0) uniform Constants {
	vec4 color;
	vec4 pos;
} c;

layout(location = 0) in vec2 inpos;

layout(location = 0) out vec4 outcolor;

out gl_PerVertex {
	vec4 gl_Position;
};

void main() {
	gl_Position = vec4(inpos + c.pos.xy, 0.0, 1.0);
	outcolor = c.color;
}
