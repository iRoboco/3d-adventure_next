#include "SceneManager.h"
#include "SplashScene.h"
#include "MainMenuScene.h"
#include "GameScene.h"
#include "SaveGameService.h"

using namespace ax;

void SceneManager::goToSplash()
{
    // Первая сцена — без перехода (нечему делать кроссфейд).
    Director::getInstance()->replaceScene(SplashScene::create());
}

void SceneManager::goToMainMenu()
{
    auto* scene = MainMenuScene::create();
    Director::getInstance()->replaceScene(TransitionFade::create(kTransitionTime, scene));
}

void SceneManager::startNewGame()
{
    auto* scene = GameScene::create();
    Director::getInstance()->replaceScene(TransitionFade::create(kTransitionTime, scene));
}

void SceneManager::loadGame()
{
    // Нет файла сохранения → деградируем к новой игре, чтобы кнопка всегда была безопасной.
    if (!SaveGameService::hasSave())
    {
        startNewGame();
        return;
    }

    auto* scene = GameScene::createFromSave();
    Director::getInstance()->replaceScene(TransitionFade::create(kTransitionTime, scene));
}

void SceneManager::exitGame()
{
    Director::getInstance()->end();
}
