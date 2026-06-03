#version 310 es
precision highp float;
precision highp int;

// =============================================================================
//  terrain.frag — «antialiased pixel art» выборка из атласа.
// =============================================================================
//  Проблема: NEAREST даёт дрожание/алиасинг вдали, LINEAR — мыло вблизи.
//  Решение: сэмплер ЛИНЕЙНЫЙ (+ мипы), а здесь UV «прищёлкивается» к центру
//  текселя так, что внутри текселя выборка резкая (как пиксель-арт), а на
//  границе размывается ровно на ширину одного экранного пикселя (fwidth).
//  → вблизи чёткие квадраты, вдали — плавно, без мерцания, без анизотропии.
//
//  Требует, чтобы атлас был привязан с фильтром LINEAR_MIPMAP_LINEAR
//  (см. ChunkManager::applyTextureFilter при Config.pixelArtAA).
// =============================================================================

layout(location = TEXCOORD0) in vec2 v_texCoord;

layout(binding = 0) uniform sampler2D u_tex0;  // атлас террейна

layout(std140) uniform fs_ub {
    vec4 u_color;  // авто-биндинг: цвет/opacity нода (дистанционный туман террейна)
};

layout(location = SV_Target0) out vec4 FragColor;

// Сетка атласа: 4×4 (согласовано с calculateTileUV в ChunkMeshBuilder.h).
const float ATLAS_GRID = 4.0;

void main(void)
{
    vec2 texSize = vec2(textureSize(u_tex0, 0));
    vec2 px      = v_texCoord * texSize;  // координаты в текселях

    // --- 1. Antialiased point sampling ---
    //  seam = центр текселя, которому принадлежит фрагмент (floor(px) + 0.5).
    //  Смещение от центра зажато в [-0.5..0.5] и поделено на экранный размер текселя
    //  (fwidth): вблизи тексель крупный → зона перехода узкая (резко), вдали мелкий →
    //  линейная интерполяция сглаживает. ВАЖНО: именно центр (а не граница floor(px+0.5)) —
    //  иначе на крайнем такселе тайла seam «перепрыгивает» в соседний тайл и ломает клемп ниже.
    vec2 seam = floor(px) + 0.5;
    vec2 dudv = fwidth(px);
    vec2 snapped = seam + clamp((px - seam) / max(dudv, vec2(1e-5)), vec2(-0.5), vec2(0.5));

    // --- 2. LOD: какой мип реально берётся (из производных) ---
    //  Зажимаем «чистыми» уровнями (тайл ≥ 1 текселя мипа). Глубже сами тексели
    //  мипа — это усреднение СОСЕДНИХ тайлов (грязь от glGenerateMipmap), туда нельзя.
    vec2  texelsPerTile = texSize / ATLAS_GRID;
    vec2  dpdx          = dFdx(px);
    vec2  dpdy          = dFdy(px);
    float lod           = 0.5 * log2(max(dot(dpdx, dpdx), dot(dpdy, dpdy)));
    float maxLod        = log2(min(texelsPerTile.x, texelsPerTile.y));
    lod                 = clamp(lod, 0.0, maxLod);

    // --- 3. Анти-bleed с учётом LOD ---
    //  КЛЮЧЕВОЕ: на мипе уровня lod тексель крупнее в 2^lod раз, поэтому отступ от
    //  края тайла нужен в 0.5 текселя ТЕКУЩЕГО мипа = 0.5*2^lod текселей mip0. Раньше
    //  я зажимал фиксированные 0.5 текселя mip0 — на грубых мипах это ничто, и
    //  билинейная выборка всё равно блендила соседний тайл (тайл там — считанные
    //  тексели). Отступ зажат половиной тайла: на пределе выборка схлопывается к
    //  центру тайла (его средний цвет — без чужих кромок).
    vec2 inset   = min(vec2(0.5 * exp2(lod)), texelsPerTile * 0.5);
    vec2 cell    = floor(seam / texelsPerTile);
    vec2 tileMin = cell * texelsPerTile + inset;
    vec2 tileMax = (cell + 1.0) * texelsPerTile - inset;
    snapped      = clamp(snapped, tileMin, tileMax);

    vec4 tex = textureLod(u_tex0, snapped / texSize, lod);

    // u_color несёт opacity нода (затухание чанков в туман по дистанции).
    FragColor = tex * u_color;
}
