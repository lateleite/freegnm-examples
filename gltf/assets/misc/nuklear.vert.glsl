#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(binding = 0) uniform Constants {
    mat4 mproj;
} c;

layout(location = 0) in vec2 inpos;
layout(location = 1) in vec2 inuv;
layout(location = 2) in vec4 incolor;

layout(location = 0) out vec2 outuv;
layout(location = 1) out vec4 outcolor;

out gl_PerVertex {
	vec4 gl_Position;
};

void main() {
	gl_Position = c.mproj * vec4(inpos, 0.0, 1.0);
	outuv = inuv;
	outcolor = incolor;
}
