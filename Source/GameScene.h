#pragma once
#include "axmol.h"
#include "ChunkManager.h"
#include "FirstPersonController.h"

/**
@brief Основная игровая сцена (Gameplay Scene)
Архитектурные принципы (Steam-ready):
- Инкапсуляция всех игровых систем внутри Scene.
- Безопасный lifecycle: init -> update -> pause/resume -> destructor.
- Разделение графа сцены: камера, игрок, чанки, будущий HUD/UI.
- Потокобезопасная интеграция через коллбеки и lazy-инициализацию.
*/
class GameScene : public ax::Scene
{
public:
    static ax::Scene* create();
    bool init() override;

    // ⚡ FIX: Деструктор для корректного shutdown() ChunkManager
    // Вызывается только при полном уничтожении сцены (replaceScene, quit),
    // но НЕ при сворачивании приложения.
    ~GameScene() override;

    // Установка внешней ссылки на нод игрока (для HUD, инвентаря, сетевых систем)
    void setPlayerNode(ax::Node* player) { _playerNode = player; }
    ax::Vec3 getPlayerPosition() const;

protected:
    // Axmol вызывает эти методы при сворачивании/разворачивании окна
    void onEnter() override;
    void onExit() override;

private:
    void update(float dt) override;

    // Ядро генерации мира
    ChunkManager _chunkMgr;
    // Управление камерой и физикой
    FirstPersonController* _playerController = nullptr;
    ax::Camera*            _mainCamera = nullptr;
    // Абстрактная ссылка для внешних систем (UI, звук, сеть)
    ax::Node*              _playerNode = nullptr;
};

