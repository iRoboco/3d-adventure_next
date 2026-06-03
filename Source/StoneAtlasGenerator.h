// StoneAtlasGenerator.h
#pragma once
#include "axmol.h"
#include <cstdint>

// Процедурный генератор каменных граней вокселя.
//
// Два режима использования:
//  1) Самостоятельная лента 6×1 (top, bottom, +X, -X, +Z, -Z) по 128×128 —
//     generate()/generateImage(). Удобно для отладки и сохранения в PNG.
//  2) Рендер отдельной грани в произвольную позицию чужого RGBA8888-буфера —
//     renderInto(). Используется TerrainAtlasBuilder, чтобы вписать камень в
//     общий 4×4-атлас террейна (слоты stone-top / stone-side / stone-bottom).
//
// Палитра — светло-серый потёртый камень со сколами, трещинами и ржавыми
// болтами по углам.
class StoneAtlasGenerator
{
public:
    static constexpr int TILE  = 128;
    static constexpr int TILES = 6;
    static constexpr int W     = TILE * TILES;  // 768
    static constexpr int H     = TILE;

    // Стиль грани. Влияет на яркость базы и наличие угловых болтов:
    //  Side   — полный набор (болты + подтёки ржавчины), как «боковая стена».
    //  Top    — реже болты, чуть светлее (верхний срез плиты).
    //  Bottom — темнее, без болтов (нижний срез).
    enum class StoneFace
    {
        Top,
        Side,
        Bottom
    };

    // ---- Самостоятельная лента 6×1 ----
    // Возвращает Texture2D, готовую к использованию (NEAREST для пиксель-арта).
    static ax::Texture2D* generate(uint32_t seed = 1337u);
    // Если нужен только Image (например, чтобы сохранить в PNG) — отдаёт его.
    static ax::Image* generateImage(uint32_t seed = 1337u);

    // ---- Рендер одной грани в чужой буфер ----
    // Пишет каменный тайл размером TILE×TILE в RGBA8888-буфер `rgba`
    // (шириной bufW, высотой bufH пикселей) с левым-верхним углом (ox, oy).
    // Выход за границы буфера обрезается. `seed` задаёт вариацию.
    static void renderInto(uint8_t* rgba, int bufW, int bufH, int ox, int oy, StoneFace face, uint32_t seed);
};
