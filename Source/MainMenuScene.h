/**
 * @file MainMenuScene.h
 * @brief Главное меню с живым 3D-превью воксельного мира на фоне.
 *
 * Архитектура:
 * - Переиспользует ChunkManager для асинхронной генерации и отрисовки фонового мира.
 * - Орбитальная камера совершает плавный кинематографичный облёт ландшафта.
 * - Поверх 3D рисуется минималистичное меню на встроенном в Axmol Dear ImGui
 *   (render loop регистрируется в ImGuiPresenter на время жизни сцены).
 * - Lifecycle: pause/resume чанков при сворачивании, shutdown при уничтожении.
 *
 * @note Меню и настройки рисуются в onDrawMenu()/onDrawSettings(); навигация и выходы
 *       делегируются SceneManager — сама сцена не вызывает replaceScene напрямую.
 */
#pragma once

#include "axmol.h"
#include "ChunkManager.h"

class MainMenuScene : public ax::Scene
{
public:
    static MainMenuScene* create();
    bool init() override;
    ~MainMenuScene() override;

protected:
    void onEnter() override;
    void onExit() override;

private:
    void update(float dt) override;

    // === Настройка подсистем ===
    void setupPreviewWorld();                       ///< Камера, небо, ChunkManager для фона
    void setupChunkManager();                       ///< Конфиг и коллбеки менеджера чанков
    void generateMenuTerrain(ChunkData& chunk);     ///< Упрощённый генератор холмов для превью

    // === ImGui render loops ===
    void onDrawMenu();      ///< Основное меню (Новая игра / Продолжить / Настройки / Выход)
    void onDrawSettings();  ///< Панель настроек (дальность прорисовки, чувствительность мыши)

    // === 3D-превью ===
    ChunkManager _chunkMgr;
    ax::Camera* _orbitCamera = nullptr;
    ax::Vec3 _worldCenter{8.0f, 34.0f, 8.0f};  ///< Точка, вокруг которой облётывает камера

    // === Параметры орбиты ===
    float _orbitAngle  = 0.0f;    ///< Текущий угол облёта (рад)
    float _orbitRadius = 78.0f;   ///< Горизонтальный радиус орбиты
    float _orbitHeight = 95.0f;   ///< Высота камеры (вид сверху-сбоку)
    float _orbitSpeed  = 0.12f;   ///< Угловая скорость (рад/сек)

    // === Состояние UI ===
    bool _showSettings = false;   ///< Открыта ли панель настроек вместо главного меню
};
