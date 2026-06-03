// TerrainAtlasBuilder.cpp
#include "TerrainAtlasBuilder.h"
#include "StoneAtlasGenerator.h"
#include <vector>

using namespace ax;

namespace
{

// Левый-верхний угол слота `slot` в выходном атласе.
inline void slotOrigin(int slot, int& ox, int& oy)
{
    ox = (slot % TerrainAtlasBuilder::GRID) * TerrainAtlasBuilder::TILE_OUT;
    oy = (slot / TerrainAtlasBuilder::GRID) * TerrainAtlasBuilder::TILE_OUT;
}

// Апскейл base→out методом NEAREST. Заполняет весь выходной буфер, сохраняя
// 4×4-раскладку базы. Поддерживает RGBA8 и RGB8; иначе — серая заливка.
void upscaleBase(std::vector<uint8_t>& out, int outW, int outH)
{
    struct RGBA
    {
        uint8_t r, g, b, a;
    };
    const RGBA grey = {120, 120, 124, 255};

    Image base;
    if (!base.initWithImageFile(TerrainAtlasBuilder::BASE_ATLAS_PATH))
    {
        // Базы нет — ровный серый фон, чтобы хотя бы камень был виден.
        for (int i = 0; i < outW * outH; ++i)
        {
            out[i * 4 + 0] = grey.r;
            out[i * 4 + 1] = grey.g;
            out[i * 4 + 2] = grey.b;
            out[i * 4 + 3] = grey.a;
        }
        AXLOGW("TerrainAtlasBuilder: base atlas '%s' not found, using flat grey", TerrainAtlasBuilder::BASE_ATLAS_PATH);
        return;
    }

    const int      bw  = base.getWidth();
    const int      bh  = base.getHeight();
    const uint8_t* src = base.getData();
    const auto     fmt = base.getPixelFormat();

    int bpp = 0;
    if (fmt == backend::PixelFormat::RGBA8)
        bpp = 4;
    else if (fmt == backend::PixelFormat::RGB8)
        bpp = 3;

    if (bpp == 0 || src == nullptr || bw <= 0 || bh <= 0)
    {
        for (int i = 0; i < outW * outH; ++i)
        {
            out[i * 4 + 0] = grey.r;
            out[i * 4 + 1] = grey.g;
            out[i * 4 + 2] = grey.b;
            out[i * 4 + 3] = grey.a;
        }
        AXLOGW("TerrainAtlasBuilder: unsupported base pixel format, using flat grey");
        return;
    }

    for (int y = 0; y < outH; ++y)
    {
        const int sy = y * bh / outH;
        for (int x = 0; x < outW; ++x)
        {
            const int sx = x * bw / outW;
            const int si = (sy * bw + sx) * bpp;
            const int di = (y * outW + x) * 4;
            out[di + 0]  = src[si + 0];
            out[di + 1]  = src[si + 1];
            out[di + 2]  = src[si + 2];
            out[di + 3]  = (bpp == 4) ? src[si + 3] : 255;
        }
    }
}

}  // namespace

Image* TerrainAtlasBuilder::build(uint32_t stoneSeed)
{
    const int            W = OUT_SIZE, H = OUT_SIZE;
    std::vector<uint8_t> buf((size_t)W * H * 4, 0);

    // 1) База: апскейл существующего атласа (трава/земля/вода/прочее).
    upscaleBase(buf, W, H);

    // 2) Камень: вписываем детальные тайлы в свои слоты.
    using SF = StoneAtlasGenerator::StoneFace;
    int ox, oy;
    slotOrigin(SLOT_STONE_TOP, ox, oy);
    StoneAtlasGenerator::renderInto(buf.data(), W, H, ox, oy, SF::Top, stoneSeed);
    slotOrigin(SLOT_STONE_SIDE, ox, oy);
    StoneAtlasGenerator::renderInto(buf.data(), W, H, ox, oy, SF::Side, stoneSeed + 1u);
    slotOrigin(SLOT_STONE_BOTTOM, ox, oy);
    StoneAtlasGenerator::renderInto(buf.data(), W, H, ox, oy, SF::Bottom, stoneSeed + 2u);

    auto* img = new Image();
    img->initWithRawData(buf.data(), (ssize_t)buf.size(), W, H, 8 /*bitsPerComponent*/);
    return img;
}

bool TerrainAtlasBuilder::bakeToPng(std::string_view absPath, uint32_t stoneSeed)
{
    Image* img = build(stoneSeed);
    if (!img)
        return false;
    const bool ok = img->saveToFile(absPath, /*isToRGB*/ false);
    img->release();
    if (!ok)
        AXLOGE("TerrainAtlasBuilder: failed to save baked atlas to '%s'", std::string(absPath).c_str());
    return ok;
}
