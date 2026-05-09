#include "GameScene.h"
#include "PerlinNoise.hpp"
#include <algorithm>
#include <thread>
#include <climits>

USING_NS_AX;

// =========================================================================
//  Фабричный метод
// =========================================================================
ax::Scene* GameScene::create()
{
    auto* ret = new (std::nothrow) GameScene();
    if (ret && ret->init())
    {
        ret->autorelease();
        return ret;
    }
    AX_SAFE_DELETE(ret);
    return nullptr;
}

// =========================================================================
//  Деструктор — ⚡ FIX: shutdown() только здесь
// =========================================================================
GameScene::~GameScene()
{
    // ⚡ FIX: Полная очистка только при уничтожении объекта.
    // onExit() вызывает pause(), который сохраняет данные.
    // shutdown() здесь гарантирует, что ресурсы освобождены
    // при replaceScene() или завершении приложения.
    _chunkMgr.shutdown();
}

// =========================================================================
//  init
// =========================================================================
bool GameScene::init()
{
    if (!Scene::init()) return false;

    auto glView = ax::Director::getInstance()->getGLView();
    float w = glView->getFrameSize().width;
    float h = glView->getFrameSize().height;
    float aspect = (h > 0.0f) ? (w / h) : 16.0f / 9.0f;
    _mainCamera = ax::Camera::createPerspective(60.0f, aspect, 0.1f, 1000.0f);
    AX_ASSERT(_mainCamera && "Failed to create perspective camera");

    // 🔧 FIX: Камера с USER1 должна рендерить только 3D-ноды.
    // Устанавливаем depth < 0 чтобы 3D-камера рендерилась ПЕРЕД 2D default-камерой.
    _mainCamera->setCameraFlag(ax::CameraFlag::USER1);
    _mainCamera->setDepth(-1);

    this->addChild(_mainCamera);

    // Конфигурация чанков
    ChunkManager::Config cfg;
    cfg.renderDistance         = 8;
    cfg.unloadMargin           = 3;
    cfg.workerThreadCount      = std::max(1u, std::thread::hardware_concurrency() / 2);
    cfg.maxGenerationsPerFrame = 2;
    cfg.maxUnloadsPerFrame     = 4;
    cfg.maxQueueSize           = 128;
    cfg.maxDirtyRebuildsPerFrame = 2;
    _chunkMgr.init(cfg);

    // Коллбеки
    _chunkMgr.setOnGenerate([](ChunkData& chunk) {
        const ChunkKey& key = chunk.getKey();
        auto basePos = ChunkManager::chunkToWorld(key);
        thread_local siv::PerlinNoise perlin(42);
        for (int x = 0; x < CHUNK_SIZE_X; ++x) {
            for (int z = 0; z < CHUNK_SIZE_Z; ++z) {
                float wx = basePos.x + x;
                float wz = basePos.z + z;
                float terrain = perlin.octave2D_01(wx * 0.02, wz * 0.02, 6) * 40.0f;
                float biome   = perlin.octave2D_01(wx * 0.005, wz * 0.005, 4) * 15.0f - 5.0f;
                int surfaceY = std::clamp(static_cast<int>(terrain + biome + 30), 1, CHUNK_SIZE_Y - 2);
                for (int y = 0; y <= surfaceY; ++y) {
                    //BlockId block = (y == surfaceY) ? 1 : (y > surfaceY - 4 ? 2 : 3);
                    BlockId block;
                    if (y == surfaceY)
                    {
                        // Верхний слой — трава
                        block = BLOCK_GRASS;
                    }
                    else if (y > surfaceY - 4)
                    {
                        // 4 слоя под травой — земля
                        block = BLOCK_DIRT;
                    }
                    else
                    {
                        // Глубже — камень
                        block = BLOCK_STONE;
                    }
                    chunk.setBlock(x, y, z, block);
                }
            }
        }
    });

    // 🔧 FIX: CameraFlag — маска USER1 на визуализируемые ноды
    _chunkMgr.setOnVisualize([this](ax::Node* node, const ChunkKey& key) {
        if (!node) return;
        int tag = ((key.x & 0xFFFF) << 16) | (key.z & 0xFFFF);
        node->setTag(tag);
        // 🔧 FIX: Маска USER1 чтобы 3D-камера рендерила этот нод
        node->setCameraMask(static_cast<int>(ax::CameraFlag::USER1));
        this->addChild(node);
    });
    _chunkMgr.setOnUnload([this](ax::Node* node, const ChunkKey& key) {
        if (node) {
            node->removeFromParentAndCleanup(true);
        }
    });

    // Создание контроллера
    _playerController = FirstPersonController::create(_mainCamera, 10.0f, 0.1f);
    AX_ASSERT(_playerController && "Failed to create FirstPersonController");
    _playerController->setChunkManager(&_chunkMgr);
    _playerController->setInitialPosition(ax::Vec3(0.0f, 60.0f, 0.0f));
    this->addChild(_playerController);

    _playerNode = _playerController;


    // Включаем обработку клавиатуры для перезапуска уровня
    auto listener          = ax::EventListenerKeyboard::create();
    listener->onKeyPressed = AX_CALLBACK_2(GameScene::onKeyPressed, this);
    _eventDispatcher->addEventListenerWithSceneGraphPriority(listener, this);

    scheduleUpdate();
    return true;
}

// =========================================================================
//  onEnter — ⚡ FIX: resume вместо повторного init
// =========================================================================
void GameScene::onEnter()
{
    Scene::onEnter();

    if (_playerController) {
        _playerController->setEnabled(true);
    }

    // ⚡ FIX: resume вместо повторного init.
    // Перезапускает воркеры, инициирует обновление чанков.
    // Если после shutdown() (полное уничтожение) — resume() ничего не сделает
    // (проверка _initialized внутри).
    _chunkMgr.resume();
}

// =========================================================================
//  onExit — ⚡ FIX: pause вместо shutdown
// =========================================================================
void GameScene::onExit()
{
    // ⚡ FIX: pause вместо shutdown.
    // При сворачивании приложения нужно ЗАМОРОЗИТЬ мир, а не УНИЧТОЖИТЬ его.
    // shutdown() вызывается только в деструкторе.
    _chunkMgr.pause();

    if (_playerController) {
        _playerController->setEnabled(false);
    }

    Scene::onExit();
}

// =========================================================================
//  update
// =========================================================================
void GameScene::update(float dt)
{
    Scene::update(dt);
    ax::Vec3 playerPos = _playerController ? _playerController->getPlayerPosition() : ax::Vec3::ZERO;
    _chunkMgr.update(playerPos);
}

ax::Vec3 GameScene::getPlayerPosition() const
{
    return _playerNode ? _playerNode->getPosition3D() : ax::Vec3::ZERO;
}

// =========================================================================
//  onKeyPressed — обработка нажатия F12 для перезапуска уровня
// =========================================================================
void GameScene::onKeyPressed(ax::EventKeyboard::KeyCode keyCode, ax::Event* event)
{
    // Перезапуск уровня по нажатию F12
    if (keyCode == ax::EventKeyboard::KeyCode::KEY_F12)
    {
        auto scene = GameScene::create();
        ax::Director::getInstance()->replaceScene(scene);
    }
}

