#version 450

// x -> -1 (left) 1(right)
// y -> -1 (top)  1(bottom)

vec2 positions[3] = vec2[](
    vec2(0.0, -0.5),
    vec2(0.5, 0.5),
    vec2(-0.5, 0.5)
);

vec3 colors[3] = vec3[](
    vec3(1.0, 0.0, 0.0),
    vec3(0.0, 1.0, 0.0),
    vec3(0.0, 0.0, 1.0)
);

layout(location = 0) out vec3 in_color;

void main()
{
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
    in_color = colors[gl_VertexIndex];
}