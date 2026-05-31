#version 310 es
precision highp float;
precision highp int;
layout(location = TEXCOORD0) in vec2 v_texCoord;

layout(binding = 0) uniform sampler2D u_tex0;

layout(std140) uniform fs_ub {
    vec4 u_color;     // авто-биндинг движка: displayedColor * opacity ноды
    float u_offset;   // анимируемое смещение прокрутки [0..1], задаётся из GameScene
};

layout(location = SV_Target0) out vec4 FragColor;

// Размер одного тайла в атласе 4x4. Вода — тайл #4 (u in [0,0.25], v in [0.25,0.5]).
// Прокрутку оборачиваем внутри границ тайла, чтобы не залезать в соседние тайлы атласа.
const float TILE = 0.25;

const float TWO_PI = 6.2831853;

void main(void)
{
    vec2 tileOrigin = floor(v_texCoord / TILE) * TILE;
    vec2 local      = (v_texCoord - tileOrigin) / TILE;

    // Синусоидальная рябь: видимое искажение даже на почти однотонном тайле воды.
    // Фаза крутится от u_offset, амплитуда мала, чтобы fract удержал UV внутри тайла.
    float phase  = u_offset * TWO_PI;
    vec2  ripple = vec2(sin(phase + local.y * 14.0), cos(phase + local.x * 14.0)) * 0.07;

    // Два слоя, текущие в разных направлениях, маскируют шов от fract-обёртки
    // и дают ощущение живой, переливающейся воды.
    vec2 uv1 = tileOrigin + fract(local + vec2(u_offset, u_offset * 0.5) + ripple) * TILE;
    vec2 uv2 = tileOrigin + fract(local + vec2(-u_offset * 0.7, u_offset * 0.3) - ripple) * TILE;

    vec4 c1       = texture(u_tex0, uv1);
    vec4 c2       = texture(u_tex0, uv2);
    vec4 texColor = mix(c1, c2, 0.5);

    // Лёгкое мерцание яркости (бликов) — делает движение заметным независимо от текстуры.
    float shimmer = 0.85 + 0.15 * sin(phase * 2.0 + (local.x + local.y) * 10.0);
    FragColor     = texColor * u_color * vec4(vec3(shimmer), 1.0);
}
