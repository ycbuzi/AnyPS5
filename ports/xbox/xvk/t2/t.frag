#version 450
layout(set = 0, binding = 1) uniform U { vec4 color; };
layout(location = 0) out vec4 o;
void main() { o = color; }
