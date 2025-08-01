#version 450

// x -> -1 (left) 1(right)
// y -> -1 (top)  1(bottom)

layout( push_constant ) uniform push{
    mat4 transform;
}trans;

layout(binding = 0) uniform un{
    mat4 view;
} view;

layout(location = 0) in vec2 in_position;
layout(location = 1) in vec3 in_color;

layout(location = 0) out vec3 frag_color;

void main()
{
    gl_Position = trans.transform * vec4(in_position, 0.0, 1.0);
    frag_color = in_color;
}