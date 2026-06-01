#version 310 es
precision highp float;
precision highp int;

layout(location = TEXCOORD0) in vec2 v_texCoord;
layout(location = TEXCOORD1) in float v_alpha;

uniform sampler2D u_tex0;
uniform sampler2D u_noiseTex;

layout(location = SV_Target0) out vec4 FragColor;

float hash(vec2 p) {
    p = fract(p * vec2(123.34, 345.45));
    p += dot(p, p + 34.345);
    return fract(p.x * p.y);
}

void main() {
    vec4 color = texture(u_tex0, v_texCoord);
    float density = texture(u_noiseTex, v_texCoord * 8.0).r;
    float a = v_alpha * density;
    // Screen-door transparency: discard based on screen-space dither
    float d = hash(gl_FragCoord.xy);
    if (d >= a) discard;
    FragColor = vec4(color.rgb, 1.0);
}
