#include "ChunkManager.h"
#include "ChunkMeshBuilder.h"
#include "PerlinNoise.hpp"
#include <algorithm>
#include <climits>

// =========================================================================
//  Конструктор / деструктор
// =========================================================================
ChunkManager::ChunkManager() = default;
ChunkManager::~ChunkManager()
{
    shutdown();
}

// =========================================================================
//  init
// =========================================================================
void ChunkManager::init(const Config& cfg)
{
    _cfg = cfg;
    AX_ASSERT(_cfg.workerThreadCount > 0);
    _running.store(true, std::memory_order_release);
    // Инициализация флагов pause/resume
    _paused      = false;
    _initialized = true;

    _genQueue.maxSize = static_cast<size_t>(_cfg.maxQueueSize);

    _terrainAtlas = ax::Director::getInstance()->getTextureCache()->addImage("textures/terrain_atlas.png");

    if (!_terrainAtlas)
    {
        AXLOGE("ChunkManager: terrain atlas not found, creating 1x1 white fallback");
        auto* image           = new ax::Image();
        unsigned char white[] = {255, 255, 255, 255};
        image->initWithRawData(white, sizeof(white), 1, 1, 8);
        _terrainAtlas = ax::Director::getInstance()->getTextureCache()->addImage(image, "fallback_white");
        image->release();
    }
    AX_ASSERT(_terrainAtlas != nullptr && "Failed to create even fallback texture!");

    // Применяем фильтрацию текстур из конфигурации
    applyTextureFilter();

    // Вынос запуска воркеров в отдельный метод
    startWorkers();
}

// =========================================================================
//  startWorkers — выделенная логика запуска воркеров.
//  Используется и в init(), и в resume(). Избегает дублирования кода.
// =========================================================================
void ChunkManager::startWorkers()
{
    // Защита: не запускать дублирующие потоки
    if (!_workers.empty())
        return;

    _running.store(true, std::memory_order_release);

    for (int i = 0; i < _cfg.workerThreadCount; ++i)
        _workers.emplace_back(&ChunkManager::workerLoop, this);
}

// =========================================================================
//  shutdown — только для полного уничтожения (деструктор)
// =========================================================================
void ChunkManager::shutdown()
{
    if (!_initialized)
        return;
    _initialized = false;

    // Останавливаем воркеры
    _running.store(false, std::memory_order_release);
    _genQueue.signalStop();

    for (auto& t : _workers)
        if (t.joinable())
            t.join();
    _workers.clear();

    // Очищаем очереди
    {
        std::lock_guard<std::mutex> lk(_readyMtx);
        _readyChunks.clear();
    }
    _genPendingSet.clear();
    _unloadQueue.clear();

    // Удаляем все визуальные ноды из сцены
    for (auto& [key, entry] : _chunks)
    {
        if (entry.visualNode && _onUnload)
            _onUnload(entry.visualNode, key);
        if (entry.waterNode && _onUnload)
            _onUnload(entry.waterNode, key);
    }
    _chunks.clear();

    // Сбрасываем текстуру (она может быть удалена из кэша при context lost)
    _terrainAtlas = nullptr;

    // Сброс флагов после полного уничтожения
    _paused = false;
}

// =========================================================================
//  pause — вызывается из onExit при сворачивании приложения.
//  Останавливает генерацию, но сохраняет ВСЕ данные чанков в памяти.
//  Визуальные ноды остаются в сцене (они просто не обновляются).
// =========================================================================
void ChunkManager::pause()
{
    if (_paused || !_initialized)
        return;
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
        if (t.joinable())
            t.join();
    _workers.clear();

    // Шаг 3: Обрабатываем чанки, которые воркеры успели сгенерировать
    // до остановки. Без этого при resume они будут «потеряны» в _readyChunks.
    // processReadyChunks() не создаёт новые задачи — просто визуализирует
    // уже готовые данные. Это быстро (создание меша + добавление в сцену).
    // Явная передача флага force=true для обработки оставшихся чанков
    processReadyChunks(true);

    // Шаг 4: Очищаем очередь генерации (при resume заполним заново).
    // НЕ очищаем _chunks — это ядро данных.
    // Используем clearAndReset вместо прямого доступа к полям. Сбрасываем флаг для будущего resume
    // _genQueue.queue.clear(); // TODO проверить как работало до и как теперь
    // _genQueue.stop = false;
    _genQueue.clearAndReset();
    _genPendingSet.clear();

    AXLOGI("ChunkManager: paused, {} chunks preserved", _chunks.size());
}

// =========================================================================
//  resume — вызывается из onEnter при разворачивании приложения.
//  Перезапускает воркеры и инициирует обновление чанков вокруг игрока.
// =========================================================================
void ChunkManager::resume()
{
    if (!_paused || !_initialized)
        return;
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
    if (!_terrainAtlas || _terrainAtlas->getPixelsWide() <= 0)
    {
        _terrainAtlas = ax::Director::getInstance()->getTextureCache()->addImage("textures/terrain_atlas.png");
        applyTextureFilter();
        if (_terrainAtlas)
        {
            // Текстура обновлена, но материал чанков ссылается на старую.
            // Нужно перестроить визуальные ноды для чанков с материалом.
            AXLOGW("ChunkManager: texture reloaded after context loss, marking all chunks dirty");
            for (auto& [key, entry] : _chunks)
            {
                if (entry.status == ChunkStatus::Active && entry.visualNode)
                {
                    entry.dirty = true;
                }
            }
        }
    }

    AXLOGI("ChunkManager: resumed, {} chunks alive, workers restarted", _chunks.size());
}

// =========================================================================
//  workerLoop
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
        // Коллбек _onGenerate всегда установлен из GameScene::init(),
        // где создаётся свой thread_local PerlinNoise.
        {
            std::lock_guard<std::mutex> lk(_readyMtx);
            _readyChunks.push_back(std::move(chunk));
        }
    }
}

// =========================================================================
//  update — главный игровой цикл менеджера чанков
// =========================================================================
void ChunkManager::update(const ax::Vec3& playerWorldPos)
{
    // Пропускаем обновление, если менеджер на паузе.
    // Это предотвращает лишние вычисления при сворачивании приложения.
    if (_paused)
        return;

    ChunkKey playerChunk = worldToChunk(playerWorldPos);

    // Пересобираем кандидатов КАЖДЫЙ кадр, а не только при смене чанка.
    // Чанки, отброшенные из-за переполнения очереди в предыдущем кадре,
    // получат второй шанс и корректно добавятся по мере освобождения слотов.
    collectChunksToLoad(playerChunk);

    if (playerChunk != _lastPlayerChunk)
    {
        _lastPlayerChunk = playerChunk;
        // Выгрузка запускается только при реальном перемещении игрока,
        // так как это тяжёлая операция с итерацией по всей карте _chunks.
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
    auto it      = _chunks.find(key);
    if (it == _chunks.end() || it->second.status != ChunkStatus::Active)
        return BLOCK_AIR;
    if (!it->second.chunkData)
        return BLOCK_AIR;
    int lx = static_cast<int>(std::floor(worldPos.x)) - (key.x * CHUNK_SIZE_X);
    int ly = static_cast<int>(std::floor(worldPos.y)) - (key.y * CHUNK_SIZE_Y);
    int lz = static_cast<int>(std::floor(worldPos.z)) - (key.z * CHUNK_SIZE_Z);
    if (lx >= 0 && lx < CHUNK_SIZE_X && ly >= 0 && ly < CHUNK_SIZE_Y && lz >= 0 && lz < CHUNK_SIZE_Z)
    {
        return it->second.chunkData->getBlock(lx, ly, lz);
    }
    return BLOCK_AIR;
}

// =========================================================================
//  collectChunksToLoad — сбор и приоритизация чанков для генерации
// =========================================================================
void ChunkManager::collectChunksToLoad(const ChunkKey& playerChunk)
{
    // Не загружаем новые чанки, если на паузе
    if (_paused)
        return;

    int rd = _cfg.renderDistance;
    std::vector<std::pair<int, ChunkKey>> candidates;
    // Резервируем память под квадрат видимости, чтобы избежать реаллокаций в цикле
    candidates.reserve((rd * 2 + 1) * (rd * 2 + 1));

    for (int32_t dx = -rd; dx <= rd; ++dx)
        for (int32_t dz = -rd; dz <= rd; ++dz)
        {
            ChunkKey key{playerChunk.x + dx, 0, playerChunk.z + dz};
            // Отсеиваем чанки за пределами кругового радиуса видимости
            if (chunkDistance(key, playerChunk) > rd)
                continue;
            // Пропускаем уже известные чанки (любой статус кроме None)
            auto it = _chunks.find(key);
            if (it != _chunks.end() && it->second.status != ChunkStatus::None)
                continue;
            // Пропускаем чанки, уже стоящие в очереди на генерацию
            if (_genPendingSet.count(key))
                continue;
            // Сохраняем пару (расстояние, ключ) для последующей сортировки
            candidates.emplace_back(chunkDistance(key, playerChunk), key);
        }

    // Сортируем кандидатов: ближние к игроку будут идти первыми
    std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

    for (const auto& [dist, key] : candidates)
    {
        // Сначала проверяем вместимость очереди.
        // Метод push() теперь возвращает bool, что позволяет безопасно откатить транзакцию.
        if (!_genQueue.push(key, dist))
        {
            // Очередь переполнена → прерываем цикл текущего кадра.
            // Ближайшие чанки уже добавлены в приоритетную очередь первыми,
            // дальние автоматически подождут следующего update().
            break;
        }

        // push() вернул true → слот зарезервирован. Теперь безопасно модифицируем состояние.
        // try_emplace возвращает pair<iterator, bool>, где bool = true если запись новая.
        auto [it, inserted] = _chunks.try_emplace(key);
        auto& entry         = it->second;
        entry.status        = ChunkStatus::QueuedForGen;
        entry.visualNode    = nullptr;
        entry.chunkData.reset();
        entry.dirty = false;
        // Регистрируем чанк как "ожидающий генерации", чтобы не дублировать задачи
        _genPendingSet.insert(key);
    }
}

// =========================================================================
//  collectChunksToUnload
// =========================================================================
void ChunkManager::collectChunksToUnload(const ChunkKey& playerChunk)
{
    // Не выгружаем чанки, если на паузе
    if (_paused)
        return;

    int unloadDist = _cfg.renderDistance + _cfg.unloadMargin;
    for (auto it = _chunks.begin(); it != _chunks.end();)
    {
        const auto& key = it->first;
        auto& entry     = it->second;
        if (entry.status == ChunkStatus::None)
        {
            it = _chunks.erase(it);
            continue;
        }
        if (entry.status == ChunkStatus::QueuedForUnload)
        {
            ++it;
            continue;
        }
        if (chunkDistance(key, playerChunk) > unloadDist)
        {
            entry.status = ChunkStatus::QueuedForUnload;
            _unloadQueue.push_back(key);
            ++it;
        }
        else
            ++it;
    }
}

// =========================================================================
//  buildChunkVisualNode
// =========================================================================
ax::Node* ChunkManager::buildChunkVisualNode(const ChunkKey& key, ChunkData& data)
{
    // Получение данных соседей для кросс-чанкового face culling
    const ChunkData* neighborX0 = nullptr;
    const ChunkData* neighborX1 = nullptr;
    const ChunkData* neighborZ0 = nullptr;
    const ChunkData* neighborZ1 = nullptr;
    auto getNeighborData        = [&](int dx, int dz) -> const ChunkData* {
        ChunkKey nk{key.x + dx, key.y, key.z + dz};
        auto nit = _chunks.find(nk);
        if (nit != _chunks.end() && nit->second.status == ChunkStatus::Active && nit->second.chunkData)
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
    // Используем ax::IndexArray вместо std::vector<uint16_t>
    // для совместимости с Mesh::create(const IndexArray& indices).
    // Конструктор по умолчанию IndexArray() инициализирует stride=2 (U_SHORT).
    ax::IndexArray inds;
    buildChunkMesh(data, neighborX0, neighborX1, neighborZ0, neighborZ1, verts, inds);
    if (verts.empty())
        return nullptr;
    ax::Mesh* mesh = createMesh(verts, inds, _terrainAtlas);
    if (!mesh)
        return nullptr;

    //  MeshRenderer — это Node (наследник Node), а НЕ компонент.
    auto* chunkNode = ax::MeshRenderer::create();
    chunkNode->addMesh(mesh);

    //  Используем MeshMaterial::createBuiltInMaterial()
    auto* material = ax::MeshMaterial::createBuiltInMaterial(ax::MeshMaterial::MaterialType::UNLIT, false);
    if (material && _terrainAtlas)
    {
        material->setTexture(_terrainAtlas, ax::NTextureData::Usage::Diffuse);
    }
    chunkNode->setMaterial(material);
    chunkNode->setPosition3D(chunkToWorld(key));
    return chunkNode;
}

// =========================================================================
//  buildWaterVisualNode
// =========================================================================
ax::Node* ChunkManager::buildWaterVisualNode(const ChunkKey& key, ChunkData& data)
{
    // === Получение данных соседей для бесшовного стыка ===
    const ChunkData *neighborX0 = nullptr, *neighborX1 = nullptr;
    const ChunkData *neighborZ0 = nullptr, *neighborZ1 = nullptr;
    auto getNeighborData = [&](int dx, int dz) -> const ChunkData* {
        ChunkKey nk{key.x + dx, key.y, key.z + dz};
        auto nit = _chunks.find(nk);
        // Проверяем статус Active и наличие данных, чтобы избежать race-conditions
        if (nit != _chunks.end() && nit->second.status == ChunkStatus::Active && nit->second.chunkData)
            return nit->second.chunkData.get();
        return nullptr;
    };
    neighborX0 = getNeighborData(-1, 0);
    neighborX1 = getNeighborData(1, 0);
    neighborZ0 = getNeighborData(0, -1);
    neighborZ1 = getNeighborData(0, 1);

    // === Генерация геометрии воды ===
    std::vector<ChunkVertex> verts;
    ax::IndexArray inds;
    buildWaterMesh(data, neighborX0, neighborX1, neighborZ0, neighborZ1, verts, inds);
    if (verts.empty())
        return nullptr;  // Вода отсутствует → оптимизация

    ax::Mesh* mesh = createMesh(verts, inds, _terrainAtlas);
    if (!mesh)
        return nullptr;

    // === Создание рендерера ===
    auto* node = ax::MeshRenderer::create();
    node->addMesh(mesh);

    auto* mat = ax::MeshMaterial::createBuiltInMaterial(ax::MeshMaterial::MaterialType::UNLIT, false);
    if (mat && _terrainAtlas)
        mat->setTexture(_terrainAtlas, ax::NTextureData::Usage::Diffuse);

    // Настройка прозрачности и рендер-стейта
    ax::BlendFunc blend{};
    blend.src = ax::backend::BlendFactor::SRC_ALPHA;
    blend.dst = ax::backend::BlendFactor::ONE_MINUS_SRC_ALPHA;
    mat->getStateBlock().setBlendFunc(blend);
    mat->getStateBlock().setDepthWrite(false);  // Не ломает Z-буфер при наложении прозрачных слоёв
    mat->getStateBlock().setDepthTest(true);
    // Явно выключаем back-face culling: меш содержит обе ориентации граней
    // (верхняя грань + инвертированная копия для вида снизу при погружении),
    // поэтому culling должен быть выключен чтобы обе грани рендерились.
    mat->getStateBlock().setCullFace(false);
    mat->getStateBlock().setCullFaceSide(ax::CullFaceSide::BACK);

    node->setMaterial(mat);
    node->setColor(ax::Color3B(40, 120, 200));  // Базовый синий тон
    node->setOpacity(178);                      // ~70% прозрачности
    node->setPosition3D(chunkToWorld(key));
    node->setTag(WATER_NODE_TAG);  // Тег для быстрого поиска в GameScene
    return node;
}

// =========================================================================
//  processReadyChunks
// =========================================================================
void ChunkManager::processReadyChunks(bool force)
{
    // Явная проверка флага паузы с учетом параметра force
    if (_paused && !force)
        return;

    std::vector<std::unique_ptr<ChunkData>> ready;
    {
        std::lock_guard<std::mutex> lk(_readyMtx);
        if (_readyChunks.empty())
            return;
        ready.swap(_readyChunks);
    }

    // Воркеры работают параллельно и завершают задачи в случайном порядке
    // (зависит от нагрузки CPU, сложности террейна, кэш-локальности).
    // Без сортировки чанки появлялись бы хаотично, игнорируя приоритет очереди.
    // Сортируем готовые чанки по расстоянию до текущей позиции игрока.
    std::sort(ready.begin(), ready.end(), [this](const auto& a, const auto& b) {
        return chunkDistance(a->getKey(), _lastPlayerChunk) < chunkDistance(b->getKey(), _lastPlayerChunk);
    });

    int processed = 0;
    for (auto& chunkPtr : ready)
    {
        if (processed >= _cfg.maxGenerationsPerFrame)
        {
            std::lock_guard<std::mutex> lk(_readyMtx);
            // Возвращаем непроцессированные чанки обратно в очередь для следующего кадра
            _readyChunks.insert(_readyChunks.end(), std::make_move_iterator(ready.begin() + processed),
                                std::make_move_iterator(ready.end()));
            break;
        }

        const ChunkKey& key = chunkPtr->getKey();
        auto it             = _chunks.find(key);

        // Лямбда-хелпер для отмены генерации устаревших/выгружаемых чанков
        auto cancelGen = [&]() {
            _genPendingSet.erase(key);
            if (it != _chunks.end())
            {
                if (it->second.visualNode && _onUnload)
                {
                    _onUnload(it->second.visualNode, key);
                    it->second.visualNode = nullptr;
                }
                it->second.status = ChunkStatus::None;
            }
        };

        if (it == _chunks.end() || it->second.status == ChunkStatus::QueuedForUnload)
        {
            cancelGen();
            continue;
        }
        it->second.chunkData = std::move(chunkPtr);

        // Используем isAllAir() для пропуска пустых чанков
        if (it->second.chunkData->isAllAir())
        {
            it->second.status     = ChunkStatus::Active;
            it->second.visualNode = nullptr;
            it->second.dirty      = false;
            _genPendingSet.erase(key);
            ++processed;
            // Соседи всё равно нужно пометить dirty
            ChunkKey neighborKeys[] = {{key.x - 1, key.y, key.z},
                                       {key.x + 1, key.y, key.z},
                                       {key.x, key.y, key.z - 1},
                                       {key.x, key.y, key.z + 1}};
            for (const auto& nk : neighborKeys)
            {
                auto nit = _chunks.find(nk);
                if (nit != _chunks.end() && nit->second.status == ChunkStatus::Active && !nit->second.dirty)
                {
                    nit->second.dirty = true;
                }
            }
            continue;
        }
        ax::Node* chunkNode   = buildChunkVisualNode(key, *it->second.chunkData);
        it->second.visualNode = chunkNode;
        it->second.waterNode  = buildWaterVisualNode(key, *it->second.chunkData);
        it->second.status     = ChunkStatus::Active;
        it->second.dirty      = false;
        _genPendingSet.erase(key);
        if (_onVisualize)
            _onVisualize(chunkNode, key);
        if (it->second.waterNode && _onVisualize)
            _onVisualize(it->second.waterNode, key);

        // Помечаем соседей dirty для перестройки стыковых граней
        ChunkKey neighborKeys[] = {
            {key.x - 1, key.y, key.z}, {key.x + 1, key.y, key.z}, {key.x, key.y, key.z - 1}, {key.x, key.y, key.z + 1}};
        for (const auto& nk : neighborKeys)
        {
            auto nit = _chunks.find(nk);
            if (nit != _chunks.end() && nit->second.status == ChunkStatus::Active && !nit->second.dirty)
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
    // Не обрабатываем dirty-чанки, если на паузе
    if (_paused)
        return;

    std::vector<ChunkKey> dirtyKeys;
    for (auto& [key, entry] : _chunks)
    {
        if (entry.dirty && entry.status == ChunkStatus::Active && entry.chunkData)
        {
            dirtyKeys.push_back(key);
            if (static_cast<int>(dirtyKeys.size()) >= _cfg.maxDirtyRebuildsPerFrame)
                break;
        }
    }
    for (const auto& key : dirtyKeys)
    {
        auto it = _chunks.find(key);
        if (it == _chunks.end() || !it->second.dirty || it->second.status != ChunkStatus::Active ||
            !it->second.chunkData)
        {
            continue;
        }
        auto& entry = it->second;
        if (entry.visualNode && _onUnload)
        {
            _onUnload(entry.visualNode, key);
            entry.visualNode = nullptr;
        }
        entry.visualNode = buildChunkVisualNode(key, *entry.chunkData);
        if (entry.waterNode && _onUnload)
            _onUnload(entry.waterNode, key);
        entry.waterNode = buildWaterVisualNode(key, *entry.chunkData);
        entry.dirty     = false;
        if (_onVisualize)
            _onVisualize(entry.visualNode, key);
        if (entry.waterNode && _onVisualize)
            _onVisualize(entry.waterNode, key);
    }
}

// =========================================================================
//  processUnloadQueue
// =========================================================================
void ChunkManager::processUnloadQueue()
{
    // Не выгружаем чанки, если на паузе
    if (_paused)
        return;

    int processed = 0;
    auto it       = _unloadQueue.begin();
    while (it != _unloadQueue.end() && processed < _cfg.maxUnloadsPerFrame)
    {
        const ChunkKey& key = *it;
        auto mapIt          = _chunks.find(key);
        if (mapIt != _chunks.end() && mapIt->second.status == ChunkStatus::QueuedForUnload)
        {
            if (mapIt->second.visualNode && _onUnload)
                _onUnload(mapIt->second.visualNode, key);
            if (mapIt->second.waterNode && _onUnload)
                _onUnload(mapIt->second.waterNode, key);
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

// =========================================================================
//  Фильтрация текстур
// =========================================================================
void ChunkManager::applyTextureFilter()
{
    if (!_terrainAtlas)
        return;

    // Настройка параметров текстуры для пиксельной фильтрации
    ax::Texture2D::TexParams texParams;

    switch (_cfg.textureFilter)
    {
    case TextureFilterMode::NEAREST:
        texParams.magFilter = ax::backend::SamplerFilter::NEAREST;
        texParams.minFilter = ax::backend::SamplerFilter::NEAREST;
        AXLOGI("ChunkManager: texture filter = NEAREST");
        break;

    case TextureFilterMode::NEAREST_MIPMAP_NEAREST:
        texParams.magFilter = ax::backend::SamplerFilter::NEAREST;
        texParams.minFilter = ax::backend::SamplerFilter::NEAREST_MIPMAP_NEAREST;
        // Автоматическая генерация mipmaps при первом использовании
        if (!_terrainAtlas->hasMipmaps())
        {
            _terrainAtlas->generateMipmap();
            AXLOGI("ChunkManager: mipmaps generated");
        }
        AXLOGI("ChunkManager: texture filter = NEAREST_MIPMAP_NEAREST");
        break;
    }

    // Применяем параметры к текстуре-атласу
    _terrainAtlas->setTexParameters(texParams);
}

// =========================================================================
//  Установка типа блока по мировым координатам
// =========================================================================

bool ChunkManager::setBlockAtWorldPos(const ax::Vec3& worldPos, BlockId id)
{
    ChunkKey key = worldToChunk(worldPos);
    auto it      = _chunks.find(key);
    if (it == _chunks.end())
        return false;

    ChunkEntry& entry = it->second;
    if (entry.status != ChunkStatus::Active || !entry.chunkData)
        return false;

    ax::Vec3 base = chunkToWorld(key);
    int lx        = static_cast<int>(std::floor(worldPos.x)) - static_cast<int>(base.x);
    int ly        = static_cast<int>(std::floor(worldPos.y)) - static_cast<int>(base.y);
    int lz        = static_cast<int>(std::floor(worldPos.z)) - static_cast<int>(base.z);

    if (lx < 0 || lx >= CHUNK_SIZE_X)
        return false;
    if (ly < 0 || ly >= CHUNK_SIZE_Y)
        return false;
    if (lz < 0 || lz >= CHUNK_SIZE_Z)
        return false;

    entry.chunkData->setBlock(lx, ly, lz, id);
    entry.dirty = true;

    // Помечаем соседние чанки dirty если блок на границе
    const int lCoords[3]    = {lx, ly, lz};
    const int chunkSizes[3] = {CHUNK_SIZE_X, CHUNK_SIZE_Y, CHUNK_SIZE_Z};

    const int kAxis[6] = {0, 0, 1, 1, 2, 2};
    const int kDir[6]  = {1, -1, 1, -1, 1, -1};

    for (int i = 0; i < 6; ++i)
    {
        int axis = kAxis[i];
        int dir  = kDir[i];

        bool onBoundary = (dir > 0) ? (lCoords[axis] == chunkSizes[axis] - 1) : (lCoords[axis] == 0);

        if (!onBoundary)
            continue;

        ChunkKey neighborKey = key;
        if (axis == 0)
            neighborKey.x += dir;
        else if (axis == 1)
            neighborKey.y += dir;
        else
            neighborKey.z += dir;

        auto nit = _chunks.find(neighborKey);
        if (nit != _chunks.end() && nit->second.status == ChunkStatus::Active)
        {
            nit->second.dirty = true;
        }
    }

    return true;
}
