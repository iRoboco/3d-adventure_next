/**
 * @file VoxelRaycaster.cpp
 * @brief Реализация intent-driven подсветки с MeshRenderer
 */

#include "VoxelRaycaster.h"
#include "ChunkMeshBuilder.h"  // Для ChunkVertex, buildChunkMesh, createMesh

USING_NS_AX;

// ============================================================================
//  Фабрика / Инициализация
// ============================================================================

VoxelRaycasterNode* VoxelRaycasterNode::create(ax::Camera* camera, ChunkManager* mgr, float maxDist)
{
    auto* ret = new (std::nothrow) VoxelRaycasterNode();
    if (ret && ret->init(camera, mgr, maxDist))
    {
        ret->autorelease();
        return ret;
    }
    AX_SAFE_DELETE(ret);
    return nullptr;
}

bool VoxelRaycasterNode::init(ax::Camera* camera, ChunkManager* mgr, float maxDist)
{
    if (!Node::init())
        return false;
    if (!camera || !mgr)
        return false;

    _camera  = camera;
    _mgr     = mgr;
    _maxDist = maxDist;

    // Получаем текстуру атласа из ChunkManager или создаём fallback
    // Примечание: ChunkManager не экспонирует _terrainAtlas, поэтому
    // загружаем самостоятельно или используем белую текстуру
    _terrainAtlas = ax::Director::getInstance()->getTextureCache()->addImage("textures/terrain_atlas.png");
    if (!_terrainAtlas)
    {
        // Fallback: создаём 1x1 белую текстуру для tint-only рендера
        auto* image           = new ax::Image();
        unsigned char white[] = {255, 255, 255, 255};
        image->initWithRawData(white, sizeof(white), 1, 1, 8);
        _terrainAtlas = ax::Director::getInstance()->getTextureCache()->addImage(image, "white_fallback");
        image->release();
    }

    scheduleUpdateWithPriority(1);

    // ============================================================================
    //  ЕДИНОВРЕМЕННАЯ ИНИЦИАЛИЗАЦИЯ РЕНДЕРЕРОВ (Оптимизация производительности)
    //  Вместо пересоздания MeshRenderer при каждом изменении хита, создаём их
    //  один раз в init(). Это снижает нагрузку на граф сцены и менеджер памяти.
    // ============================================================================
    auto createCachedMaterial = [this]() -> ax::Material* {
        auto* mat = ax::MeshMaterial::createBuiltInMaterial(ax::MeshMaterial::MaterialType::UNLIT, false);
        if (mat)
        {
            ax::BlendFunc blend{};
            blend.src = ax::backend::BlendFactor::SRC_ALPHA;
            blend.dst = ax::backend::BlendFactor::ONE_MINUS_SRC_ALPHA;
            mat->getStateBlock().setBlendFunc(blend);
            mat->getStateBlock().setDepthWrite(false);  // Не ломаем Z-буфер
            mat->getStateBlock().setDepthTest(true);

            // [CRITICAL] Сохраняем материал для применения ПОСЛЕ добавления меша.
            // retain() гарантирует, что объект не будет удалён пулом autorelease до onExit().
            mat->retain();
        }
        return mat;
    };

    _highlightMaterial = createCachedMaterial();
    _previewMaterial   = createCachedMaterial();
    _breakMaterial     = createCachedMaterial();

    auto setupCachedRenderer = [&](DynamicMeshRenderer*& renderer) {
        auto* dynRenderer = new (std::nothrow) DynamicMeshRenderer();
        if (dynRenderer && dynRenderer->init())
        {
            dynRenderer->autorelease();
            renderer = dynRenderer;
            renderer->setCameraMask(static_cast<unsigned short>(ax::CameraFlag::USER1));
            renderer->setVisible(false);
            this->addChild(renderer);  // Добавляем в граф, но НЕ вызываем setMaterial() здесь!
        }
        else
        {
            AX_SAFE_DELETE(dynRenderer);
        }
    };

    setupCachedRenderer(_faceHighlightRenderer);
    setupCachedRenderer(_placePreviewRenderer);
    setupCachedRenderer(_breakProgressRenderer);
    _renderersInitialized = true;

    return true;
}

void VoxelRaycasterNode::setInteractionMode(BlockInteractionMode mode)
{
    if (_mode == mode)
        return;
    _mode = mode;

    if (_mode != BlockInteractionMode::PlaceIntent)
        hidePlacePreview();
    if (_mode != BlockInteractionMode::BreakHold)
    {
        hideBreakProgress();
        _breakProgress = 0.0f;  // [FIX #4] Явный сброс прогресса при смене режима
    }
}

// ============================================================================
//  update — главный цикл с ветвлением по режиму
// ============================================================================

void VoxelRaycasterNode::update(float /*dt*/)
{
    if (!_camera || !_mgr)
        return;

    Vec3 eye;
    _camera->getNodeToWorldTransform().getTranslation(&eye);
    Vec3 forward = cameraForward(_camera);

    _lastHit = VoxelRay::castWorld(eye, forward, _maxDist, _mgr);

    if (!_lastHit.has_value() || _lastHit->t == 0.0f)
    {
        hideAll();
        if (_onMiss)
            _onMiss();
        // [FIX #6] Сбрасываем кэш при промахе
        _faceHighlightDirty = true;
        return;
    }

    const VoxelHit& hit = *_lastHit;

    // Highlight грани — всегда при хите
    updateFaceHighlight(hit);

    // Остальной фидбек — по режиму
    switch (_mode)
    {
    case BlockInteractionMode::Look:
        hidePlacePreview();
        hideBreakProgress();
        break;

    case BlockInteractionMode::PlaceIntent:
        updatePlacePreview(hit);
        hideBreakProgress();
        break;

    case BlockInteractionMode::BreakHold:
        hidePlacePreview();
        updateBreakProgress(hit);
        break;
    }

    if (_onHit)
        _onHit(hit);
}

// ============================================================================
//  Меш-билдинг: highlight грани (тонкий блок со смещением)
// ============================================================================

ax::Mesh* VoxelRaycasterNode::createFaceHighlightMesh(const VoxelHit& hit)
{
    if (hit.normal == Vec3::ZERO)
        return nullptr;

    Vec3 offset(static_cast<float>(hit.bx) + hit.normal.x * 0.002f, static_cast<float>(hit.by) + hit.normal.y * 0.002f,
                static_cast<float>(hit.bz) + hit.normal.z * 0.002f);

    std::vector<ChunkVertex> verts;
    ax::IndexArray inds;

    auto addFace = [&](const ChunkVertex& v0, const ChunkVertex& v1, const ChunkVertex& v2, const ChunkVertex& v3) {
        uint16_t baseIdx = static_cast<uint16_t>(verts.size());
        verts.insert(verts.end(), {v0, v1, v2, v3});
        inds.emplace_back<uint16_t>(baseIdx + 0);
        inds.emplace_back<uint16_t>(baseIdx + 1);
        inds.emplace_back<uint16_t>(baseIdx + 2);
        inds.emplace_back<uint16_t>(baseIdx + 0);
        inds.emplace_back<uint16_t>(baseIdx + 2);
        inds.emplace_back<uint16_t>(baseIdx + 3);
    };

    int face            = hit.normal.y > 0.5f    ? 2
                          : hit.normal.y < -0.5f ? 3
                          : hit.normal.x > 0.5f  ? 0
                          : hit.normal.x < -0.5f ? 1
                          : hit.normal.z > 0.5f  ? 4
                                                 : 5;
    auto [u, v, u2, v2] = calculateTileUV(getBlockTileIndex(hit.blockId, face));

    float x0 = offset.x, x1 = offset.x + 1.0f;
    float y0 = offset.y, y1 = offset.y + 1.0f;
    float z0 = offset.z, z1 = offset.z + 1.0f;

    const float e = 0.001f;
    if (hit.normal.x > 0.5f)
    {  // +X
        addFace({x1 + e, y0 - e, z0 - e, u, v}, {x1 + e, y1 + e, z0 - e, u, v2}, {x1 + e, y1 + e, z1 + e, u2, v2},
                {x1 + e, y0 - e, z1 + e, u2, v});
    }
    else if (hit.normal.x < -0.5f)
    {  // -X
        addFace({x0 - e, y0 - e, z1 + e, u2, v}, {x0 - e, y1 + e, z1 + e, u2, v2}, {x0 - e, y1 + e, z0 - e, u, v2},
                {x0 - e, y0 - e, z0 - e, u, v});
    }
    else if (hit.normal.y > 0.5f)
    {  // +Y
        addFace({x0 - e, y1 + e, z1 + e, u, v2}, {x1 + e, y1 + e, z1 + e, u2, v2}, {x1 + e, y1 + e, z0 - e, u2, v},
                {x0 - e, y1 + e, z0 - e, u, v});
    }
    else if (hit.normal.y < -0.5f)
    {  // -Y
        addFace({x0 - e, y0 - e, z0 - e, u, v2}, {x1 + e, y0 - e, z0 - e, u2, v2}, {x1 + e, y0 - e, z1 + e, u2, v},
                {x0 - e, y0 - e, z1 + e, u, v});
    }
    else if (hit.normal.z > 0.5f)
    {  // +Z
        addFace({x0 - e, y0 - e, z1 + e, u, v}, {x1 + e, y0 - e, z1 + e, u2, v}, {x1 + e, y1 + e, z1 + e, u2, v2},
                {x0 - e, y1 + e, z1 + e, u, v2});
    }
    else
    {  // -Z
        addFace({x1 + e, y0 - e, z0 - e, u2, v}, {x0 - e, y0 - e, z0 - e, u, v}, {x0 - e, y1 + e, z0 - e, u, v2},
                {x1 + e, y1 + e, z0 - e, u2, v2});
    }

    return createMesh(verts, inds, _terrainAtlas);
}

// ============================================================================
//  Меш-билдинг: preview блока для установки
// ============================================================================

ax::Mesh* VoxelRaycasterNode::createPlacePreviewMesh(const VoxelHit& hit)
{
    Vec3 offset(static_cast<float>(hit.adjX()), static_cast<float>(hit.adjY()), static_cast<float>(hit.adjZ()));

    std::vector<ChunkVertex> verts;
    ax::IndexArray inds;

    auto addFace = [&](const ChunkVertex& v0, const ChunkVertex& v1, const ChunkVertex& v2, const ChunkVertex& v3) {
        uint16_t baseIdx = static_cast<uint16_t>(verts.size());
        verts.insert(verts.end(), {v0, v1, v2, v3});
        inds.emplace_back<uint16_t>(baseIdx + 0);
        inds.emplace_back<uint16_t>(baseIdx + 1);
        inds.emplace_back<uint16_t>(baseIdx + 2);
        inds.emplace_back<uint16_t>(baseIdx + 0);
        inds.emplace_back<uint16_t>(baseIdx + 2);
        inds.emplace_back<uint16_t>(baseIdx + 3);
    };

    int face            = hit.normal.y > 0.5f    ? 2
                          : hit.normal.y < -0.5f ? 3
                          : hit.normal.x > 0.5f  ? 0
                          : hit.normal.x < -0.5f ? 1
                          : hit.normal.z > 0.5f  ? 4
                                                 : 5;
    auto [u, v, u2, v2] = calculateTileUV(getBlockTileIndex(hit.blockId, face));

    float x0 = offset.x, x1 = offset.x + 1.0f;
    float y0 = offset.y, y1 = offset.y + 1.0f;
    float z0 = offset.z, z1 = offset.z + 1.0f;

    addFace({x1, y0, z0, u, v}, {x1, y1, z0, u, v2}, {x1, y1, z1, u2, v2}, {x1, y0, z1, u2, v});
    addFace({x0, y0, z1, u2, v}, {x0, y1, z1, u2, v2}, {x0, y1, z0, u, v2}, {x0, y0, z0, u, v});
    addFace({x0, y1, z1, u, v2}, {x1, y1, z1, u2, v2}, {x1, y1, z0, u2, v}, {x0, y1, z0, u, v});
    addFace({x0, y0, z0, u, v2}, {x1, y0, z0, u2, v2}, {x1, y0, z1, u2, v}, {x0, y0, z1, u, v});
    addFace({x0, y0, z1, u, v}, {x1, y0, z1, u2, v}, {x1, y1, z1, u2, v2}, {x0, y1, z1, u, v2});
    addFace({x1, y0, z0, u2, v}, {x0, y0, z0, u, v}, {x0, y1, z0, u, v2}, {x1, y1, z0, u2, v2});

    return createMesh(verts, inds, _terrainAtlas);
}

// ============================================================================
//  Меш-билдинг: прогресс разрушения (заполняющаяся грань)
// ============================================================================

ax::Mesh* VoxelRaycasterNode::createBreakProgressMesh(const VoxelHit& hit, float progress)
{
    if (progress <= 0.0f)
        return nullptr;

    Vec3 center(static_cast<float>(hit.bx) + 0.5f + hit.normal.x * 0.003f,
                static_cast<float>(hit.by) + 0.5f + hit.normal.y * 0.003f,
                static_cast<float>(hit.bz) + 0.5f + hit.normal.z * 0.003f);

    float s = progress;

    std::vector<ChunkVertex> verts;
    ax::IndexArray inds;

    int face            = hit.normal.y > 0.5f    ? 2
                          : hit.normal.y < -0.5f ? 3
                          : hit.normal.x > 0.5f  ? 0
                          : hit.normal.x < -0.5f ? 1
                          : hit.normal.z > 0.5f  ? 4
                                                 : 5;
    auto [u, v, u2, v2] = calculateTileUV(getBlockTileIndex(hit.blockId, face));

    Vec3 right, up;
    if (std::abs(hit.normal.x) > 0.5f)
    {
        right = Vec3(0, 0, 1);
        up    = Vec3(0, 1, 0);
    }
    else if (std::abs(hit.normal.y) > 0.5f)
    {
        right = Vec3(1, 0, 0);
        up    = Vec3(0, 0, 1);
    }
    else
    {
        right = Vec3(1, 0, 0);
        up    = Vec3(0, 1, 0);
    }

    Vec3 p0 = center - right * (0.5f * s) - up * (0.5f * s);
    Vec3 p1 = center + right * (0.5f * s) - up * (0.5f * s);
    Vec3 p2 = center + right * (0.5f * s) + up * (0.5f * s);
    Vec3 p3 = center - right * (0.5f * s) + up * (0.5f * s);

    p0 += hit.normal * 0.001f;
    p1 += hit.normal * 0.001f;
    p2 += hit.normal * 0.001f;
    p3 += hit.normal * 0.001f;

    verts.push_back({p0.x, p0.y, p0.z, u, v});
    verts.push_back({p1.x, p1.y, p1.z, u2, v});
    verts.push_back({p2.x, p2.y, p2.z, u2, v2});
    verts.push_back({p3.x, p3.y, p3.z, u, v2});

    inds.emplace_back<uint16_t>(0);
    inds.emplace_back<uint16_t>(1);
    inds.emplace_back<uint16_t>(2);
    inds.emplace_back<uint16_t>(0);
    inds.emplace_back<uint16_t>(2);
    inds.emplace_back<uint16_t>(3);

    return createMesh(verts, inds, _terrainAtlas);
}

// ============================================================================
//  Обновление рендереров
// ============================================================================

void VoxelRaycasterNode::updateFaceHighlight(const VoxelHit& hit)
{
    // [OPT] Проверка кэша: если координаты блока и нормаль грани совпадают с прошлым кадром,
    // геометрия не изменилась → пропускаем аллокацию меша и обновление рендерера.
    bool sameBlock = _lastFaceHighlightHit.bx == hit.bx && _lastFaceHighlightHit.by == hit.by &&
                     _lastFaceHighlightHit.bz == hit.bz && _lastFaceHighlightHit.normal == hit.normal;
    if (sameBlock)
        return;

    // Генерируем новую геометрию только при смене целевой грани
    if (auto* mesh = createFaceHighlightMesh(hit))
    {
        // [API] MeshRenderer::clearMeshes() очищает вектор привязанных мешей без удаления самой ноды.
        // Это безопаснее, чем removeFromParent(), и не вызывает пересчёт графа сцены.
        _faceHighlightRenderer->clearMeshes();
        _faceHighlightRenderer->addMesh(mesh);

        // [FIX CRASH] Применяем кэшированный материал ПОСЛЕ добавления меша.
        // Если вызвать setMaterial() до addMesh(), массив _meshes пуст и вызов будет no-op.
        if (_highlightMaterial)
            _faceHighlightRenderer->setMaterial(_highlightMaterial);

        _faceHighlightRenderer->setVisible(true);  // Показываем ноду

        // [FIX] Axmol::Node::setColor() принимает только RGB (Color3B).
        // Alpha-канал управляется отдельно через setOpacity().
        _faceHighlightRenderer->setColor(ax::Color3B(static_cast<uint8_t>(_faceColor.r * 255),
                                                     static_cast<uint8_t>(_faceColor.g * 255),
                                                     static_cast<uint8_t>(_faceColor.b * 255)));
        _faceHighlightRenderer->setOpacity(static_cast<uint8_t>(_faceColor.a * 255));

        _lastFaceHighlightHit = hit;  // Сохраняем состояние для сравнения в следующем кадре
    }
}

void VoxelRaycasterNode::updatePlacePreview(const VoxelHit& hit)
{
    // [FIX #1] Если превью отключено флагом, просто скрываем кэшированную ноду
    if (!_placePreviewVisible)
    {
        _placePreviewRenderer->setVisible(false);
        return;
    }

    if (auto* mesh = createPlacePreviewMesh(hit))
    {
        _placePreviewRenderer->clearMeshes();  // Безопасная очистка
        _placePreviewRenderer->addMesh(mesh);
        if (_previewMaterial)
            _placePreviewRenderer->setMaterial(_previewMaterial);
        _placePreviewRenderer->setVisible(true);

        _placePreviewRenderer->setColor(ax::Color3B(static_cast<uint8_t>(_previewColor.r * 255),
                                                    static_cast<uint8_t>(_previewColor.g * 255),
                                                    static_cast<uint8_t>(_previewColor.b * 255)));
        _placePreviewRenderer->setOpacity(static_cast<uint8_t>(_previewColor.a * 255));
    }
    else
    {
        // Если меш не создан (например, hit стал валидным, но геометрия нулевая), скрываем ноду
        _placePreviewRenderer->setVisible(false);
    }
}

void VoxelRaycasterNode::updateBreakProgress(const VoxelHit& hit)
{
    // Если прогресс сброшен или равен нулю, скрываем индикатор и выходим
    if (_breakProgress <= 0.0f)
    {
        _breakProgressRenderer->setVisible(false);
        return;
    }

    if (auto* mesh = createBreakProgressMesh(hit, _breakProgress))
    {
        _breakProgressRenderer->clearMeshes();  // Безопасная очистка
        _breakProgressRenderer->addMesh(mesh);
        if (_breakMaterial)
            _breakProgressRenderer->setMaterial(_breakMaterial);
        _breakProgressRenderer->setVisible(true);

        _breakProgressRenderer->setColor(ax::Color3B(static_cast<uint8_t>(_breakColor.r * 255),
                                                     static_cast<uint8_t>(_breakColor.g * 255),
                                                     static_cast<uint8_t>(_breakColor.b * 255)));
        _breakProgressRenderer->setOpacity(static_cast<uint8_t>(_breakColor.a * 255));
    }
    else
    {
        _breakProgressRenderer->setVisible(false);
    }
}

// ============================================================================
//  Скрытие рендереров
// ============================================================================
//
// [OPT] Раньше эти методы вызывали removeFromParentAndCleanup(true),
// что приводило к полному уничтожению ноды и аллокации новой в следующем кадре.
// Теперь мы просто меняем флаг видимости кэшированной ноды.
void VoxelRaycasterNode::hideFaceHighlight()
{
    if (_faceHighlightRenderer)
        _faceHighlightRenderer->setVisible(false);
}

void VoxelRaycasterNode::hidePlacePreview()
{
    if (_placePreviewRenderer)
        _placePreviewRenderer->setVisible(false);
}

void VoxelRaycasterNode::hideBreakProgress()
{
    if (_breakProgressRenderer)
        _breakProgressRenderer->setVisible(false);
}

void VoxelRaycasterNode::hideAll()
{
    hideFaceHighlight();
    hidePlacePreview();
    hideBreakProgress();
}

// ============================================================================
//  Действия
// ============================================================================

bool VoxelRaycasterNode::breakBlock()
{
    if (!_lastHit.has_value() || !_mgr)
        return false;
    const VoxelHit& h = *_lastHit;
    if (h.t == 0.0f)
        return false;

    bool ok = _mgr->setBlockAtWorldPos(
        Vec3{static_cast<float>(h.bx) + 0.5f, static_cast<float>(h.by) + 0.5f, static_cast<float>(h.bz) + 0.5f},
        BLOCK_AIR);

    if (ok)
    {
        AXLOG("[VoxelRaycaster] breakBlock at (%d, %d, %d) id=%u — OK", h.bx, h.by, h.bz,
              static_cast<unsigned>(h.blockId));
        _breakProgress = 0.0f;
    }
    return ok;
}

bool VoxelRaycasterNode::placeBlock(BlockId id)
{
    if (!_lastHit.has_value() || !_mgr)
        return false;
    if (id == BLOCK_AIR)
        return false;
    const VoxelHit& h = *_lastHit;
    if (h.t == 0.0f)
        return false;

    // [FIX #8] Проверка коллизии с капсулой игрока перед установкой блока
    if (_playerCapsule)
    {
        Vec3 bMin(h.adjX(), h.adjY(), h.adjZ());
        Vec3 bMax = bMin + Vec3(1, 1, 1);
        Vec3 cMin = _playerCapsule->bottomPos - Vec3(_playerCapsule->radius, 0, _playerCapsule->radius);
        Vec3 cMax =
            _playerCapsule->bottomPos + Vec3(_playerCapsule->radius, _playerCapsule->height, _playerCapsule->radius);
        if (bMin.x < cMax.x && bMax.x > cMin.x && bMin.y < cMax.y && bMax.y > cMin.y && bMin.z < cMax.z &&
            bMax.z > cMin.z)
            return false;  // Блок пересекается с игроком
    }

    bool ok = _mgr->setBlockAtWorldPos(h.adjacentCenter(), id);

    if (ok)
    {
        AXLOG("[VoxelRaycaster] placeBlock id=%u at adjacent (%d, %d, %d) — OK", static_cast<unsigned>(id), h.adjX(),
              h.adjY(), h.adjZ());
    }
    return ok;
}

// ============================================================================
//  Утилиты
// ============================================================================

// [FIX #7] Унификация вычисления forward-вектора с учетом pitch как в FPC
Vec3 VoxelRaycasterNode::cameraForward(const ax::Camera* cam)
{
    if (!cam)
        return Vec3(0, 0, -1);
    float yaw   = AX_DEGREES_TO_RADIANS(cam->getRotation3D().y);
    float pitch = AX_DEGREES_TO_RADIANS(cam->getRotation3D().x);
    Vec3 fwd(-std::sinf(yaw), std::sinf(pitch), -std::cosf(yaw));
    fwd.x *= std::cosf(pitch);
    fwd.z *= std::cosf(pitch);
    fwd.normalize();
    return fwd;
}

VoxelRaycasterNode::~VoxelRaycasterNode()
{
    // [MEMORY] Axmol использует ручной подсчёт ссылок.
    // Кэшированные материалы требуют явного release() при уничтожении ноды.
    AX_SAFE_RELEASE_NULL(_highlightMaterial);
    AX_SAFE_RELEASE_NULL(_previewMaterial);
    AX_SAFE_RELEASE_NULL(_breakMaterial);
}
