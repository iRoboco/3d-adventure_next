/**
 * @file VoxelRaycaster.h
 * @brief Воксельный рейкастер с подсветкой грани через MeshRenderer
 *
 * Архитектура подсветки (4 варианта от экстра до оптимального):
 *
 * ВАРИАНТ 1 (Экстра): Кастомный шейдер с glow + пост-процессинг
 *   - Требует: custom .vert/.frag, FrameBuffer, blur pass
 *   - Плюсы: неоновая анимация, bloom
 *   - Минусы: +200 строк, сложность поддержки
 *
 * ВАРИАНТ 2 (Средний): MeshRenderer с wireframe + отдельный прозрачный меш
 *   - Требует: 2 MeshRenderer ноды (wire + solid transparent)
 *   - Плюсы: стандартный pipeline, работает из коробки
 *   - Минусы: 2 draw calls вместо 1
 *
 * ВАРИАНТ 3 (Оптимальный): CustomCommand напрямую в Renderer
 *   - Требует: ручная работа с VBO/IBO, PrimitiveType::LINE_STRIP
 *   - Плюсы: 1 draw call, полный контроль
 *   - Минусы: низкоуровневый, хрупкий при изменениях API
 *
 * ВАРИАНТ 4 (Минимальный): Переиспользование ChunkMeshBuilder
 *   - Требует: buildChunkMesh для 1 блока + прозрачный материал
 *   - Плюсы: 0 нового кода мешбилдинга, консистентность с миром
 *   - Минусы: немного оверхеда от полного меша 1 блока
 *
 * Реализован ВАРИАНТ 4 как основной + ВАРИАНТ 2 для wireframe outline.
 */

#pragma once
#include "axmol.h"
#include "ChunkManager.h"
#include <optional>
#include <functional>
#include <cstdint>
#include <cmath>
#include <limits>

// ============================================================================
//  VoxelHit
// ============================================================================

struct VoxelHit
{
    int bx, by, bz;
    ax::Vec3 hitPos;
    ax::Vec3 normal;
    float t;
    BlockId blockId;

    int adjX() const noexcept { return bx + static_cast<int>(normal.x); }
    int adjY() const noexcept { return by + static_cast<int>(normal.y); }
    int adjZ() const noexcept { return bz + static_cast<int>(normal.z); }

    ax::Vec3 adjacentCenter() const noexcept
    {
        return {static_cast<float>(adjX()) + 0.5f, static_cast<float>(adjY()) + 0.5f,
                static_cast<float>(adjZ()) + 0.5f};
    }
};

// ============================================================================
//  namespace VoxelRay — stateless DDA
// ============================================================================

namespace VoxelRay
{

namespace detail
{
inline double safe_inv(double v) noexcept
{
    return (std::abs(v) < 1e-12) ? std::numeric_limits<double>::infinity() : 1.0 / v;
}

inline double tMaxInit(double origin, int step, double inv_dir) noexcept
{
    double boundary = (step > 0) ? std::ceil(origin) : std::floor(origin);
    if (std::abs(boundary - origin) < 1e-9)
        boundary += step;
    return std::abs(boundary - origin) * std::abs(inv_dir);
}
}  // namespace detail

template <typename IsSolidFn>
std::optional<VoxelHit> castRay(const ax::Vec3& origin, const ax::Vec3& dir, float maxDistance, IsSolidFn&& isSolid)
{
    using namespace detail;

    const double ox = origin.x, oy = origin.y, oz = origin.z;
    const double dx = dir.x, dy = dir.y, dz = dir.z;
    const double maxT = static_cast<double>(std::max(0.0f, maxDistance));

    int vx = static_cast<int>(std::floor(ox));
    int vy = static_cast<int>(std::floor(oy));
    int vz = static_cast<int>(std::floor(oz));

    if (isSolid(vx, vy, vz))
    {
        VoxelHit hit;
        hit.bx      = vx;
        hit.by      = vy;
        hit.bz      = vz;
        hit.hitPos  = origin;
        hit.normal  = ax::Vec3::ZERO;
        hit.t       = 0.0f;
        hit.blockId = BLOCK_AIR;
        return hit;
    }

    const int stepX = (dx > 0.0) ? 1 : (dx < 0.0 ? -1 : 0);
    const int stepY = (dy > 0.0) ? 1 : (dy < 0.0 ? -1 : 0);
    const int stepZ = (dz > 0.0) ? 1 : (dz < 0.0 ? -1 : 0);

    const double inv_dx = safe_inv(dx);
    const double inv_dy = safe_inv(dy);
    const double inv_dz = safe_inv(dz);

    const double tDeltaX = (stepX != 0) ? std::abs(inv_dx) : std::numeric_limits<double>::infinity();
    const double tDeltaY = (stepY != 0) ? std::abs(inv_dy) : std::numeric_limits<double>::infinity();
    const double tDeltaZ = (stepZ != 0) ? std::abs(inv_dz) : std::numeric_limits<double>::infinity();

    double tMaxX = (stepX != 0) ? tMaxInit(ox, stepX, inv_dx) : std::numeric_limits<double>::infinity();
    double tMaxY = (stepY != 0) ? tMaxInit(oy, stepY, inv_dy) : std::numeric_limits<double>::infinity();
    double tMaxZ = (stepZ != 0) ? tMaxInit(oz, stepZ, inv_dz) : std::numeric_limits<double>::infinity();

    int lastAxis = -1;

    while (true)
    {
        double t;
        if (tMaxX < tMaxY)
        {
            if (tMaxX < tMaxZ)
            {
                t = tMaxX;
                if (t > maxT)
                    break;
                vx += stepX;
                tMaxX += tDeltaX;
                lastAxis = 0;
            }
            else
            {
                t = tMaxZ;
                if (t > maxT)
                    break;
                vz += stepZ;
                tMaxZ += tDeltaZ;
                lastAxis = 2;
            }
        }
        else
        {
            if (tMaxY < tMaxZ)
            {
                t = tMaxY;
                if (t > maxT)
                    break;
                vy += stepY;
                tMaxY += tDeltaY;
                lastAxis = 1;
            }
            else
            {
                t = tMaxZ;
                if (t > maxT)
                    break;
                vz += stepZ;
                tMaxZ += tDeltaZ;
                lastAxis = 2;
            }
        }

        if (!isSolid(vx, vy, vz))
            continue;

        ax::Vec3 normal = ax::Vec3::ZERO;
        if (lastAxis == 0)
            normal.x = static_cast<float>(-stepX);
        else if (lastAxis == 1)
            normal.y = static_cast<float>(-stepY);
        else
            normal.z = static_cast<float>(-stepZ);

        VoxelHit hit;
        hit.bx = vx;
        hit.by = vy;
        hit.bz = vz;
        hit.hitPos =
            ax::Vec3{static_cast<float>(ox + dx * t), static_cast<float>(oy + dy * t), static_cast<float>(oz + dz * t)};
        hit.normal  = normal;
        hit.t       = static_cast<float>(t);
        hit.blockId = BLOCK_AIR;
        return hit;
    }

    return std::nullopt;
}

inline std::optional<VoxelHit> castWorld(const ax::Vec3& origin,
                                         const ax::Vec3& dir,
                                         float maxDistance,
                                         const ChunkManager* mgr)
{
    if (!mgr)
        return std::nullopt;

    auto result = castRay(origin, dir, maxDistance, [mgr](int x, int y, int z) -> bool {
        return mgr->getBlockAtWorldPos(ax::Vec3{static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f,
                                                static_cast<float>(z) + 0.5f}) != BLOCK_AIR;
    });

    if (result.has_value())
    {
        result->blockId = mgr->getBlockAtWorldPos(ax::Vec3{static_cast<float>(result->bx) + 0.5f,
                                                           static_cast<float>(result->by) + 0.5f,
                                                           static_cast<float>(result->bz) + 0.5f});
    }
    return result;
}

template <typename IsSolidFn>
uint8_t exposedFaceMask(int bx, int by, int bz, IsSolidFn&& isSolidNeighbor)
{
    uint8_t mask = 0;
    if (!isSolidNeighbor(bx + 1, by, bz))
        mask |= (1u << 0);
    if (!isSolidNeighbor(bx - 1, by, bz))
        mask |= (1u << 1);
    if (!isSolidNeighbor(bx, by + 1, bz))
        mask |= (1u << 2);
    if (!isSolidNeighbor(bx, by - 1, bz))
        mask |= (1u << 3);
    if (!isSolidNeighbor(bx, by, bz + 1))
        mask |= (1u << 4);
    if (!isSolidNeighbor(bx, by, bz - 1))
        mask |= (1u << 5);
    return mask;
}

}  // namespace VoxelRay

// ============================================================================
//  VoxelRaycasterNode — подсветка через MeshRenderer (ВАРИАНТ 4 + 2)
// ============================================================================

class VoxelRaycasterNode : public ax::Node
{
public:
    using HitCallback  = std::function<void(const VoxelHit&)>;
    using MissCallback = std::function<void()>;

    static VoxelRaycasterNode* create(ax::Camera* camera, ChunkManager* mgr, float maxDist = 8.0f);

    bool init(ax::Camera* camera, ChunkManager* mgr, float maxDist);

    void setMaxDistance(float d) noexcept { _maxDist = d; }
    float getMaxDistance() const noexcept { return _maxDist; }

    // Цвет подсветки грани (tint меша)
    void setFaceHighlightColor(const ax::Color4F& c) noexcept { _faceColor = c; }

    // Цвет preview блока
    void setPlacePreviewColor(const ax::Color4F& c) noexcept { _previewColor = c; }
    void setPlacePreviewVisible(bool v) noexcept { _previewVisible = v; }

    void setOnHit(HitCallback cb) { _onHit = std::move(cb); }
    void setOnMiss(MissCallback cb) { _onMiss = std::move(cb); }

    const std::optional<VoxelHit>& getLastHit() const noexcept { return _lastHit; }
    bool hasHit() const noexcept { return _lastHit.has_value(); }

    bool breakBlock();
    bool placeBlock(BlockId id);

    void update(float dt) override;

private:
    // Создаёт меш для одного блока (переиспользуем ChunkMeshBuilder логику)
    ax::Mesh* createSingleBlockMesh(BlockId blockId, const ax::Vec3& offset, float scale = 1.0f);

    // Создаёт wireframe меш для грани (ВАРИАНТ 2: линии через _wireframe)
    ax::Mesh* createFaceWireMesh(const VoxelHit& hit);

    // Создаёт полупрозрачный меш грани (ВАРИАНТ 4: прозрачный блок)
    ax::Mesh* createFaceHighlightMesh(const VoxelHit& hit);

    // Создаёт preview меш для установки
    ax::Mesh* createPlacePreviewMesh(const VoxelHit& hit);

    // Обновляет/создаёт MeshRenderer'ы для подсветки
    void rebuildHighlight(const VoxelHit& hit);
    void hideHighlight();

    static ax::Vec3 cameraForward(const ax::Camera* cam);

    ax::Camera* _camera = nullptr;
    ChunkManager* _mgr  = nullptr;
    float _maxDist      = 8.0f;

    std::optional<VoxelHit> _lastHit;

    // MeshRenderer для подсветки (переиспользуем существующий 3D pipeline)
    ax::MeshRenderer* _faceHighlightRenderer = nullptr;  // Полупрозрачная грань
    ax::MeshRenderer* _placePreviewRenderer  = nullptr;  // Preview блока

    bool _previewVisible = true;

    ax::Color4F _faceColor{1.0f, 1.0f, 1.0f, 0.25f};
    ax::Color4F _previewColor{0.2f, 0.9f, 0.2f, 0.2f};

    // Кэш текстуры атласа (берём из ChunkManager или создаём fallback)
    ax::Texture2D* _terrainAtlas = nullptr;

    HitCallback _onHit;
    MissCallback _onMiss;
};
