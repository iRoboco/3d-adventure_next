// TerrainAtlasBuilder.h
#pragma once
#include "axmol.h"
#include <cstdint>
#include <string_view>

// Сборка общего террейн-атласа 512×512 (сетка 4×4, тайл 128×128).
//
// Базовые тайлы (трава, земля, вода и т.п.) берутся из существующего
// низкоразрешённого textures/terrain_atlas.png и апскейлятся ×N методом
// NEAREST, сохраняя «крупный» пиксель-арт. Поверх в каменные слоты вписывается
// детальный процедурный камень из StoneAtlasGenerator.
//
// ВАЖНО: исходный атлас устроен ПО СТОЛБЦАМ (каждый столбец 4×4-сетки = один
// материал, повторённый вниз): col0=трава, col1=трава-бок, col2=земля,
// col3=камень. Поэтому каменные грани занимают три ячейки КАМЕННОГО столбца
// (idx 3/7/11) — они и так все «камень», так что даже без процедурной генерации
// куб остаётся каменным (а не получает траву/землю на боках).
//   слот 3 → stone TOP, слот 7 → stone SIDE, слот 11 → stone BOTTOM.
//
// Раскладка слотов согласована с getBlockTileIndex() в ChunkMeshBuilder.h.
class TerrainAtlasBuilder
{
public:
    static constexpr int GRID     = 4;                  // 4×4
    static constexpr int TILE_OUT = 128;               // размер тайла в выходном атласе
    static constexpr int OUT_SIZE = GRID * TILE_OUT;   // 512

    // Слоты каменных граней — три ячейки КАМЕННОГО столбца (col3) сетки 4×4.
    // Все три исходно «камень», поэтому фолбэк без процедурки корректен.
    static constexpr int SLOT_STONE_TOP    = 3;   // col3 row0
    static constexpr int SLOT_STONE_SIDE   = 7;   // col3 row1
    static constexpr int SLOT_STONE_BOTTOM = 11;  // col3 row2

    // Путь к базовому (низкоразрешённому) атласу-источнику.
    static constexpr const char* BASE_ATLAS_PATH = "textures/terrain_atlas.png";

    // Собирает атлас в память. Caller владеет результатом (release()).
    // Возвращает nullptr только при катастрофической ошибке аллокации.
    static ax::Image* build(uint32_t stoneSeed = 2024u);

    // Оффлайн-бейк: собрать и сохранить PNG по абсолютному пути.
    // Удобно вызвать один раз из dev-кода, затем закоммитить как ассет и
    // гонять без рантайм-генерации (proceduralStoneAtlas = false).
    static bool bakeToPng(std::string_view absPath, uint32_t stoneSeed = 2024u);
};
