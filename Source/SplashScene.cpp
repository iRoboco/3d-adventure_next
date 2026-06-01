#include "SplashScene.h"
#include "SceneManager.h"

using namespace ax;

ax::Scene* SplashScene::create()
{
    auto* scene = new (std::nothrow) SplashScene();
    if (scene && scene->init())
    {
        scene->autorelease();
        return scene;
    }
    AX_SAFE_DELETE(scene);
    return nullptr;
}

bool SplashScene::init()
{
    if (!Scene::init())
        return false;

    const auto visibleSize = Director::getInstance()->getVisibleSize();
    const Vec2 center      = visibleSize / 2.0f;

    // Тёмный фон-подложка на весь экран.
    auto bg = LayerColor::create(Color4B(8, 9, 14, 255));
    addChild(bg);

    // Логотип, если он есть в ресурсах; иначе — текстовый заголовок.
    Node* brand = nullptr;
    if (FileUtils::getInstance()->isFileExist("ui/logo.png"))
    {
        auto logo = Sprite::create("ui/logo.png");
        brand     = logo;
    }
    else
    {
        auto title = Label::createWithTTF("VOXEL ADVENTURE", "fonts/arial.ttf", 56);
        brand      = title;
    }

    if (brand)
    {
        brand->setPosition(center);
        brand->setOpacity(0);  // плавное проявление
        addChild(brand);

        // Fade in → пауза → fade out, затем переход в меню.
        brand->runAction(Sequence::create(FadeIn::create(0.6f), DelayTime::create(1.0f), FadeOut::create(0.4f), nullptr));
    }

    // Переход в меню по завершении всей последовательности заставки (~2.0 сек).
    // Привязываем к сцене, а не к brand: переход должен случиться даже без логотипа.
    runAction(Sequence::create(DelayTime::create(2.1f), CallFunc::create([this]() { proceed(); }), nullptr));

    return true;
}

void SplashScene::proceed()
{
    SceneManager::goToMainMenu();
}
