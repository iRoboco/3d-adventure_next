# 3D Adventure Next

<!-- TOC START -->
- [3D Adventure Next](#3d-adventure-next)
- [Описание проекта](#описание-проекта)
- [Ключевые возможности (с акцентом на Axmol)](#ключевые-возможности-с-акцентом-на-axmol)
- [Архитектурный обзор с привязкой к Axmol](#архитектурный-обзор-с-привязкой-к-axmol)
- [Подробное описание подсистем (с акцентом на Axmol)](#подробное-описание-подсистем-с-акцентом-на-axmol)
  - [1) Chunk Manager (Source/ChunkManager.h)](#1-chunk-manager-sourcechunkmanagerh)
    - [Роль и интеграция с Axmol](#роль-и-интеграция-с-axmol)
    - [Как используются возможности Axmol](#как-используются-возможности-axmol)
    - [Жизненный цикл и безопасность памяти](#жизненный-цикл-и-безопасность-памяти)
    - [Операции с большим количеством данных](#операции-с-большим-количеством-данных)
  - [2) Chunk Mesh Builder (Source/ChunkMeshBuilder.h)](#2-chunk-mesh-builder-sourcechunkmeshbuilderh)
    - [Роль и использование Axmol API](#роль-и-использование-axmol-api)
    - [Почему Axmol помогает здесь](#почему-axmol-помогает-здесь)
    - [Практические тонкости при работе с Axmol](#практические-тонкости-при-работе-с-axmol)
  - [3) First Person Controller (Source/FirstPersonController.h)](#3-first-person-controller-sourcefirstpersoncontrollerh)
    - [Axmol-специфические аспекты](#axmol-специфические-аспекты)
    - [Интеграция с камерой и сценой](#интеграция-с-камерой-и-сценой)
    - [Управление памятью и lifecycle](#управление-памятью-и-lifecycle)
  - [4) Voxel Collision Resolver (Source/VoxelCollisionResolver.h)](#4-voxel-collision-resolver-sourcevoxelcollisionresolverh)
    - [Интеграция с Axmol](#интеграция-с-axmol)
    - [Рекомендации по использованию](#рекомендации-по-использованию)
- [Практические советы по разработке и отладке (Axmol-centric)](#практические-советы-по-разработке-и-отладке-axmol-centric)
- [Интеграция и запуск](#интеграция-и-запуск)
- [Для разработчиков](#для-разработчиков)

<!-- TOC END -->

## Описание проекта
**3D Adventure Next** — воксельная игра в стиле Minecraft, реализованная на C++20 на движке **Axmol 2.11**. Проект ориентирован на производительный рендер воксельных миров, надёжную физику игрока и модульную архитектуру, максимально использующую возможности Axmol: граф-сцену (`ax::Node`), систему управления памятью (`ax::Object` / `autorelease`), `MeshRenderer`/`ax::Mesh`, `TextureCache` и `EventDispatcher`.

Код документирован в стиле Doxygen — легко генерировать локальную документацию и быстро ориентироваться по API.

---

## Ключевые возможности (с акцентом на Axmol)
- Асинхронная генерация чанков с пулом `std::thread`-воркеров и потокобезопасной очередью задач; результаты интегрируются в сцену через коллбек `_onVisualize`, который создаёт `ax::Node`/`ax::MeshRenderer` в главном потоке.
- Оптимизированная сборка мешей чанков (Face Culling, cross-chunk checks, триангуляция) с использованием `ax::Mesh::create()` для загрузки VBO/IBO в VRAM и расчёта AABB для фрум-каллинга движка.
- Компактный формат вершин (`ChunkVertex`: `x`, `y`, `z`, `u`, `v`) для UNLIT-рендеринга; деинтерливинг в `positions`/`uvs` перед вызовом `ax::Mesh::create()`.
- Sweep-based резолвер коллизий для капсулы игрока (sub-stepping, skin width, ground probe) — использует `ChunkManager::getBlockAtWorldPos()` (только чтение).
- First-Person контроллер с режимом FPS (гравитация, коллизии) и FreeFlight (noclip); обрабатывает ввод через `ax::EventListenerMouse`/`ax::EventListenerKeyboard` и планирует обновления через `scheduleUpdate()`.
- Защита от context-loss (проверки текстуры атласа через `ax::Director::getInstance()->getTextureCache()`) и корректная работа с `autorelease`/`retain` семантикой Axmol.

---

## Архитектурный обзор с привязкой к Axmol
Архитектура распределяет ответственность по подсистемам, максимально опираясь на примитивы Axmol:
- `ChunkManager` — жизненный цикл чанков, задачи генерации и интеграция визуализации. Создаваемые визуальные объекты — обычные `ax::Node` с `ax::MeshRenderer` (или `ax::Sprite`/`CustomCommand` в альтернативных режимах).
- `ChunkMeshBuilder` — чисто CPU-подсистема, генерирующая сырые `positions`/`uv`/`индексы`; затем вызывается `ax::Mesh::create()` и присваивается текстура (`ax::Texture2D`) через `mesh->setTexture()`.
- `FirstPersonController` — наследник `ax::Node`, использует event listeners и `scheduleUpdate()` для обновления логики в главном потоке. Позиция/вращение камеры синхронизируются с `ax::Camera`.
- `VoxelCollisionResolver` — однопоточный алгоритм движения и столкновений, опирающийся на `ChunkManager` (`getBlockAtWorldPos()`).

Коммуникация между подсистемами происходит через строго типизированные callback-и и `const`-ссылки; главное правило — любые обращения к графическим объектам и Axmol API выполняются только в главном потоке.

---

## Подробное описание подсистем (с акцентом на Axmol)

### 1) Chunk Manager (Source/ChunkManager.h)
Source: https://github.com/iRoboco/3d-adventure_next/blob/main/Source/ChunkManager.h

Роль и интеграция с Axmol:
- `ChunkManager` отвечает за постановку задач генерации, управление пулом воркеров и создание визуальных нод чанков в главном потоке.
- Для визуализации он создаёт `ax::Node`, добавляет `ax::MeshRenderer` с `ax::Mesh` (созданным через `ax::Mesh::create`) и привязывает `ax::Texture2D` (атлас) к мешу через `mesh->setTexture()`.
- Позиционирование визуального нода выполняется через transform ноды (позиция = `chunkToWorld(key)`).

Как используются возможности Axmol:
- `ax::TextureCache` / `ax::Director` — загрузка атласа выполняется через `Director::getInstance()->getTextureCache()->addImage("textures/terrain_atlas.png")`. Менеджер применяет параметры фильтрации (Sampler) для текстуры в соответствии с `Config.textureFilter`.
- `ax::Mesh::create()` автоматически создаёт VBO/IBO в VRAM и рассчитывает AABB → `MeshRenderer` получает готовый `MeshCommand` для движка.
- Visualize/Unload callbacks вызываются в главном потоке, где безопасно взаимодействовать с `ax::Scene`, `ax::Node::addChild()`/`ax::Node::removeFromParent()`.

Жизненный цикл и безопасность памяти:
- Visual nodes возвращаются как обычные `ax::Node*` (обычно `autorelease`): при добавлении в сцену они управляются сценографом. При выгрузке `ChunkManager` вызывает `_onUnload` для удаления нода (`removeFromParent()` и/или `release()` при необходимости).
- `ChunkManager` хранит `ChunkData` в `std::unique_ptr`, но визуальные объекты — в ax-терминах: их владение следует удерживать через `addChild()`/`retain()` если нужно сохранить за пределами сцены.

Операции с большим количеством данных:
- `processReadyChunks()` и `processDirtyChunks()` используют per-frame лимиты, чтобы не блокировать render/update pipeline Axmol.


### 2) Chunk Mesh Builder (Source/ChunkMeshBuilder.h)
Source: https://github.com/iRoboco/3d-adventure_next/blob/main/Source/ChunkMeshBuilder.h

Роль и использование Axmol API:
- Генерирует `std::vector<ChunkVertex>` и `ax::IndexArray` (Axmol helper) — затем `createMesh()` вызывает `ax::Mesh::create(positions, normals, uvs, indices)`.
- После создания меш привязывается `ax::Texture2D` с помощью `mesh->setTexture(texture)`. Это позволяет движку использовать стандартный pipeline (UNLIT материал) без написания кастомных шейдеров.

Почему Axmol помогает здесь:
- `ax::Mesh::create` обеспечивает перенос данных в VRAM и расчёт AABB, что ускоряет встраивание в рендер-пайплайн и даёт готовый `MeshCommand`.
- `MeshRenderer` и `CameraFlag` позволяют управлять порядком рендеринга и слоёв сцены (например, выделение подсветки через отдельный `MeshRenderer` с прозрачностью / `wireframe`).
- `ax::IndexArray` и `ax::Mesh` упрощают работу с индексами/вершинами и интеграцию с материалами движка.

Практические тонкости при работе с Axmol:
- Нормали в коде устанавливаются заглушкой (для UNLIT). При переходе на освещённый материал нужно либо рассчитывать нормали при создании меша, либо генерировать tangent/bitangent при необходимости PBR.
- Для обеспечения корректного отображения и очистки ресурсов убедитесь, что созданные `ax::Mesh`/`ax::Texture2D` остаются живыми: `Mesh` хранится внутри `MeshRenderer`/`Node` и удалится по логике управления памятью Axmol (`autorelease`/`retain`).


### 3) First Person Controller (Source/FirstPersonController.h)
Source: https://github.com/iRoboco/3d-adventure_next/blob/main/Source/FirstPersonController.h

Axmol-специфические аспекты:
- `FirstPersonController` наследуется от `ax::Node` и поэтому напрямую интегрируется в сценограф: можно `addChild(controller)` и управлять трансформом/видимостью.
- Создание через фабрику возвращает `autorelease`-объект: либо добавить в сцену, либо вызвать `retain()` если требуется вручную контролировать lifetime.
- Обработка ввода через `ax::EventListenerMouse` и `ax::EventListenerKeyboard`: keyboard listener зарегистрирован с fixed priority и должен удаляться вручную в `onExit()` (как реализовано в коде) — это важная деталь Axmol для предотвращения dangling callbacks.
- `scheduleUpdate()` используется для регистрации `update(float dt)` в главном цикле Axmol; это гарантирует, что все взаимодействия с `ax::Scene`/`ax::Node` выполняются в безопасном контексте.

Интеграция с камерой и сценой:
- Контроллер принимает `ax::Camera*` — синхронизирует её позицию/ориентацию. Для raycasting и взаимодействия с миром используется `cameraForward` и camera transform.
- В режиме FreeFlight контроллер освобождает физику, но остаётся `ax::Node` в сцене для совместимости с HUD/UI и внешними системами.

Управление памятью и lifecycle:
- Listener'ы мыши/клавиатуры и другие ресурсы очищаются в `onExit()`/деструкторе — это обязательный шаг при использовании Axmol, т.к. слушатели с fixed priority не удаляются автоматически движком.
- Вызовы к `ChunkManager` (`getBlockAtWorldPos()`) выполняются только для чтения и из главного потока — это предотвращает race conditions с визуализацией.

### 4) Voxel Collision Resolver (Source/VoxelCollisionResolver.h)
Source: https://github.com/iRoboco/3d-adventure_next/blob/main/Source/VoxelCollisionResolver.h

Интеграция с Axmol:
- Резолвер работает в главном потоке и обновляет позицию `PlayerCapsule` (`ax::Vec3`). После разрешения `FirstPersonController` синхронизирует значение с transform ноды и камерой.
- Для стабилизации и проверки состояний используется `ChunkManager::getBlockAtWorldPos()`, который безопасно возвращает `BLOCK_AIR` для незагруженных чанков — это позволяет избежать зависания контроллера при динамической загрузке сцены.

Рекомендации по использованию:
- Вызывать `resolve()` только в `update()` главного потока (что и сделано через `scheduleUpdate()` в контроллере).
- Не выполнять heavy queries к `ChunkManager` внутри резолвера — `ChunkManager` оптимизирован для чтения, но следует уважать per-frame лимиты при rebuild-ах мешей.

---

## Краткие примеры использования Axmol (код)
Ниже несколько минимальных примеров, показывающих создание меша из сырых данных, привязку текстуры и создание `MeshRenderer`/`Node`.

Пример 1 — создание `ax::Mesh` из массивов и привязка текстуры:

```cpp
// Препроцесс: positions, normals, uvs заполнены (float arrays), indices — ax::IndexArray
// textures/terrain_atlas.png загружен через TextureCache

auto* texture = ax::Director::getInstance()->getTextureCache()->addImage("textures/terrain_atlas.png");
if (!texture) {
    // fallback: белая 1x1 текстура
    auto* img = new ax::Image();
    unsigned char white[] = {255,255,255,255};
    img->initWithRawData(white, sizeof(white), 1, 1, 8);
    texture = ax::Director::getInstance()->getTextureCache()->addImage(img, "white_fallback");
    img->release();
}

// Создаём меш: Axmol перенесёт данные в VRAM и рассчитает AABB
ax::Mesh* mesh = ax::Mesh::create(positions, normals, uvs, indices);
if (mesh && texture) {
    mesh->setTexture(texture); // Привязываем атлас к мешу
}
```

Пример 2 — создание `Node` + `MeshRenderer` и добавление в сцену:

```cpp
// Создаём нод и компонент MeshRenderer, затем добавляем в сцену
ax::Node* chunkNode = ax::Node::create();
if (mesh) {
    auto* mr = ax::MeshRenderer::create(mesh);
    // Настройка флагов рендера (пример): включить wireframe для отладки
    // mr->setWireframe(true);

    chunkNode->addComponent(mr); // MeshRenderer хранится нодой (autorelease/retain по Axmol)
}

// Позиционируем чанк в мире (нижний угол чанка)
chunkNode->setPosition(chunkToWorld(key));

// Добавляем в сцену (владелец сценографа возьмёт ownership)
scene->addChild(chunkNode);
```

Примечания по памяти и lifecycle:
- `ax::Mesh::create` и `ax::Node::create` возвращают объекты, управляемые Axmol (`autorelease`). Если вы храните указатель вне сцены, используйте `retain()`/`release()`.
- Все обращения к объектам Axmol (создание нод, добавление компонентов, изменение трансформа) должны выполняться в главном потоке.
- Для обновления/пересоздания меша: удалите старый visual node через `removeFromParent()` и создайте новый node с `MeshRenderer` (ChunkManager использует этот подход в `processDirtyChunks()`).

Эти примеры — отправная точка. При необходимости добавлю пример deinterleaving из `ChunkVertex` → `positions`/`uvs` и построение `ax::IndexArray` из `uint16_t` индексов.

---

## Практические советы по разработке и отладке (Axmol-centric)
- Debug: используйте `MeshRenderer::setWireframe(true)` или создавайте вспомогательные `MeshRenderer` ноды для визуализации границ граней и AABB.
- Текстуры: загружайте через `Director::getInstance()->getTextureCache()` и тестируйте режимы фильтрации (`NEAREST` vs `MIPMAP`). В коде предусмотрен fallback (создание 1x1 белой `ax::Image`) при отсутствии атласа.
- Память: помните про `ax::Object`/`autorelease` семантику — если вы храните указатели на `ax::Node`/`ax::Mesh` вне сценографа, используйте `retain()`/`release()`.
- Event listeners: keyboard listener с fixed priority нужно удалять вручную (`onExit()`) — иначе возможны dangling callbacks после удаления ноды.

---

## Интеграция и запуск
Требования:
- Axmol Engine 2.11
- Компилятор с поддержкой C++20
- Ресурсы: текстурный атлас `textures/terrain_atlas.png`

Сборка и запуск:
- Настройте пути Axmol в CMake/IDE, соберите проект и запустите приложение. Откройте `GameScene` — она инициализирует `ChunkManager`, создаёт `FirstPersonController` и настраивает рейкастер.

---

## Для разработчиков
- Чтение исходников: `Source/GameScene.h` → `Source/ChunkManager.h` → `Source/ChunkMeshBuilder.h` → `Source/FirstPersonController.h` → `Source/VoxelCollisionResolver.h`.
- Doxygen: генерация документации из комментариев облегчит понимание API.
- Следите за per-frame лимитами и `maxQueueSize` при внесении изменений.

---

