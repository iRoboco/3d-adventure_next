/**
 * @file VoxelRaycaster.cpp
 * @brief Реализация с MeshRenderer подсветкой (ВАРИАНТ 4 + 2)
 * 
 * Ключевое изменение: вместо DrawNode (2D API) используем MeshRenderer (3D pipeline).
 * Это гарантирует:
 * - Корректную отрисовку в 3D сцене с CameraFlag::USER1
 * - Правильное z-buffer тестирование (no z-fighting при настройке)
 * - Возможность wireframe через _wireframe = true
 * - Прозрачность через BlendFunc и материал
 */

#include "VoxelRaycaster.h"
#include "ChunkMeshBuilder.h"  // Для ChunkVertex, buildChunkMesh, createMesh

USING_NS_AX;

// ============================================================================
//  Фабрика / Инициализация
// ============================================================================

VoxelRaycasterNode* VoxelRaycasterNode::create(ax::Camera*   camera,
                                               ChunkManager* mgr,
                                               float         maxDist)
{
    auto* ret = new (std::nothrow) VoxelRaycasterNode();
    if (ret && ret->init(camera, mgr, maxDist)) {
        ret->autorelease();
        return ret;
    }
    AX_SAFE_DELETE(ret);
    return nullptr;
}

bool VoxelRaycasterNode::init(ax::Camera* camera, ChunkManager* mgr, float maxDist)
{
    if (!Node::init())   return false;
    if (!camera || !mgr) return false;

    _camera  = camera;
    _mgr     = mgr;
    _maxDist = maxDist;

    // Получаем текстуру атласа из ChunkManager или создаём fallback
    // Примечание: ChunkManager не экспонирует _terrainAtlas, поэтому
    // загружаем самостоятельно или используем белую текстуру
    _terrainAtlas = ax::Director::getInstance()->getTextureCache()->addImage("textures/terrain_atlas.png");
    if (!_terrainAtlas) {
        // Fallback: создаём 1x1 белую текстуру для tint-only рендера
        auto* image = new ax::Image();
        unsigned char white[] = {255, 255, 255, 255};
        image->initWithRawData(white, sizeof(white), 1, 1, 8);
        _terrainAtlas = ax::Director::getInstance()->getTextureCache()->addImage(image, "white_fallback");
        image->release();
    }

    scheduleUpdate();
    return true;
}

// ============================================================================
//  update
// ============================================================================

void VoxelRaycasterNode::update(float /*dt*/)
{
    if (!_camera || !_mgr) return;

    Vec3 eye;
    _camera->getNodeToWorldTransform().getTranslation(&eye);
    Vec3 forward = cameraForward(_camera);

    _lastHit = VoxelRay::castWorld(eye, forward, _maxDist, _mgr);

    if (_lastHit.has_value()) {
        rebuildHighlight(*_lastHit);
    } else {
        hideHighlight();
    }

    if (_lastHit.has_value()) {
        if (_onHit) _onHit(*_lastHit);
    } else {
        if (_onMiss) _onMiss();
    }
}

// ============================================================================
//  createSingleBlockMesh — ядро ВАРИАНТА 4
// ============================================================================
//
// Переиспользуем логику ChunkMeshBuilder для создания меша ОДНОГО блока.
// Это консистентно с миром — те же UV, те же нормали, тот же pipeline.

ax::Mesh* VoxelRaycasterNode::createSingleBlockMesh(BlockId blockId, const ax::Vec3& offset, float scale)
{
    // Создаём фейковый ChunkData с одним блоком
    // Для этого используем прямой подход: генерируем 6 граней вручную
    // (упрощённо, без face culling — нам нужны ВСЕ грани для highlight)
    
    std::vector<ChunkVertex> verts;
    ax::IndexArray inds;
    
    auto uv = calculateBlockUV(blockId);
    float tileU = uv[0], tileV = uv[1], tileU2 = uv[2], tileV2 = uv[3];
    
    float x0 = offset.x, x1 = offset.x + scale;
    float y0 = offset.y, y1 = offset.y + scale;
    float z0 = offset.z, z1 = offset.z + scale;
    
    // Лямбда для добавления грани (4 вершины + 6 индексов)
    auto addFace = [&](const ChunkVertex& v0, const ChunkVertex& v1,
                       const ChunkVertex& v2, const ChunkVertex& v3)
    {
        uint16_t baseIdx = static_cast<uint16_t>(verts.size());
        verts.insert(verts.end(), {v0, v1, v2, v3});
        inds.emplace_back<uint16_t>(baseIdx + 0);
        inds.emplace_back<uint16_t>(baseIdx + 1);
        inds.emplace_back<uint16_t>(baseIdx + 2);
        inds.emplace_back<uint16_t>(baseIdx + 0);
        inds.emplace_back<uint16_t>(baseIdx + 2);
        inds.emplace_back<uint16_t>(baseIdx + 3);
    };
    
    // +X грань
    addFace({x1, y0, z0, tileU, tileV},  {x1, y1, z0, tileU, tileV2},
            {x1, y1, z1, tileU2, tileV2},{x1, y0, z1, tileU2, tileV});
    // -X грань
    addFace({x0, y0, z1, tileU2, tileV}, {x0, y1, z1, tileU2, tileV2},
            {x0, y1, z0, tileU, tileV2}, {x0, y0, z0, tileU, tileV});
    // +Y грань
    addFace({x0, y1, z1, tileU, tileV2}, {x1, y1, z1, tileU2, tileV2},
            {x1, y1, z0, tileU2, tileV}, {x0, y1, z0, tileU, tileV});
    // -Y грань
    addFace({x0, y0, z0, tileU, tileV2}, {x1, y0, z0, tileU2, tileV2},
            {x1, y0, z1, tileU2, tileV}, {x0, y0, z1, tileU, tileV});
    // +Z грань
    addFace({x0, y0, z1, tileU, tileV},  {x1, y0, z1, tileU2, tileV},
            {x1, y1, z1, tileU2, tileV2},{x0, y1, z1, tileU, tileV2});
    // -Z грань
    addFace({x1, y0, z0, tileU2, tileV}, {x0, y0, z0, tileU, tileV},
            {x0, y1, z0, tileU, tileV2}, {x1, y1, z0, tileU2, tileV2});
    
    return createMesh(verts, inds, _terrainAtlas);
}

// ============================================================================
//  createFaceHighlightMesh — ВАРИАНТ 4: прозрачная грань как "блок"
// ============================================================================
//
// Вместо квада рисуем ТОНКИЙ БЛОК (scale = 1.0f, но смещённый на 0.001 от грани).
// Это даёт корректное поведение z-buffer и освещение.

ax::Mesh* VoxelRaycasterNode::createFaceHighlightMesh(const VoxelHit& hit)
{
    if (hit.normal == Vec3::ZERO) return nullptr;
    
    // Позиция "блока" = позиция целевого блока + нормаль * epsilon
    // Но мы рисуем ПОЛНЫЙ блок, просто смещённый — это проще чем квад
    Vec3 offset(
        static_cast<float>(hit.bx) + hit.normal.x * 0.001f,
        static_cast<float>(hit.by) + hit.normal.y * 0.001f,
        static_cast<float>(hit.bz) + hit.normal.z * 0.001f
    );
    
    // Масштаб чуть больше 1 чтобы перекрыть грань целевого блока
    float scale = 1.002f;
    
    return createSingleBlockMesh(BLOCK_STONE, offset, scale); // BLOCK_STONE = любая текстура, будет tinted
}

// ============================================================================
//  createPlacePreviewMesh — ВАРИАНТ 4: preview блока для установки
// ============================================================================

ax::Mesh* VoxelRaycasterNode::createPlacePreviewMesh(const VoxelHit& hit)
{
    if (hit.normal == Vec3::ZERO) return nullptr;
    
    Vec3 offset(
        static_cast<float>(hit.adjX()),
        static_cast<float>(hit.adjY()),
        static_cast<float>(hit.adjZ())
    );
    
    return createSingleBlockMesh(BLOCK_STONE, offset, 1.0f);
}

// ============================================================================
//  createFaceWireMesh — ВАРИАНТ 2: wireframe через MeshRenderer::_wireframe
// ============================================================================
//
// Создаём тот же меш, но будем рендерить с _wireframe = true.
// Это даёт линейный контур вокруг грани.

ax::Mesh* VoxelRaycasterNode::createFaceWireMesh(const VoxelHit& hit)
{
    // Для wireframe используем тот же меш — рендер в линейном режиме
    // Axmol поддерживает wireframe через MeshRenderer::setWireframe(true)
    return createFaceHighlightMesh(hit);
}

// ============================================================================
//  rebuildHighlight — сборка MeshRenderer'ов
// ============================================================================

void VoxelRaycasterNode::rebuildHighlight(const VoxelHit& hit)
{
    // Удаляем старые renderer'ы
    hideHighlight();
    
    // === Подсветка грани (ВАРИАНТ 4: прозрачный tinted блок) ===
    if (auto* mesh = createFaceHighlightMesh(hit)) {
        _faceHighlightRenderer = MeshRenderer::create();
        _faceHighlightRenderer->addMesh(mesh);
        _faceHighlightRenderer->setCameraMask(static_cast<unsigned short>(CameraFlag::USER1));
        
        // Материал: UNLIT с прозрачностью + tint цветом
        auto* material = MeshMaterial::createBuiltInMaterial(
            MeshMaterial::MaterialType::UNLIT, false);
        if (material) {
            // Настройка прозрачности через BlendFunc
            BlendFunc blend;
            blend.src = backend::BlendFactor::SRC_ALPHA;
            blend.dst = backend::BlendFactor::ONE_MINUS_SRC_ALPHA;
            material->setStateBlock(material->getStateBlock());
            material->getStateBlock().setBlendFunc(blend);
            material->getStateBlock().setDepthWrite(false); // Прозрачные объекты не пишут в depth
            
            // Применяем tint цветом через setColor (если поддерживается)
            // Или через uniform u_color в шейдере
            _faceHighlightRenderer->setColor(Color4B(
                static_cast<uint8_t>(_faceColor.r * 255),
                static_cast<uint8_t>(_faceColor.g * 255),
                static_cast<uint8_t>(_faceColor.b * 255),
                static_cast<uint8_t>(_faceColor.a * 255)
            ));
        }
        _faceHighlightRenderer->setMaterial(material);
        
        // ВАРИАНТ 2: включаем wireframe для контурного эффекта
        // _faceHighlightRenderer->setWireframe(true); // Раскомментировать для wireframe-only
        
        this->addChild(_faceHighlightRenderer);
    }
    
    // === Preview блока для установки ===
    if (_previewVisible) {
        if (auto* mesh = createPlacePreviewMesh(hit)) {
            _placePreviewRenderer = MeshRenderer::create();
            _placePreviewRenderer->addMesh(mesh);
            _placePreviewRenderer->setCameraMask(static_cast<unsigned short>(CameraFlag::USER1));
            
            auto* material = MeshMaterial::createBuiltInMaterial(
                MeshMaterial::MaterialType::UNLIT, false);
            if (material) {
                BlendFunc blend;
                blend.src = backend::BlendFactor::SRC_ALPHA;
                blend.dst = backend::BlendFactor::ONE_MINUS_SRC_ALPHA;
                material->getStateBlock().setBlendFunc(blend);
                material->getStateBlock().setDepthWrite(false);
                
                _placePreviewRenderer->setColor(Color4B(
                    static_cast<uint8_t>(_previewColor.r * 255),
                    static_cast<uint8_t>(_previewColor.g * 255),
                    static_cast<uint8_t>(_previewColor.b * 255),
                    static_cast<uint8_t>(_previewColor.a * 255)
                ));
            }
            _placePreviewRenderer->setMaterial(material);
            
            this->addChild(_placePreviewRenderer);
        }
    }
}

void VoxelRaycasterNode::hideHighlight()
{
    if (_faceHighlightRenderer) {
        _faceHighlightRenderer->removeFromParentAndCleanup(true);
        _faceHighlightRenderer = nullptr;
    }
    if (_placePreviewRenderer) {
        _placePreviewRenderer->removeFromParentAndCleanup(true);
        _placePreviewRenderer = nullptr;
    }
}

// ============================================================================
//  cameraForward / breakBlock / placeBlock
// ============================================================================

Vec3 VoxelRaycasterNode::cameraForward(const ax::Camera* cam)
{
    const Mat4& m = cam->getNodeToWorldTransform();
    Vec3 fwd(-m.m[8], -m.m[9], -m.m[10]);
    fwd.normalize();
    return fwd;
}

bool VoxelRaycasterNode::breakBlock()
{
    if (!_lastHit.has_value() || !_mgr) return false;
    const VoxelHit& h = *_lastHit;
    if (h.t == 0.0f) return false;

    bool ok = _mgr->setBlockAtWorldPos(
        Vec3{static_cast<float>(h.bx) + 0.5f,
             static_cast<float>(h.by) + 0.5f,
             static_cast<float>(h.bz) + 0.5f},
        BLOCK_AIR);

    if (ok) {
        AXLOG("[VoxelRaycaster] breakBlock at (%d, %d, %d) id=%u — OK",
              h.bx, h.by, h.bz, static_cast<unsigned>(h.blockId));
    }
    return ok;
}

bool VoxelRaycasterNode::placeBlock(BlockId id)
{
    if (!_lastHit.has_value() || !_mgr) return false;
    if (id == BLOCK_AIR) return false;
    const VoxelHit& h = *_lastHit;
    if (h.t == 0.0f) return false;

    bool ok = _mgr->setBlockAtWorldPos(h.adjacentCenter(), id);

    if (ok) {
        AXLOG("[VoxelRaycaster] placeBlock id=%u at adjacent (%d, %d, %d) — OK",
              static_cast<unsigned>(id), h.adjX(), h.adjY(), h.adjZ());
    }
    return ok;
}
