#version 310 es

layout(location = POSITION)  in vec3 a_position;
layout(location = NORMAL)    in vec3 a_normal;
layout(location = TEXCOORD0) in vec2 a_texCoord;

layout(std140) uniform vs_ub {
    mat4  u_MVPMatrix;
    float u_Layer;
    float u_Length;
    float u_Density;
};

layout(location = TEXCOORD0) out vec2 v_texCoord;
layout(location = TEXCOORD1) out float v_alpha;

void main() {
    vec3 pos = a_position + a_normal * u_Layer * u_Length;
    v_texCoord = a_texCoord;
    v_alpha = 1.0 - u_Layer * u_Density;
    gl_Position = u_MVPMatrix * vec4(pos, 1.0);
}
