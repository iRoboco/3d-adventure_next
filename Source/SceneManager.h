/**
 * @file SceneManager.h
 * @brief Централизованный роутер сцен (production scene flow).
 *
 * Убирает разбросанные по проекту вызовы Director::replaceScene(...) и задаёт
 * единый управляемый поток переходов:
 *
 *   Splash → MainMenu → Game (new / from save) → MainMenu
 *
 * Все переходы используют единый стиль (TransitionFade), что упрощает поддержку
 * и обеспечивает консистентный UX. Класс полностью статический — состояние
 * мира живёт внутри самих сцен, а не в роутере.
 */
#pragma once

#include "axmol.h"

class SceneManager
{
public:
    /// @brief Заставка/загрузка — первая сцена приложения (без перехода).
    static void goToSplash();

    /// @brief Главное меню с живым 3D-превью.
    static void goToMainMenu();

    /// @brief Новая игра (свежий мир, спавн по умолчанию).
    static void startNewGame();

    /// @brief Загрузка сохранённой игры. Если сейва нет — стартует новая игра.
    static void loadGame();

    /// @brief Корректный выход из приложения.
    static void exitGame();

private:
    /// @brief Единое время кроссфейда между сценами (сек).
    static constexpr float kTransitionTime = 0.4f;
};
