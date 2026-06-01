/**
 * @file SplashScene.h
 * @brief Стартовая заставка: короткий брендинг перед главным меню.
 *
 * Лёгкая сцена без тяжёлых систем (ChunkManager не создаётся), чтобы окно появилось
 * мгновенно. По завершении анимации передаёт управление в MainMenuScene через SceneManager.
 */
#pragma once

#include "axmol.h"

class SplashScene : public ax::Scene
{
public:
    static ax::Scene* create();
    bool init() override;

private:
    /// @brief Переход в главное меню (вызывается по окончании анимации заставки).
    void proceed();
};
