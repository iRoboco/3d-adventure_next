// StoneAtlasGenerator.cpp
#include "StoneAtlasGenerator.h"
#include <algorithm>
#include <cmath>
#include <vector>

using namespace ax;
using StoneFace = StoneAtlasGenerator::StoneFace;

namespace
{

// ---- RNG (xorshift32, детерминированный, без зависимостей) ----
struct Rng
{
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed ? seed : 0x9E3779B9u) {}
    uint32_t u32()
    {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return s;
    }
    float f01() { return (u32() >> 8) * (1.0f / 16777216.0f); }  // [0,1)
    float range(float a, float b) { return a + (b - a) * f01(); }
    int   irange(int a, int b) { return a + (int)(u32() % (uint32_t)(b - a + 1)); }
};

struct Col
{
    uint8_t r, g, b, a;
};

inline uint8_t clampB(int v)
{
    return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
}

// ---- Value noise + fBm для зернистости камня ----
inline float hash2(int x, int y, uint32_t seed)
{
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + seed * 2246822519u;
    h          = (h ^ (h >> 13)) * 1274126177u;
    return ((h ^ (h >> 16)) & 0xFFFFFF) / 16777215.0f;
}
inline float smooth(float t)
{
    return t * t * (3.0f - 2.0f * t);
}
inline float valueNoise(float x, float y, uint32_t seed)
{
    int   xi = (int)std::floor(x), yi = (int)std::floor(y);
    float fx = smooth(x - xi), fy = smooth(y - yi);
    float a = hash2(xi, yi, seed), b = hash2(xi + 1, yi, seed);
    float c = hash2(xi, yi + 1, seed), d = hash2(xi + 1, yi + 1, seed);
    return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy;
}
inline float fbm(float x, float y, uint32_t seed)
{
    float sum = 0.f, amp = 0.5f, freq = 1.f;
    for (int o = 0; o < 4; ++o)
    {
        sum += amp * valueNoise(x * freq, y * freq, seed + o * 101);
        freq *= 2.f;
        amp *= 0.5f;
    }
    return sum;
}

// ---- Палитра камня (тона потёртого светло-серого камня) ----
const Col STONE_BASE  = {150, 150, 153, 255};  // светло-серый
const Col STONE_LIGHT = {188, 188, 190, 255};  // блики
const Col STONE_DARK  = {92, 92, 96, 255};      // тень / швы
const Col CRACK_DARK  = {46, 46, 50, 255};      // глубокие трещины
const Col GROUT       = {70, 70, 74, 255};      // межплиточные швы
const Col RUST_A      = {150, 92, 46, 255};     // ржавчина светлая
const Col RUST_B      = {96, 56, 28, 255};      // ржавчина тёмная
const Col BOLT_LIGHT  = {170, 170, 172, 255};
const Col BOLT_DARK   = {74, 74, 78, 255};

inline Col lerpCol(const Col& a, const Col& b, float t)
{
    return {clampB((int)(a.r + (b.r - a.r) * t)), clampB((int)(a.g + (b.g - a.g) * t)),
            clampB((int)(a.b + (b.b - a.b) * t)), 255};
}

// Невладеющий холст поверх произвольного RGBA8888-буфера.
struct Canvas
{
    uint8_t* px;
    int      w, h;
    Canvas(uint8_t* data, int W, int H) : px(data), w(W), h(H) {}
    inline void set(int x, int y, const Col& c)
    {
        if (x < 0 || y < 0 || x >= w || y >= h)
            return;
        int i     = (y * w + x) * 4;
        px[i]     = c.r;
        px[i + 1] = c.g;
        px[i + 2] = c.b;
        px[i + 3] = c.a;
    }
    inline void blend(int x, int y, const Col& c, float a)
    {
        if (x < 0 || y < 0 || x >= w || y >= h)
            return;
        int i     = (y * w + x) * 4;
        px[i]     = clampB((int)(px[i] * (1 - a) + c.r * a));
        px[i + 1] = clampB((int)(px[i + 1] * (1 - a) + c.g * a));
        px[i + 2] = clampB((int)(px[i + 2] * (1 - a) + c.b * a));
        px[i + 3] = 255;
    }
};

// Рисуем «шов плитки» — тёмные борозды, делящие тайл на блоки разного размера.
void drawGrout(Canvas& cv, int ox, int oy, int T, Rng& rng)
{
    int              vcuts = rng.irange(1, 2), hcuts = rng.irange(1, 2);
    std::vector<int> xs, ys;
    for (int i = 0; i < vcuts; ++i)
        xs.push_back(rng.irange(T / 4, 3 * T / 4));
    for (int i = 0; i < hcuts; ++i)
        ys.push_back(rng.irange(T / 4, 3 * T / 4));
    for (int x : xs)
        for (int y = 0; y < T; ++y)
        {
            cv.blend(ox + x, oy + y, GROUT, 0.85f);
            cv.blend(ox + x + 1, oy + y, STONE_DARK, 0.4f);
            cv.blend(ox + x - 1, oy + y, STONE_LIGHT, 0.25f);
        }
    for (int y : ys)
        for (int x = 0; x < T; ++x)
        {
            cv.blend(ox + x, oy + y, GROUT, 0.85f);
            cv.blend(ox + x, oy + y + 1, STONE_DARK, 0.4f);
            cv.blend(ox + x, oy + y - 1, STONE_LIGHT, 0.25f);
        }
}

// Зазубренная трещина случайным блужданием.
void drawCrack(Canvas& cv, int ox, int oy, int T, Rng& rng)
{
    float x   = rng.range(T * 0.2f, T * 0.8f);
    float y   = rng.range(0, T * 0.3f);
    float ang = rng.range(1.2f, 1.9f);  // в основном вниз
    int   len = rng.irange(T / 2, T);
    for (int i = 0; i < len; ++i)
    {
        ang += rng.range(-0.35f, 0.35f);
        x += std::cos(ang);
        y += std::sin(ang);
        if (x < 1 || x > T - 1 || y < 1 || y > T - 1)
            break;
        cv.blend(ox + (int)x, oy + (int)y, CRACK_DARK, 0.8f);
        cv.blend(ox + (int)x + 1, oy + (int)y, STONE_LIGHT, 0.2f);  // лёгкий блик для объёма
        if (rng.f01() < 0.04f)                                       // редкое ветвление
        {
            float bx = x, by = y, ba = ang + rng.range(-1.f, 1.f);
            int   bl = rng.irange(4, 12);
            for (int j = 0; j < bl; ++j)
            {
                ba += rng.range(-0.4f, 0.4f);
                bx += std::cos(ba);
                by += std::sin(ba);
                cv.blend(ox + (int)bx, oy + (int)by, CRACK_DARK, 0.6f);
            }
        }
    }
}

// Мелкие сколы/щербинки.
void drawChip(Canvas& cv, int ox, int oy, int T, Rng& rng)
{
    int cx = rng.irange(2, T - 2), cy = rng.irange(2, T - 2);
    int r = rng.irange(1, 3);
    for (int dy = -r; dy <= r; ++dy)
        for (int dx = -r; dx <= r; ++dx)
            if (dx * dx + dy * dy <= r * r)
                cv.blend(ox + cx + dx, oy + cy + dy, CRACK_DARK, 0.55f);
    cv.blend(ox + cx, oy + cy - r, STONE_LIGHT, 0.4f);  // нижний край скола светлее (фаска)
}

// Ржавый болт в углу.
void drawBolt(Canvas& cv, int ox, int oy, int /*T*/, int cx, int cy, Rng& rng)
{
    int  r     = rng.irange(5, 7);
    bool rusty = rng.f01() < 0.6f;
    for (int dy = -r - 1; dy <= r + 1; ++dy)
        for (int dx = -r - 1; dx <= r + 1; ++dx)
        {
            float d  = std::sqrt((float)(dx * dx + dy * dy));
            int   px = ox + cx + dx, py = oy + cy + dy;
            if (d <= r)
            {
                float light = 0.5f - (dx + dy) * 0.04f;  // освещение сверху-слева
                Col   body  = lerpCol(BOLT_DARK, BOLT_LIGHT, std::clamp(light, 0.f, 1.f));
                if (rusty)
                {
                    float rt = (fbm(px * 0.3f, py * 0.3f, 77u) - 0.3f);
                    if (rt > 0)
                        body = lerpCol(body, lerpCol(RUST_A, RUST_B, rng.f01()), std::clamp(rt * 1.6f, 0.f, 0.85f));
                }
                cv.set(px, py, body);
            }
            else if (d <= r + 1.2f)
            {
                cv.blend(px, py, CRACK_DARK, 0.5f);  // тёмный ободок
            }
        }
    for (int dx = -r + 2; dx <= r - 2; ++dx)  // прорезь под отвёртку
        cv.blend(ox + cx + dx, oy + cy, BOLT_DARK, 0.7f);
}

// Подтёки ржавчины из угла.
void drawRustStreak(Canvas& cv, int ox, int oy, int T, int cx, int cy, Rng& rng)
{
    float x   = (float)cx, y = (float)cy;
    int   len = rng.irange(T / 4, T / 2);
    float dir = (cy < T / 2) ? 1.f : -1.f;  // течёт от ближнего края
    for (int i = 0; i < len; ++i)
    {
        x += rng.range(-0.6f, 0.6f);
        y += dir * rng.range(0.6f, 1.2f);
        Col c = lerpCol(RUST_A, RUST_B, rng.f01());
        cv.blend(ox + (int)x, oy + (int)y, c, 0.25f * (1.f - (float)i / len));
        if (rng.f01() < 0.3f)
            cv.blend(ox + (int)x + 1, oy + (int)y, c, 0.15f);
    }
}

// Рендер одного каменного тайла стиля `face` в позицию (ox, oy) холста.
void renderTileAt(Canvas& cv, int ox, int oy, int T, StoneFace face, uint32_t seed)
{
    Rng rng(seed);

    // Стиль грани: вероятность болтов и сдвиг яркости базы.
    const float boltChance = (face == StoneFace::Side) ? 0.7f : (face == StoneFace::Top ? 0.3f : 0.0f);
    const float darken     = (face == StoneFace::Bottom) ? 0.22f : 0.0f;
    const float lighten    = (face == StoneFace::Top) ? 0.06f : 0.0f;

    // 1) Базовый каменный фон с fBm-зернистостью.
    for (int y = 0; y < T; ++y)
        for (int x = 0; x < T; ++x)
        {
            float n    = fbm(x * 0.08f, y * 0.08f, seed + 13);
            float fine = valueNoise(x * 0.6f, y * 0.6f, seed + 7) * 0.18f;
            float t    = std::clamp(n * 1.1f + fine - 0.15f, 0.f, 1.f);
            Col   base = (t < 0.5f) ? lerpCol(STONE_DARK, STONE_BASE, t * 2.f)
                                    : lerpCol(STONE_BASE, STONE_LIGHT, (t - 0.5f) * 2.f);
            if (darken > 0.f)
                base = lerpCol(base, STONE_DARK, darken);
            if (lighten > 0.f)
                base = lerpCol(base, STONE_LIGHT, lighten);
            // лёгкая виньетка к краям — границы плиты
            float edge = std::min({(float)x, (float)y, (float)(T - 1 - x), (float)(T - 1 - y)});
            if (edge < 3.f)
                base = lerpCol(base, STONE_DARK, (3.f - edge) / 3.f * 0.6f);
            cv.set(ox + x, oy + y, base);
        }

    // 2) Швы плитки.
    drawGrout(cv, ox, oy, T, rng);

    // 3) Трещины.
    int cracks = rng.irange(1, 3);
    for (int i = 0; i < cracks; ++i)
        drawCrack(cv, ox, oy, T, rng);

    // 4) Сколы.
    int chips = rng.irange(6, 14);
    for (int i = 0; i < chips; ++i)
        drawChip(cv, ox, oy, T, rng);

    // 5) Болты по углам + подтёки (зависит от стиля грани).
    if (boltChance > 0.f)
    {
        const int m = 12;  // отступ от края
        struct P
        {
            int x, y;
        } corners[4] = {{m, m}, {T - m, m}, {m, T - m}, {T - m, T - m}};
        for (auto& c : corners)
        {
            if (rng.f01() < boltChance)
            {
                drawBolt(cv, ox, oy, T, c.x, c.y, rng);
                if (rng.f01() < 0.5f)
                    drawRustStreak(cv, ox, oy, T, c.x, c.y, rng);
            }
        }
    }

    // 6) Финальная зернистая пыль поверх.
    for (int y = 0; y < T; ++y)
        for (int x = 0; x < T; ++x)
        {
            float g = hash2(ox + x, oy + y, seed + 555);
            if (g > 0.93f)
                cv.blend(ox + x, oy + y, STONE_LIGHT, 0.12f);
            else if (g < 0.05f)
                cv.blend(ox + x, oy + y, STONE_DARK, 0.15f);
        }
}

// Соответствие позиции в ленте 6×1 стилю грани.
StoneFace stripFace(int index)
{
    if (index == 0)
        return StoneFace::Top;
    if (index == 1)
        return StoneFace::Bottom;
    return StoneFace::Side;  // 2..5 — боковые
}

}  // namespace

void StoneAtlasGenerator::renderInto(uint8_t* rgba, int bufW, int bufH, int ox, int oy, StoneFace face, uint32_t seed)
{
    Canvas cv(rgba, bufW, bufH);
    renderTileAt(cv, ox, oy, TILE, face, seed);
}

Image* StoneAtlasGenerator::generateImage(uint32_t seed)
{
    std::vector<uint8_t> buf(W * H * 4, 0);
    Canvas               cv(buf.data(), W, H);
    for (int i = 0; i < TILES; ++i)
        renderTileAt(cv, i * TILE, 0, TILE, stripFace(i), seed + i * 99173u);

    auto* img = new Image();
    // initWithRawData копирует данные внутрь Image (RGBA8888).
    img->initWithRawData(buf.data(), (ssize_t)buf.size(), W, H, 8 /*bitsPerComponent*/);
    return img;
}

Texture2D* StoneAtlasGenerator::generate(uint32_t seed)
{
    Image* img = generateImage(seed);
    auto*  tex = new Texture2D();
    tex->initWithImage(img);
    img->release();

    Texture2D::TexParams tp;
    tp.minFilter    = backend::SamplerFilter::NEAREST;
    tp.magFilter    = backend::SamplerFilter::NEAREST;
    tp.sAddressMode = backend::SamplerAddressMode::CLAMP_TO_EDGE;
    tp.tAddressMode = backend::SamplerAddressMode::CLAMP_TO_EDGE;
    tex->setTexParameters(tp);
    return tex;
}
