#ifndef __MAIN_SCENE_H__
#define __MAIN_SCENE_H__
 
#include "axmol.h"
#include "FirstPersonController.h"

class MainScene : public ax::Scene {
public:
    MainScene();
    ~MainScene() override;

    bool init() override;
    void update(float delta) override;

    // a selector callback
    void menuCloseCallback(ax::Object* sender);

    // Методы для поворота камеры (оставлены для обратной совместимости с UI)
    void rotateCubeLeft(ax::Object* sender);
    void rotateCubeRight(ax::Object* sender);

    void createCubeTerrain(float cubeSize, float spacing);

    void createPlatform(float cubeSize, float spacing);

  private:
    enum class GameState {
        init = 0,
        update,
        pause,
        end,
        menu1,
        menu2,
    };

    void screenSizeInfoSetup(ax::Node* sender);

    GameState _gameState = GameState::init;
    int _sceneID = 0;

    // Метод для формирования строки debug-информации
    std::string getDebugInfoString() const;
    ax::Label* _debugInfoLabel;
    ax::Size _visibleSize;
    ax::Vec2 _origin;
    ax::Rect _safeArea;
    ax::Vec2 _safeOrigin;

    ax::Mesh* _flatMesh = nullptr;
    ax::MeshRenderer* _cubeMesh = nullptr;
    ax::TextureCube* _textureCube = nullptr;
    ax::Skybox* _skyBox = nullptr;
    ax::Camera* _uiCamera = nullptr; // Камера для UI элементов
};

#endif  // __MAIN_SCENE_H__
