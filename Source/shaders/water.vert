#version 310 es

// =============================================================================
//  water.vert — анимация волнения поверхности воды (vertex displacement)
// =============================================================================
//  Вершинный шейдер смещает поверхность воды по Y суммой синусоид (sum-of-sines,
//  «Gerstner-lite») и вычисляет АНАЛИТИЧЕСКУЮ нормаль из производных волны.
//  Координаты вершин приходят в ЛОКАЛЬНОМ пространстве чанка (0..16 по X/Z),
//  поэтому фаза волны строится по МИРОВЫМ координатам (u_chunkOrigin + local),
//  чтобы волны были непрерывными на стыках чанков.
//
//  Атрибуты совпадают с мешем из ax::Mesh::create(positions, normals, uvs):
//    POSITION  = a_position (vec3, локальные коорд. чанка)
//    TEXCOORD0 = a_texCoord (vec2, UV атласа)
//  Нормаль из меша (a_normal) не используется — она заглушка (0,1,0).
// =============================================================================

layout(location = POSITION)  in vec4 a_position;
layout(location = TEXCOORD0) in vec2 a_texCoord;

layout(location = TEXCOORD0) out vec2 v_texCoord;
layout(location = TEXCOORD1) out vec3 v_normal;    // мировая нормаль волны
layout(location = TEXCOORD2) out vec3 v_worldPos;  // мировая позиция (для Fresnel/specular)
layout(location = TEXCOORD3) out float v_time;     // время → во фрагментный шейдер (искры)

layout(std140) uniform vs_ub {
    mat4  u_MVPMatrix;    // авто-биндинг движком (Pass::updateMVPUniform)
    vec3  u_chunkOrigin;  // мировые координаты угла чанка (задаётся при создании нода)
    float u_time;         // время в секундах (обновляется каждый кадр)
};

// -----------------------------------------------------------------------------
//  Параметры волн. Малые амплитуды (~7 см суммарно) сохраняют воксельный стиль.
//  Каждая компонента: направление (нормированное), частота, скорость, амплитуда.
// -----------------------------------------------------------------------------
const int   WAVE_COUNT = 3;
const vec2  WAVE_DIR[3] = vec2[3](
    vec2( 1.0,  0.0),
    vec2( 0.6,  0.8),
    vec2(-0.45, 0.89)
);
const float WAVE_FREQ[3]  = float[3](0.90, 1.70, 2.60);
const float WAVE_SPEED[3] = float[3](1.55, 2.10, 2.80);
const float WAVE_AMP[3]   = float[3](0.038, 0.022, 0.013);

// Высота волны и её частные производные dH/dx, dH/dz (для нормали).
// Возвращает vec3(height, dHdx, dHdz).
vec3 waveField(vec2 p, float t)
{
    float h = 0.0, dx = 0.0, dz = 0.0;
    for (int i = 0; i < WAVE_COUNT; ++i)
    {
        vec2  d     = WAVE_DIR[i];
        float w     = WAVE_FREQ[i];
        float phase = dot(d, p) * w + t * WAVE_SPEED[i];
        float s     = sin(phase);
        float c     = cos(phase);
        h  += WAVE_AMP[i] * s;
        // d/dx sin(dot(d,p)*w + ...) = cos(...) * d.x * w
        dx += WAVE_AMP[i] * c * d.x * w;
        dz += WAVE_AMP[i] * c * d.y * w;
    }
    return vec3(h, dx, dz);
}

void main(void)
{
    vec3 localPos = a_position.xyz;
    vec3 worldPos = localPos + u_chunkOrigin;

    vec3 wf = waveField(worldPos.xz, u_time);

    // Смещаем по Y. Верхняя грань и верхний край боковых граней имеют общий XZ,
    // поэтому смещаются одинаково — меш остаётся «водонепроницаемым».
    vec3 displaced = localPos + vec3(0.0, wf.x, 0.0);

    // Нормаль из градиента высоты: N = normalize(-dHdx, 1, -dHdz)
    v_normal   = normalize(vec3(-wf.y, 1.0, -wf.z));
    v_worldPos = worldPos + vec3(0.0, wf.x, 0.0);

    // Передаём время во фрагментный шейдер. ВАЖНО: единственный u_time живёт здесь,
    // в vs_ub. Дублировать его в fs_ub нельзя — рефлексия юниформов движка хранит
    // плоскую карту "имя→инфо", и одинаковое имя из фрагментной стадии затирает
    // вершинную, из-за чего u_time перестаёт доходить до волн (они «замерзают»).
    v_time = u_time;

    // Лёгкий дрейф UV — текстура «течёт» по поверхности
    v_texCoord    = a_texCoord + vec2(u_time * 0.015, u_time * 0.010);
    v_texCoord.y  = 1.0 - v_texCoord.y;

    gl_Position = u_MVPMatrix * vec4(displaced, 1.0);
}
