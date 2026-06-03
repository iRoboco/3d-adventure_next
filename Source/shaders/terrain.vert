#version 310 es

// =============================================================================
//  terrain.vert — вершинный шейдер вокселя (террейн).
// =============================================================================
//  Минимальный трансформ: переводит локальные координаты чанка в clip-space и
//  пробрасывает UV атласа без изменений. Вся «магия» — во фрагментном шейдере
//  (antialiased pixel art). Атрибуты совпадают с ax::Mesh::create(pos,norm,uv):
//    POSITION  = a_position (vec3 в vec4, локальные коорд. чанка)
//    TEXCOORD0 = a_texCoord (vec2, UV атласа)
// =============================================================================

layout(location = POSITION)  in vec4 a_position;
layout(location = TEXCOORD0) in vec2 a_texCoord;

layout(location = TEXCOORD0) out vec2 v_texCoord;

layout(std140) uniform vs_ub {
    mat4 u_MVPMatrix;  // авто-биндинг движком (Pass::updateMVPUniform)
};

void main(void)
{
    // Кастомные шейдеры в axmol трактуют V инвертированно относительно встроенного
    // UNLIT-материала (под который свёрстаны UV меша) — поэтому переворачиваем V,
    // иначе бок травы оказывается вверх ногами. То же делает water.vert.
    v_texCoord  = vec2(a_texCoord.x, 1.0 - a_texCoord.y);
    gl_Position = u_MVPMatrix * vec4(a_position.xyz, 1.0);
}
