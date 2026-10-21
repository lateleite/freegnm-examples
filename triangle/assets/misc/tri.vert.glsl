#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) out vec4 outcol;

out gl_PerVertex {
	vec4 gl_Position;
};

void main() {
	float x;
	float y;
	float red;
	
	if (gl_VertexIndex == 0) {
		x = -0.5;
		y = -0.4;
		red = 0.1;
	} else if (gl_VertexIndex == 1) {
		x = 0;
		y = 0.6;
		red = 0.5;
	} else {
		x = 0.5;
		y = -0.4;
		red = 0.9;
	}

	gl_Position = vec4(x, y, 0.0, 1.0);
	outcol = vec4(1.0, red, 0.0, 1.0);
}
