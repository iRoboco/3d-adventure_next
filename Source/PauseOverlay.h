/**
 * @file PauseOverlay.h
 * @brief Оверлей паузы поверх GameScene (НЕ отдельная сцена).
 *
 * Production-подход: пауза реализована как слой поверх живой игровой сцены, а не
 * как замена сцены. Благодаря этому ChunkManager, рендерер и состояние мира НЕ
 * уничтожаются — возобновление мгновенно и без перезагрузки чанков.
 *
 * Слой состоит из:
 * - затемняющей подложки (LayerColor) поверх 3D-сцены;
 * - меню паузы на Dear ImGui (Продолжить / Сохранить / Сохранить и выйти),
 *   которое регистрируется как render loop на сцене-владельце.
 */
#pragma once

#include "axmol.h"

class GameScene;

class PauseOverlay : public ax::LayerColor
{
public:
    /// @brief Создаёт оверлей, привязанный к игровой сцене-владельцу.
    static PauseOverlay* create(GameScene* owner);

    bool initWithOwner(GameScene* owner);

protected:
    void onExit() override;  ///< Снимает ImGui render loop при удалении из сцены

private:
    void onDrawPauseMenu();  ///< ImGui-отрисовка меню паузы

    GameScene* _owner   = nullptr;
    bool _loopRegistered = false;  ///< Зарегистрирован ли ещё render loop (для идемпотентного снятия)
};
