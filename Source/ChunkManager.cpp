#include "ChunkManager.h"
#include "ChunkMeshBuilder.h"
#include "PerlinNoise.hpp"
#include <algorithm>
#include <climits>

// =========================================================================
//  Конструктор / деструктор
// =========================================================================
ChunkManager::ChunkManager() = default;
ChunkManager::~ChunkManager() { shutdown(); }

// =========================================================================
//  init
// =========================================================================
void ChunkManager::init(const Config& cfg)
{
    _cfg = cfg;
    AX_ASSERT(_cfg.workerThreadCount > 0);
    _running.store(true, std::memory_order_release);
    // ⚡ FIX: Инициализация флагов pause/resume
    _paused = false;
    _initialized = true;

    _genQueue.maxSize = static_cast<size_t>(_cfg.maxQueueSize);

    _terrainAtlas = ax::Director::getInstance()->getTextureCache()->addImage("textures/terrain_atlas.png");

    if (!_terrainAtlas) {
        AXLOGE("ChunkManager: terrain atlas not found, creating 1x1 white fallback");
        auto* image = new ax::Image();
        unsigned char white[] = {255, 255, 255, 255};
        image->initWithRawData(white, sizeof(white), 1, 1, 8);
        _terrainAtlas = ax::Director::getInstance()->getTextureCache()->addImage(image, "fallback_white");
        image->release();
    }
    AX_ASSERT(_terrainAtlas != nullptr && "Failed to create even fallback texture!");

    // ⚡ FIX: Вынос запуска воркеров в отдельный метод
    startWorkers();
}

// =========================================================================
//  ⚡ FIX: startWorkers — выделенная логика запуска воркеров.
//  Используется и в init(), и в resume(). Избегает дублирования кода.
// =========================================================================
void ChunkManager::startWorkers()
{
    // Защита: не запускать дублирующие потоки
    if (!_workers.empty()) return;

    _running.store(true, std::memory_order_release);

    for (int i = 0; i < _cfg.workerThreadCount; ++i)
        _workers.emplace_back(&ChunkManager::workerLoop, this);
}

// =========================================================================
//  shutdown — только для полного уничтожения (деструктор)
// =========================================================================
void ChunkManager::shutdown()
{
    if (!_initialized) return;
    _initialized = false;

    // Останавливаем воркеры
    _running.store(false, std::memory_order_release);
    _genQueue.signalStop();

    for (auto& t : _workers)
        if (t.joinable()) t.join();
    _workers.clear();

    // Очищаем очереди
    { std::lock_guard<std::mutex> lk(_readyMtx); _readyChunks.clear(); }
    _genPendingSet.clear();
    _unloadQueue.clear();

    // Удаляем все визуальные ноды из сцены
    for (auto& [key, entry] : _chunks) {
        if (entry.visualNode && _onUnload)
            _onUnload(entry.visualNode, key);
    }
    _chunks.clear();

    // Сбрасываем текстуру (она может быть удалена из кэша при context lost)
    _terrainAtlas = nullptr;

    // ⚡ FIX: Сброс флагов после полного уничтожения
    _paused = false;
}

// =========================================================================
//  ⚡ FIX: pause — вызывается из onExit при сворачивании приложения.
//  Останавливает генерацию, но сохраняет ВСЕ данные чанков в памяти.
//  Визуальные ноды остаются в сцене (они просто не обновляются).
// =========================================================================
void ChunkManager::pause()
{
    if (_paused || !_initialized) return;
    _paused = true;

    // Шаг 1: Останавливаем приём новых задач в очередь генерации.
    // Воркеры завершат текущую генерацию чанка и выйдут из pop()
    // с false (т.к. stop=true). Это гарантирует, что ни один чанк
    // не останется в «наполовину сгенерированном» состоянии.
    _genQueue.signalStop();

    // Шаг 2: Дожидаемся завершения всех воркеров.
    // Это БЛОКИРУЮЩИЙ вызов, но при сворачивании приложения
    // задержка в несколько мс допустима — рендер уже остановлен.
    for (auto& t : _workers)
        if (t.joinable()) t.join();
    _workers.clear();

    // Шаг 3: Обрабатываем чанки, которые воркеры успели сгенерировать
    // до остановки. Без этого при resume они будут «потеряны» в _readyChunks.
    // processReadyChunks() не создаёт новые задачи — просто визуализирует
    // уже готовые данные. Это быстро (создание меша + добавление в сцену).
    processReadyChunks();

    // Шаг 4: Очищаем очередь генерации (при resume заполним заново).
    // НЕ очищаем _chunks — это ядро данных.
    // ⚡ Используем clearAndReset вместо прямого доступа к полям. Сбрасываем флаг для будущего resume
	// _genQueue.queue.clear(); // TODO проверить как работало до и как теперь
	// _genQueue.stop = false;
	_genQueue.clearAndReset();
	_genPendingSet.clear();

    AXLOGI("ChunkManager: paused, {} chunks preserved", _chunks.size());
}

// =========================================================================
//  ⚡ FIX: resume — вызывается из onEnter при разворачивании приложения.
//  Перезапускает воркеры и инициирует обновление чанков вокруг игрока.
// =========================================================================
void ChunkManager::resume()
{
    if (!_paused || !_initialized) return;
    _paused = false;

    // Шаг 1: Перезапускаем воркеры.
    // _running всё ещё true (не сбрасывался при pause),
    // очередь уже очищена от stop-флага.
    startWorkers();

    // Шаг 2: Сбрасываем _lastPlayerChunk чтобы при следующем update()
    // произошла полная переоценка — какие чанки нужны, какие нет.
    // За время сворачивания игрок мог «переместиться» (например,
    // при восстановлении из сохранения) или чанки могли устареть.
    _lastPlayerChunk = ChunkKey{INT32_MIN, INT32_MIN, INT32_MIN};

    // Шаг 3: Проверяем, не потеряна ли текстура (context loss на мобильных).
    // На Desktop/GL контекст может быть пересоздан при сворачивании —
    // текстура станет невалидной. Пересоздаём из кэша.
    if (!_terrainAtlas || _terrainAtlas->getPixelsWide() <= 0) {
        _terrainAtlas = ax::Director::getInstance()->getTextureCache()->addImage("textures/terrain_atlas.png");
        if (_terrainAtlas) {
            // Текстура обновлена, но материал чанков ссылается на старую.
            // Нужно перестроить визуальные ноды для чанков с материалом.
            AXLOGW("ChunkManager: texture reloaded after context loss, marking all chunks dirty");
            for (auto& [key, entry] : _chunks) {
                if (entry.status == ChunkStatus::Active && entry.visualNode) {
                    entry.dirty = true;
                }
            }
        }
    }

    AXLOGI("ChunkManager: resumed, {} chunks alive, workers restarted", _chunks.size());
}

// =========================================================================
//  workerLoop — 🔧 FIX: Убран дублирующий thread_local PerlinNoise + ветка else
// =========================================================================
void ChunkManager::workerLoop()
{
    ChunkKey key;
    while (_genQueue.pop(key))
    {
        auto chunk = std::make_unique<ChunkData>(key);
        if (_onGenerate)
        {
            _onGenerate(*chunk);
        }
        // 🔧 FIX: Удалена ветка else с дублирующим thread_local PerlinNoise.
        // Коллбек _onGenerate всегда установлен из GameScene::init(),
        // где создаётся свой thread_local PerlinNoise.
        {
            std::lock_guard<std::mutex> lk(_readyMtx);
            _readyChunks.push_back(std::move(chunk));
        }
    }
}

// =========================================================================
//  update
// =========================================================================
void ChunkManager::update(const ax::Vec3& playerWorldPos)
{
    // ⚡ FIX: Пропускаем обновление, если менеджер на паузе.
    // Это предотвращает лишние вычисления при сворачивании приложения.
    if (_paused) return;

    ChunkKey playerChunk = worldToChunk(playerWorldPos);
    if (playerChunk != _lastPlayerChunk)
    {
        _lastPlayerChunk = playerChunk;
        collectChunksToLoad(playerChunk);
        collectChunksToUnload(playerChunk);
    }
    processReadyChunks();
    processDirtyChunks();
    processUnloadQueue();
}

// =========================================================================
//  getBlockAtWorldPos
// =========================================================================
BlockId ChunkManager::getBlockAtWorldPos(const ax::Vec3& worldPos) const
{
    ChunkKey key = worldToChunk(worldPos);
    auto it = _chunks.find(key);
    if (it == _chunks.end() || it->second.status != ChunkStatus::Active)
        return BLOCK_AIR;
    if (!it->second.chunkData) return BLOCK_AIR;
    int lx = static_cast<int>(std::floor(worldPos.x)) - (key.x * CHUNK_SIZE_X);
    int ly = static_cast<int>(std::floor(worldPos.y)) - (key.y * CHUNK_SIZE_Y);
    int lz = static_cast<int>(std::floor(worldPos.z)) - (key.z * CHUNK_SIZE_Z);
    if (lx >= 0 && lx < CHUNK_SIZE_X &&
        ly >= 0 && ly < CHUNK_SIZE_Y &&
        lz >= 0 && lz < CHUNK_SIZE_Z)
    {
        return it->second.chunkData->getBlock(lx, ly, lz);
    }
    return BLOCK_AIR;
}

// =========================================================================
//  collectChunksToLoad — 🔧 FIX: try_emplace вместо двойного lookup
// =========================================================================
void ChunkManager::collectChunksToLoad(const ChunkKey& playerChunk)
{
    // ⚡ FIX: Не загружаем новые чанки, если на паузе
    if (_paused) return;

    int rd = _cfg.renderDistance;
    std::vector<std::pair<int, ChunkKey>> candidates;
    candidates.reserve((rd * 2 + 1) * (rd * 2 + 1));
    for (int32_t dx = -rd; dx <= rd; ++dx)
        for (int32_t dz = -rd; dz <= rd; ++dz)
        {
            ChunkKey key{playerChunk.x + dx, 0, playerChunk.z + dz};
            if (chunkDistance(key, playerChunk) > rd) continue;
            auto it = _chunks.find(key);
            if (it != _chunks.end() && it->second.status != ChunkStatus::None) continue;
            if (_genPendingSet.count(key)) continue;
            candidates.emplace_back(chunkDistance(key, playerChunk), key);
        }
    std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b){ return a.first < b.first; });
    for (const auto& [dist, key] : candidates)
    {
        // 🔧 FIX: try_emplace — один lookup вместо find + operator[]
        auto [it, inserted] = _chunks.try_emplace(key);
        auto& entry = it->second;
        entry.status = ChunkStatus::QueuedForGen;
        entry.visualNode = nullptr;
        entry.chunkData.reset();
        entry.dirty = false;
        _genPendingSet.insert(key);
        _genQueue.push(key);
    }
}

// =========================================================================
//  collectChunksToUnload
// =========================================================================
void ChunkManager::collectChunksToUnload(const ChunkKey& playerChunk)
{
    // ⚡ FIX: Не выгружаем чанки, если на паузе
    if (_paused) return;

    int unloadDist = _cfg.renderDistance + _cfg.unloadMargin;
    for (auto it = _chunks.begin(); it != _chunks.end(); )
    {
        const auto& key = it->first;
        auto& entry = it->second;
        if (entry.status == ChunkStatus::None) {
            it = _chunks.erase(it);
            continue;
        }
        if (entry.status == ChunkStatus::QueuedForUnload)
        { ++it; continue; }
        if (chunkDistance(key, playerChunk) > unloadDist)
        {
            entry.status = ChunkStatus::QueuedForUnload;
            _unloadQueue.push_back(key);
            ++it;
        }
        else ++it;
    }
}

// =========================================================================
//  buildChunkVisualNode — 🔧 FIX: Полностью переписан на реальный API
// =========================================================================
ax::Node* ChunkManager::buildChunkVisualNode(const ChunkKey& key, ChunkData& data)
{
    // Получение данных соседей для кросс-чанкового face culling
    const ChunkData* neighborX0 = nullptr;
    const ChunkData* neighborX1 = nullptr;
    const ChunkData* neighborZ0 = nullptr;
    const ChunkData* neighborZ1 = nullptr;
    auto getNeighborData = [&](int dx, int dz) -> const ChunkData* {
        ChunkKey nk{key.x + dx, key.y, key.z + dz};
        auto nit = _chunks.find(nk);
        if (nit != _chunks.end() &&
            nit->second.status == ChunkStatus::Active &&
            nit->second.chunkData)
        {
            return nit->second.chunkData.get();
        }
        return nullptr;
    };
    neighborX0 = getNeighborData(-1, 0);
    neighborX1 = getNeighborData(1, 0);
    neighborZ0 = getNeighborData(0, -1);
    neighborZ1 = getNeighborData(0, 1);

    // Мешбилдинг
    std::vector<ChunkVertex> verts;
    // ⚡ MIN FIX: Тип изменён с std::vector<uint16_t> на ax::IndexArray
    // для совместимости с Mesh::create(const IndexArray& indices).
    // Конструктор по умолчанию IndexArray() инициализирует stride=2 (U_SHORT).
    ax::IndexArray inds;
    buildChunkMesh(data, neighborX0, neighborX1, neighborZ0, neighborZ1, verts, inds);
    if (verts.empty()) return nullptr;
    ax::Mesh* mesh = createMesh(verts, inds, _terrainAtlas);
    if (!mesh) return nullptr;

    // 🔧 FIX: MeshRenderer — это Node (наследник Node), а НЕ компонент.
    auto* chunkNode = ax::MeshRenderer::create();
    chunkNode->addMesh(mesh);

    // 🔧 FIX: Используем MeshMaterial::createBuiltInMaterial()
    auto* material = ax::MeshMaterial::createBuiltInMaterial(
        ax::MeshMaterial::MaterialType::UNLIT, false);
    if (material && _terrainAtlas)
    {
        // MeshMaterial::setTexture(Texture2D*, NTextureData::Usage) — реальный API
        material->setTexture(_terrainAtlas, ax::NTextureData::Usage::Diffuse);
    }
    chunkNode->setMaterial(material);
    chunkNode->setPosition3D(chunkToWorld(key));
    return chunkNode;
}

// =========================================================================
//  processReadyChunks
// =========================================================================
void ChunkManager::processReadyChunks()
{
    std::vector<std::unique_ptr<ChunkData>> ready;
    {
        std::lock_guard<std::mutex> lk(_readyMtx);
        if (_readyChunks.empty()) return;
        ready.swap(_readyChunks);
    }
    int processed = 0;
    for (auto& chunkPtr : ready)
    {
        if (processed >= _cfg.maxGenerationsPerFrame)
        {
            std::lock_guard<std::mutex> lk(_readyMtx);
            _readyChunks.insert(_readyChunks.end(),
                std::make_move_iterator(ready.begin() + processed),
                std::make_move_iterator(ready.end()));
            break;
        }
        const ChunkKey& key = chunkPtr->getKey();
        auto it = _chunks.find(key);
        auto cancelGen = [&]() {
            _genPendingSet.erase(key);
            if (it != _chunks.end()) {
                if (it->second.visualNode && _onUnload) {
                    _onUnload(it->second.visualNode, key);
                    it->second.visualNode = nullptr;
                }
                it->second.status = ChunkStatus::None;
            }
        };
        if (it == _chunks.end() || it->second.status == ChunkStatus::QueuedForUnload)
        { cancelGen(); continue; }
        it->second.chunkData = std::move(chunkPtr);
        // 🔧 Используем isAllAir() для пропуска пустых чанков
        if (it->second.chunkData->isAllAir()) {
            it->second.status = ChunkStatus::Active;
            it->second.visualNode = nullptr;
            it->second.dirty = false;
            _genPendingSet.erase(key);
            ++processed;
            // Соседи всё равно нужно пометить dirty
            ChunkKey neighborKeys[] = {
                {key.x - 1, key.y, key.z}, {key.x + 1, key.y, key.z},
                {key.x, key.y, key.z - 1}, {key.x, key.y, key.z + 1}
            };
            for (const auto& nk : neighborKeys) {
                auto nit = _chunks.find(nk);
                if (nit != _chunks.end() &&
                    nit->second.status == ChunkStatus::Active &&
                    !nit->second.dirty)
                {
                    nit->second.dirty = true;
                }
            }
            continue;
        }
        ax::Node* chunkNode = buildChunkVisualNode(key, *it->second.chunkData);
        it->second.visualNode = chunkNode;
        it->second.status = ChunkStatus::Active;
        it->second.dirty = false;
        _genPendingSet.erase(key);
        if (_onVisualize) _onVisualize(chunkNode, key);
        ChunkKey neighborKeys[] = {
            {key.x - 1, key.y, key.z}, {key.x + 1, key.y, key.z},
            {key.x, key.y, key.z - 1}, {key.x, key.y, key.z + 1}
        };
        for (const auto& nk : neighborKeys) {
            auto nit = _chunks.find(nk);
            if (nit != _chunks.end() &&
                nit->second.status == ChunkStatus::Active &&
                !nit->second.dirty)
            {
                nit->second.dirty = true;
            }
        }
        ++processed;
    }
}

// =========================================================================
//  processDirtyChunks
// =========================================================================
void ChunkManager::processDirtyChunks()
{
    // ⚡ FIX: Не обрабатываем dirty-чанки, если на паузе
    if (_paused) return;

    std::vector<ChunkKey> dirtyKeys;
    for (auto& [key, entry] : _chunks) {
        if (entry.dirty && entry.status == ChunkStatus::Active && entry.chunkData) {
            dirtyKeys.push_back(key);
            if (static_cast<int>(dirtyKeys.size()) >= _cfg.maxDirtyRebuildsPerFrame)
                break;
        }
    }
    for (const auto& key : dirtyKeys) {
        auto it = _chunks.find(key);
        if (it == _chunks.end() || !it->second.dirty ||
            it->second.status != ChunkStatus::Active || !it->second.chunkData)
        {
            continue;
        }
        auto& entry = it->second;
        if (entry.visualNode && _onUnload) {
            _onUnload(entry.visualNode, key);
            entry.visualNode = nullptr;
        }
        entry.visualNode = buildChunkVisualNode(key, *entry.chunkData);
        entry.dirty = false;
        if (_onVisualize) _onVisualize(entry.visualNode, key);
    }
}

// =========================================================================
//  processUnloadQueue
// =========================================================================
void ChunkManager::processUnloadQueue()
{
    // ⚡ FIX: Не выгружаем чанки, если на паузе
    if (_paused) return;

    int processed = 0;
    auto it = _unloadQueue.begin();
    while (it != _unloadQueue.end() && processed < _cfg.maxUnloadsPerFrame)
    {
        const ChunkKey& key = *it;
        auto mapIt = _chunks.find(key);
        if (mapIt != _chunks.end() && mapIt->second.status == ChunkStatus::QueuedForUnload)
        {
            if (mapIt->second.visualNode && _onUnload)
                _onUnload(mapIt->second.visualNode, key);
            _chunks.erase(mapIt);
            _genPendingSet.erase(key);
            it = _unloadQueue.erase(it);
            ++processed;
        }
        else
        {
            ++it;
        }
    }
}

// =========================================================================
//  Геттеры
// =========================================================================
ax::Node* ChunkManager::getChunkNode(const ChunkKey& key) const
{
    auto it = _chunks.find(key);
    if (it != _chunks.end() && it->second.status == ChunkStatus::Active)
        return it->second.visualNode;
    return nullptr;
}
bool ChunkManager::isChunkActive(const ChunkKey& key) const
{
    auto it = _chunks.find(key);
    return it != _chunks.end() && it->second.status == ChunkStatus::Active;
}
