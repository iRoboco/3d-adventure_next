#pragma once
#include "axmol.h"
#include "ChunkManager.h"

/**
 * @file ChunkMeshBuilder.h
 * @brief Модуль построения геометрии чанков для воксельного мира.
 * 
 * Реализует алгоритм генерации мешей с оптимизациями:
 * - Face Culling (отсечение невидимых граней)
 * - Кросс-чанковая проверка соседей для устранения щелей на стыках
 * - Триангуляция квадов в индексный буфер
 * - UV-развёртка для текстурного атласа
 * 
 * Все функции inline для минимизации накладных расходов вызова.
 */

// ============================================================================
/** @name Структуры данных
 *  Базовые типы для хранения вершинной геометрии
 */
/// @{

/**
 * @struct ChunkVertex
 * @brief Интерливинговая структура вершины для воксельного меша.
 * 
 * Оптимизирована под UNLIT-рендеринг:
 * - Позиция (x,y,z): локальные координаты внутри чанка [0..16]×[0..256]×[0..16]
 * - Текстура (u,v): нормированные UV-координаты для атласа [0.0..1.0]
 * 
 * Размер: 5 × float = 20 байт. Нормали не хранятся — вычисляются в шейдере
 * или задаются константой для UNLIT-материала.
 */
struct ChunkVertex
{
    float x, y, z;    ///< Позиция вершины в локальном пространстве чанка
    float u, v;       ///< UV-координаты для выборки из текстурного атласа
};

/// @}
// ============================================================================

/** @name Вспомогательные функции
 *  Утилиты для расчёта параметров рендеринга
 */
/// @{

/**
 * @brief Рассчитывает UV-координаты тайла для заданного blockId в атласе.
 * 
 * @param blockId Идентификатор блока (индекс тайла в атласе)
 * @param atlasSize Размер сетки атласа (по умолчанию 16×16)
 * @return std::array<float, 4> Массив [u_min, v_min, u_max, v_max]
 * 
 * @details
 * Математика развёртки:
 * @code
 * col = blockId % atlasSize          // номер столбца (0..atlasSize-1)
 * row = blockId / atlasSize          // номер строки
 * tileU = col / atlasSize            // левая граница тайла по U
 * tileV = row / atlasSize            // нижняя граница тайла по V
 * tileSize = 1.0 / atlasSize         // размер одного тайла в нормированных координатах
 * @endcode
 * 
 * Возвращаемые координаты позволяют "натянуть" текстуру тайла ровно на грань блока
 * без искажений. Для BLOCK_AIR (id=0) принудительно возвращается первый тайл.
 * 
 * @note Функция inline — компилируется в месте вызова, нулевые накладные расходы.
 */
inline std::array<float, 4> calculateBlockUV(uint16_t blockId, int atlasSize = 4)
{
    // Защита от деления на 0 и корректная обработка воздуха
    uint16_t idx = (blockId == 0 || blockId >= 256) ? 0 : blockId;
    //uint16_t idx = (blockId == 0) ? 0 : (blockId % 256);
    
    // Расчёт границ тайла в нормированном пространстве [0.0, 1.0]
    float tileU = (idx % atlasSize) / static_cast<float>(atlasSize);
    float tileV = (idx / atlasSize) / static_cast<float>(atlasSize);
    float tileSize = 1.0f / static_cast<float>(atlasSize);

    // [u_min, v_min, u_max, v_max] — готово для привязки к вершинам
    return { tileU, tileV, tileU + tileSize, tileV + tileSize };
}

/// @}
// ============================================================================

/** @name Основные алгоритмы
 *  Ядро генерации геометрии чанка
 */
/// @{

/**
 * @brief Генерирует вершины и индексы для видимых граней чанка.
 * 
 * @param data Ссылка на данные текущего чанка (источник блоков)
 * @param neighborX0 Данные чанка-соседа по -X (nullptr если не активен)
 * @param neighborX1 Данные чанка-соседа по +X (nullptr если не активен)
 * @param neighborZ0 Данные чанка-соседа по -Z (nullptr если не активен)
 * @param neighborZ1 Данные чанка-соседа по +Z (nullptr если не активен)
 * @param vertices Выходной вектор вершин формата ChunkVertex
 * @param indices Выходной индексный буфер Axmol (тип uint16_t)
 * @param defaultBlockUV Резервный UV-индекс для блоков без текстуры
 * 
 * @details
 * ## Механизм работы:
 * 
 * ### 1. Face Culling (отсечение граней)
 * Для каждого непрозрачного блока проверяются 6 соседей. Грань генерируется
 * только если соседний воксель = BLOCK_AIR или находится за пределами карты.
 * Лямбда `isSolid` обрабатывает кросс-чанковые переходы: если блок на границе
 * чанка, запрашиваются данные из соседнего чанка (если он активен).
 * 
 * ### 2. Порядок вершин (CCW — Counter-Clockwise)
 * Все грани добавляются в порядке против часовой стрелки относительно
 * внешней нормали. Это критично для Backface Culling: GPU автоматически
 * отбрасывает невидимые обратные грани, экономя ~50% вершинного трафика.
 * 
 * ### 3. Триангуляция
 * Каждый квад (4 вершины) разбивается на 2 треугольника по диагонали:
 * @code
 * Треугольник 1: v0 → v1 → v2  (индексы: base+0, +1, +2)
 * Треугольник 2: v0 → v2 → v3  (индексы: base+0, +2, +3)
 * @endcode
 * Индексный буфер позволяет переиспользовать вершины, экономя память.
 * 
 * ### 4. UV-координаты
 * Вычисляются один раз на блок (не на грань!), затем применяются ко всем
 * вершинам грани. Это гарантирует корректное наложение текстуры без швов.
 * 
 * ### 5. Оптимизация прохода
 * Циклы организованы в порядке Y-major (Z → Y → X) для лучшей кэш-локальности
 * при доступе к линейному массиву блоков в ChunkData.
 * 
 * @note Функция не создаёт графические объекты — только подготавливает
 * сырые данные для последующей передачи в ax::Mesh::create().
 */
inline void buildChunkMesh(const ChunkData& data,
                          const ChunkData* neighborX0,
                          const ChunkData* neighborX1,
                          const ChunkData* neighborZ0,
                          const ChunkData* neighborZ1,
                          std::vector<ChunkVertex>& vertices,
                          ax::IndexArray& indices,
                          uint16_t defaultBlockUV = 1)
{
    constexpr int CX = CHUNK_SIZE_X;  ///< Размер чанка по X (16)
    constexpr int CY = CHUNK_SIZE_Y;  ///< Размер чанка по Y (256)
    constexpr int CZ = CHUNK_SIZE_Z;  ///< Размер чанка по Z (16)
    
    // Предварительное резервирование: максимум 24 вершины на блок (6 граней × 4 вершины)
    vertices.reserve(CX * CY * CZ * 24);

    /**
     * @brief Лямбда-проверка "сплошности" вокселя для Face Culling.
     * @param lx,ly,lz Локальные координаты проверяемого соседа
     * @return true если блок непрозрачный (грань не нужна)
     * 
     * @details
     * Обрабатывает два случая:
     * 1. Сосед внутри текущего чанка — прямой доступ к data.getBlock()
     * 2. Сосед за границей чанка — запрос к соответствующему neighbor-чанку
     * 
     * Если соседний чанк не передан (nullptr) или не активен — считаем
     * область за границей "воздухом", чтобы сгенерировать торцевую грань.
     */
    auto isSolid = [&](int lx, int ly, int lz) -> bool {
        // Локальная проверка: блок внутри текущего чанка
        if (lx >= 0 && lx < CX && ly >= 0 && ly < CY && lz >= 0 && lz < CZ) {
            return data.getBlock(lx, ly, lz) != BLOCK_AIR;
        }
        // Кросс-чанковые проверки по осям X и Z
        if (lx < 0    && neighborX0) return neighborX0->getBlock(CX-1, ly, lz) != BLOCK_AIR;
        if (lx >= CX  && neighborX1) return neighborX1->getBlock(0, ly, lz)    != BLOCK_AIR;
        if (lz < 0    && neighborZ0) return neighborZ0->getBlock(lx, ly, CZ-1) != BLOCK_AIR;
        if (lz >= CZ  && neighborZ1) return neighborZ1->getBlock(lx, ly, 0)    != BLOCK_AIR;
        // За пределами известных чанков — считаем воздух (грань нужна)
        return false;
    };

    /**
     * @brief Добавляет одну грань (квад) в буферы вершин и индексов.
     * @param v0,v1,v2,v3 Вершины квада в строгом CCW-порядке
     * 
     * @details
     * 1. Запоминает стартовый индекс вершин (baseIdx)
     * 2. Добавляет 4 вершины в конец вектора
     * 3. Генерирует 6 индексов для двух треугольников:
     *    - Треугольник 1: (baseIdx+0, +1, +2)
     *    - Треугольник 2: (baseIdx+0, +2, +3)
     * 
     * CCW-порядок сохраняется в обоих треугольниках — критично для
     * корректного определения направления нормали и Backface Culling.
     */
    auto addFace = [&](const ChunkVertex& v0, const ChunkVertex& v1,
                       const ChunkVertex& v2, const ChunkVertex& v3)
    {
        uint16_t baseIdx = static_cast<uint16_t>(vertices.size());  ///< Стартовый индекс для текущей грани
        vertices.insert(vertices.end(), {v0, v1, v2, v3});

        // Триангуляция: квад → 2 треугольника по диагонали v0→v2
        indices.emplace_back<uint16_t>(baseIdx + 0);
        indices.emplace_back<uint16_t>(baseIdx + 1);
        indices.emplace_back<uint16_t>(baseIdx + 2);
        indices.emplace_back<uint16_t>(baseIdx + 0);
        indices.emplace_back<uint16_t>(baseIdx + 2);
        indices.emplace_back<uint16_t>(baseIdx + 3);
    };

    // Основной проход по всем вокселям чанка (Y-major для кэш-локальности)
    for (int z = 0; z < CZ; ++z)
        for (int y = 0; y < CY; ++y)
            for (int x = 0; x < CX; ++x)
            {
                uint16_t bid = data.getBlock(x, y, z);
                if (bid == BLOCK_AIR) continue;  ///< Пропускаем воздух — геометрия не нужна

                // UV вычисляем один раз на блок, переиспользуем для всех 6 граней
                auto uv = calculateBlockUV(bid);
                float x0 = x, x1 = x + 1;
                float y0 = y, y1 = y + 1;
                float z0 = z, z1 = z + 1;

                // === Генерация граней по 6 направлениям ===
                // Каждая грань создаётся только если сосед — воздух (!isSolid)
                // Вершины задаются в CCW-порядке относительно внешней нормали
                
                // +X (правая грань, нормаль (1,0,0))
                if (!isSolid(x + 1, y, z)) addFace(
                    {x1, y0, z0, uv[0], uv[1]}, {x1, y1, z0, uv[0], uv[3]},
                    {x1, y1, z1, uv[2], uv[3]}, {x1, y0, z1, uv[2], uv[1]});
                
                // -X (левая грань, нормаль (-1,0,0))
                if (!isSolid(x - 1, y, z)) addFace(
                    {x0, y0, z1, uv[2], uv[1]}, {x0, y1, z1, uv[2], uv[3]},
                    {x0, y1, z0, uv[0], uv[3]}, {x0, y0, z0, uv[0], uv[1]});
                 
                // +Y (верхняя грань, нормаль (0,1,0))
                if (!isSolid(x, y + 1, z)) addFace(
                    {x0, y1, z1, uv[0], uv[3]}, {x1, y1, z1, uv[2], uv[3]},
                    {x1, y1, z0, uv[2], uv[1]}, {x0, y1, z0, uv[0], uv[1]});
                
                // -Y (нижняя грань, нормаль (0,-1,0))
                if (!isSolid(x, y - 1, z)) addFace(
                    {x0, y0, z0, uv[0], uv[3]}, {x1, y0, z0, uv[2], uv[3]},
                    {x1, y0, z1, uv[2], uv[1]}, {x0, y0, z1, uv[0], uv[1]});
                
                // +Z (передняя грань, нормаль (0,0,1))
                if (!isSolid(x, y, z + 1)) addFace(
                    {x0, y0, z1, uv[0], uv[1]}, {x1, y0, z1, uv[2], uv[1]},
                    {x1, y1, z1, uv[2], uv[3]}, {x0, y1, z1, uv[0], uv[3]});
                
                // -Z (задняя грань, нормаль (0,0,-1))
                if (!isSolid(x, y, z - 1)) addFace(
                    {x1, y0, z0, uv[2], uv[1]}, {x0, y0, z0, uv[0], uv[1]},
                    {x0, y1, z0, uv[0], uv[3]}, {x1, y1, z0, uv[2], uv[3]});
            }
}

/// @}
// ============================================================================

/** @name Интеграция с Axmol
 *  Функции создания графических объектов движка
 */
/// @{

/**
 * @brief Создаёт объект ax::Mesh из сырых вершин и индексов.
 * 
 * @param vertices Вектор вершин формата ChunkVertex (интерливинг)
 * @param indices Индексный буфер ax::IndexArray (триангуляция)
 * @param texture Указатель на текстуру-атлас для привязки к мешу
 * @return ax::Mesh* Указатель на созданный меш или nullptr при ошибке
 * 
 * @details
 * ## Механизм деинтерливинга:
 * Axmol API требует раздельные массивы атрибутов для Mesh::create(),
 * тогда как ChunkVertex хранит данные в интерливинговом формате.
 * Функция выполняет преобразование:
 * 
 * @code
 * Вход:  [x0,y0,z0,u0,v0, x1,y1,z1,u1,v1, ...]  (ChunkVertex[])
 * Выход: 
 *   positions[] = [x0,y0,z0, x1,y1,z1, ...]     // 3 float на вершину
 *   normals[]   = [0,1,0, 0,1,0, ...]           // заглушка для UNLIT
 *   uvs[]       = [u0,v0, u1,v1, ...]           // 2 float на вершину
 * @endcode
 * 
 * ## Что делает ax::Mesh::create():
 * 1. Создаёт VBO (Vertex Buffer Object) и IBO (Index Buffer Object)
 * 2. Рассчитывает AABB (Axis-Aligned Bounding Box) для Frustum Culling
 * 3. Копирует данные в VRAM и подготавливает MeshCommand для рендерера
 * 
 * @note Для UNLIT-материалов нормали-заглушки не влияют на визуализацию.
 * Если в будущем потребуется освещение — заменить на расчёт реальных нормалей.
 */
inline ax::Mesh* createMesh(const std::vector<ChunkVertex>& vertices,
                           const ax::IndexArray& indices,
                           ax::Texture2D* texture)
{
    if (vertices.empty() || indices.empty()) return nullptr;  ///< Пустые данные — меш не создаётся
    
    const size_t vertCount = vertices.size();
    
    // === Деинтерливинг: разделение атрибутов для Axmol API ===
    std::vector<float> positions(vertCount * 3);   ///< Массив позиций: [x0,y0,z0, x1,y1,z1, ...]
    std::vector<float> normals(vertCount * 3, 0.0f);  ///< Заглушка нормалей (для UNLIT не используется)
    std::vector<float> uvs(vertCount * 2);         ///< Массив UV: [u0,v0, u1,v1, ...]

    for (size_t i = 0; i < vertCount; ++i)
    {
        positions[i * 3 + 0] = vertices[i].x;
        positions[i * 3 + 1] = vertices[i].y;
        positions[i * 3 + 2] = vertices[i].z;
        
        normals[i * 3 + 1] = 1.0f;  ///< Заглушка: нормаль "вверх" (не влияет на UNLIT-шейдер)

        uvs[i * 2 + 0] = vertices[i].u;
        uvs[i * 2 + 1] = vertices[i].v;
    }

    // === Сборка меша через Axmol API ===
    // Движок автоматически: создаёт буферы, считает AABB, оптимизирует данные
    auto* mesh = ax::Mesh::create(positions, normals, uvs, indices);

    if (mesh && texture) {
        mesh->setTexture(texture);  ///< Прямая привязка диффузной текстуры к мешу
    }

    return mesh;
}

/// @}
// ============================================================================
