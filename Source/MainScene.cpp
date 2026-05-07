#include "MainScene.h"
#include "Cube3D.h"
#include "physics3d/Physics3D.h"
#include "PerlinNoise.hpp"

using namespace ax;

static int s_sceneID = 1000;

bool MainScene::init() {

    if (!Scene::initWithPhysics()) { return false; }

    // Извлекаем ссылку на физический мир
    auto physicsWorld = this->getPhysics3DWorld();
    // Отладочный отрисовщик границ физики
    physicsWorld->setDebugDrawEnable(false);
    physicsWorld->setGravity(Vec3(0, -30.0f, 0));

    /////////////////////////////////////
    //
    // Создаем отдельную камеру для UI
    //
    auto winSize = _director->getWinSize();
    _uiCamera = Camera::createOrthographic(winSize.width, winSize.height, 1, 1000);
    _uiCamera->setCameraFlag(CameraFlag::USER1);
    _uiCamera->setPosition3D(Vec3(winSize.width / 2, winSize.height / 2, 500));
    _uiCamera->lookAt(Vec3(winSize.width / 2, winSize.height / 2, 0));
    _uiCamera->setDepth(2); // UI камера должна рендериться после основной
    this->addChild(_uiCamera, 10000); // Высокий z-order для UI камеры

    /////////////////////////////////////
    //
    // Создаем отдельный узел для UI элементов, который не будет наследовать трансформацию сцены
    //
    auto uiNode = Node::create();
    uiNode->setCameraMask((unsigned short)CameraFlag::USER1);
    this->addChild(uiNode, 9999);

    // Настраиваем информацию о размерах экрана
    screenSizeInfoSetup(uiNode);

    // Кнопки для поворота камеры
    auto rotateLeftBtn = MenuItemLabel::create(Label::createWithTTF("Rotate Left", "fonts/Marker Felt.ttf", 24),
        AX_CALLBACK_1(MainScene::rotateCubeLeft, this));
    rotateLeftBtn->setPosition(Vec2(100, 100));
    rotateLeftBtn->setCameraMask((unsigned short)CameraFlag::USER1);

    auto rotateRightBtn = MenuItemLabel::create(Label::createWithTTF("Rotate Right", "fonts/Marker Felt.ttf", 24),
        AX_CALLBACK_1(MainScene::rotateCubeRight, this));
    rotateRightBtn->setPosition(Vec2(250, 100));
    rotateRightBtn->setCameraMask((unsigned short)CameraFlag::USER1);

    auto menu = Menu::create(rotateLeftBtn, rotateRightBtn, nullptr);
    menu->setPosition(Vec2::ZERO);
    menu->setCameraMask((unsigned short)CameraFlag::USER1);
    uiNode->addChild(menu);

    // Получаем камеру по умолчанию и настраиваем её
    auto defaultCamera = this->getDefaultCamera();
    defaultCamera->setPosition3D(Vec3(0, 500, 500));
    defaultCamera->lookAt(Vec3(0, 0, 0));
    defaultCamera->setDepth(1);                 // Основная камера рендерится первой
    defaultCamera->setFarPlane(10000.0f);       // Дальний край видимости объектов

    /////////////////////////////////////
    // 
    // Создаем и настраиваем контроллер от первого лица
    //
    FirstPersonController* fpController = FirstPersonController::create(defaultCamera);
    fpController->setPosition3D(Vec3(0, 500, 500));
    fpController->setMoveSpeed(200.0f);         // Скорость перемещения, по-умолчанию 100.0f
    fpController->setMouseSensitivity(0.2f);    // Чувствительность мыши, по-умолчанию 0.1f
    fpController->setFreeFlightMode(false);
    this->addChild(fpController);

    /////////////////////////////////////
    // 
    // Создаем небо (Skybox) с текстурой куба
    //
    _textureCube = TextureCube::create(
        "envmap_miramar/miramar_lf.tga", "envmap_miramar/miramar_rt.tga",
        "envmap_miramar/miramar_up.tga", "envmap_miramar/miramar_dn.tga",
        "envmap_miramar/miramar_ft.tga", "envmap_miramar/miramar_bk.tga");

    _skyBox = Skybox::create();
    _skyBox->setTexture(_textureCube);
    _skyBox->setCameraMask((unsigned short)CameraFlag::DEFAULT);
    addChild(_skyBox);

    /////////////////////////////////////
    //
    // Создаем Куб из объемной модели
    //
    // auto rigidDes = Physics3DRigidBodyDes();
    // rigidDes.mass = 10.0f;  // Установите массу, например, 10.0
    // rigidDes.shape = Physics3DShape::createBox(Vec3(64 * 2, 64 * 2, 64 * 2));
    // _cubeMesh = ax::PhysicsMeshRenderer::create("models/cube.obj"sv, &rigidDes); // с физикой

    // //_cubeMesh = MeshRenderer::create("models/cube.obj"sv); // без физики
    // _cubeMesh->setTexture("textures/grass.png"sv);
    // //_cubeMesh->setPosition3D(Vec3(_visibleSize.width / 2 + _origin.x, _visibleSize.height / 2 + _origin.y, 0));
    // _cubeMesh->setPosition3D(Vec3(0, 600, 0));
    // _cubeMesh->setScale(64.0f);
    // addChild(_cubeMesh);

    // auto rotate_action = RotateBy::create(5.0f, Vec3(30.0f, 45.0f, 15.0f));
    // _cubeMesh->runAction(RepeatForever::create(rotate_action));

    /////////////////////////////////////
    //
    // Создаем куб из плоских мешей и 6ти разных текстур
    //
    // std::string meshFile = { "models/plane.obj"s }; // Плоский меш

    // std::array<std::string, 6> textureFiles = {
    //     "textures/front.png", "textures/back.png",
    //     "textures/left.png", "textures/right.png",
    //     "textures/top.png", "textures/bottom.png"
    // };

    // Cube3D* cube = Cube3D::create(textureFiles, 64.0f);
    // if (cube) {
    //     // Устанавливаем позицию куба и добавляем его на сцену
    //     cube->setPosition3D(Vec3(0, 400, 0));
    //     cube->setRotation3D(Vec3(0, 15, 0));
    //     this->addChild(cube);
    //     // Добавляем физическое тело для второго куба (статическое)
    //     auto staticRigidDes = Physics3DRigidBodyDes();
    //     staticRigidDes.mass = 25.0f; // Масса 0 означает статическое тело
    //     staticRigidDes.shape = Physics3DShape::createBox(Vec3(64 * 2, 64 * 2, 64 * 2));
    //     // Устанавливаем трансформацию, соответствующую позиции и повороту куба
    //     Mat4 transform;
    //     transform.translate(Vec3(0, -100, 0));
    //     // Создаем ось Y (0, 1, 0) и угол 15 градусов в радианах
    //     transform.rotate(Vec3(0, 1, 0), AX_DEGREES_TO_RADIANS(15));
    //     staticRigidDes.originalTransform = transform;

    //     // Создаем физическое тело и добавляем его в мир
    //     auto rigidBody = Physics3DRigidBody::create(&staticRigidDes);
    //     auto component = Physics3DComponent::create(rigidBody);
    //     cube->addComponent(component);

    //     auto rotate_action = RotateBy::create(2.0f, Vec3(10.0f, -30.0f, 15.0f));
    //     cube->runAction(RepeatForever::create(rotate_action));
    // }

    //createPlatform(64.0f, 0.0f); // Размер куба 64, отступ между кубами 
    createCubeTerrain(64.0f, 0.0f);

    // Запускаем планировщик основного цикла
    scheduleUpdate();

    return true;
}

void MainScene::createCubeTerrain(float cubeSize, float spacing) {
    // Размеры платформы
    const int gridWidth = 30;
    const int gridHeight = 30;

    // Инициализируем генератор шума Перлина
    siv::PerlinNoise perlin;
    perlin.reseed(std::random_device{}()); // Случайное зерно для генерации

    // Вычисляем общий размер куба с учетом отступа
    float totalSize = cubeSize + spacing;

    // Вычисляем начальную позицию, чтобы платформа была центрирована
    float startX = -(gridWidth * totalSize) / 2.0f;
    float startZ = -(gridHeight * totalSize) / 2.0f;
    float baseY = 100.0f; // Базовая высота платформы

    // Создаем статическое описание для физики (одно на все кубы платформы)
    auto staticRigidDes = Physics3DRigidBodyDes();
    staticRigidDes.mass = 0.0f; // Масса 0 означает статическое тело
    staticRigidDes.shape = Physics3DShape::createBox(Vec3(cubeSize, cubeSize, cubeSize));

    // Создаем родительский узел для всей платформы
    auto platformNode = Node::create();
    addChild(platformNode);

    // Масштаб для шума Перлина (меньше значение = более плавные изменения)
    const float noiseScale = 0.1f;

    // Создаем кубы в сетке
    for (int x = 0; x < gridWidth; x++) {
        for (int z = 0; z < gridHeight; z++) {
            // Вычисляем позицию текущего куба
            float posX = startX + x * totalSize;
            float posZ = startZ + z * totalSize;

            // Генерируем высоту с помощью шума Перлина (от 1 до 5 кубов)
            float noiseValue = perlin.noise2D(x * noiseScale, z * noiseScale);
            int height = static_cast<int>(1 + (noiseValue + 0.5f) * 5.0f); // Преобразуем [-1,1] в [1,5]

            // Создаем столбец кубов для текущей позиции
            for (int yLevel = 0; yLevel < height; yLevel++) {
                // Создаем куб с физикой используя программный меш
                auto cubeMesh = createPhysicsCube(&staticRigidDes, cubeSize / 2.0f);
                
                if (!cubeMesh) {
                    AXLOGE("Не удалось создать программный куб");
                    continue;
                }
                
                // Выбираем текстуру в зависимости от высоты
                if (yLevel == height - 1) {
                    // Верхний куб - трава
                    cubeMesh->setTexture("textures/grass.png"sv);
                } else if (yLevel == 0) {
                    // Нижний куб - камень
                    cubeMesh->setTexture("textures/grass.png"sv);
                } else {
                    // Средние кубы - земля
                    cubeMesh->setTexture("textures/grass.png"sv);
                }

                // Включаем отсечение задних граней для оптимизации рендеринга
                for (auto& mesh : cubeMesh->getMeshes()) {
                    mesh->getMaterial()->getStateBlock().setCullFace(true);
                    mesh->getMaterial()->getStateBlock().setCullFaceSide(ax::backend::CullMode::BACK);
                }

                // Устанавливаем позицию куба
                float posY = baseY + yLevel * cubeSize;
                cubeMesh->setPosition3D(Vec3(posX, posY, posZ));
                cubeMesh->getPhysicsObj()->setMask(1000);

                // Добавляем куб на платформу
                platformNode->addChild(cubeMesh);
            }
        }
    }
}


void MainScene::createPlatform(float cubeSize, float spacing) {
    // Размеры платформы
    const int gridWidth = 30;
    const int gridHeight = 30;

    // Вычисляем общий размер куба с учетом отступа
    float totalSize = cubeSize + spacing;

    // Вычисляем начальную позицию, чтобы платформа была центрирована
    float startX = -(gridWidth * totalSize) / 2.0f;
    float startZ = -(gridHeight * totalSize) / 2.0f;
    float y = 100.0f; // Позиция по Y (высота платформы)

    // Создаем статическое описание для физики (одно на все кубы платформы)
    auto staticRigidDes = Physics3DRigidBodyDes();
    staticRigidDes.mass = 0.0f; // Масса 0 означает статическое тело
    staticRigidDes.shape = Physics3DShape::createBox(Vec3(cubeSize, cubeSize, cubeSize));

    // Создаем родительский узел для всей платформы
    auto platformNode = Node::create();
    addChild(platformNode);

    // Создаем кубы в сетке
    for (int x = 0; x < gridWidth; x++) {
        for (int z = 0; z < gridHeight; z++) {
            // Вычисляем позицию текущего куба
            float posX = startX + x * totalSize;
            float posZ = startZ + z * totalSize;

            // Создаем куб с физикой
            auto cubeMesh = ax::PhysicsMeshRenderer::create("models/cube.obj"sv, &staticRigidDes);
            cubeMesh->setTexture("textures/grass.png"sv);
            cubeMesh->setPosition3D(Vec3(posX, y, posZ));
            cubeMesh->setScale(cubeSize / 2);
            cubeMesh->getPhysicsObj()->setMask(1000);
            // Добавляем куб на платформу
            platformNode->addChild(cubeMesh);
        }
    }
}

// Методы поворота для работы с камерой по умолчанию
void MainScene::rotateCubeLeft(ax::Object* sender) {
    auto camera = this->getDefaultCamera();
    auto currentRotation = camera->getRotation3D();
    currentRotation.add(0.0f, 5.0f, 0.0f); // Поворот на 5 градусов влево вокруг оси Y
    camera->setRotation3D(currentRotation);
}

void MainScene::rotateCubeRight(ax::Object* sender) {
    auto camera = this->getDefaultCamera();
    auto currentRotation = camera->getRotation3D();
    currentRotation.add(0.0f, -5.0f, 0.0f); // Поворот на 5 градусов вправо вокруг оси Y
    camera->setRotation3D(currentRotation);
}

void MainScene::update(float delta) {
    switch (_gameState) {
    case GameState::init:
    {
        _gameState = GameState::update;
        break;
    }

    case GameState::update:
    {
        // Обновляем отладочную информацию, если метка существует
        if (_debugInfoLabel) {
            _debugInfoLabel->setString(getDebugInfoString());
        }

        break;
    }

    case GameState::pause:
    {
        /////////////////////////////
        // Add your codes below...like....
        //
        // anyPauseStuff()
        break;
    }

    case GameState::menu1:
    {  /////////////////////////////
        // Add your codes below...like....
        //
        // UpdateMenu1();
        break;
    }

    case GameState::menu2:
    {  /////////////////////////////
        // Add your codes below...like....
        //
        // UpdateMenu2();
        break;
    }

    case GameState::end:
    {  /////////////////////////////
        // Add your codes below...like....
        //
        // CleanUpMyCrap();
        menuCloseCallback(this);
        break;
    }

    }  // switch
}

MainScene::MainScene() : _debugInfoLabel(nullptr) {
    _sceneID = ++s_sceneID;
    AXLOGD("Scene: ctor: #{}", _sceneID);
}

MainScene::~MainScene() {
    AXLOGD("~Scene: dtor: #{}", _sceneID);
    _sceneID = -1;
}

void MainScene::menuCloseCallback(ax::Object* sender) {
    // Close the axmol game scene and quit the application
    _director->end();

    /*To navigate back to native iOS screen(if present) without quitting the application  ,do not use
     * _director->end() as given above,instead trigger a custom event created in RootViewController.mm
     * as below*/

     // EventCustom customEndEvent("game_scene_close_event");
     //_eventDispatcher->dispatchEvent(&customEndEvent);
}

// Метод для формирования строки с отладочной информацией
std::string MainScene::getDebugInfoString() const {
    auto camera = getDefaultCamera();
    auto cameraPos = camera->getPosition3D();
    auto cameraRot = camera->getRotation3D();

    return "Visible Size: " + std::to_string((int)_visibleSize.width) + "x" + std::to_string((int)_visibleSize.height) + "\n" +
        "Origin: " + std::to_string((int)_origin.x) + ", " + std::to_string((int)_origin.y) + "\n" +
        "SafeArea: " + std::to_string((int)_safeArea.size.width) + "x" + std::to_string((int)_safeArea.size.height) + "\n" +
        "SafeOrigin: " + std::to_string((int)_safeOrigin.x) + ", " + std::to_string((int)_safeOrigin.y) + "\n" +
        "Camera Position: " + std::to_string((int)cameraPos.x) + ", " + std::to_string((int)cameraPos.y) + ", " + std::to_string((int)cameraPos.z) + "\n" +
        "Camera Rotation: (Pitch/X) " + std::to_string((int)cameraRot.x) + ", (Yaw/Y) " + std::to_string((int)cameraRot.y) + ", (Roll/Z) " + std::to_string((int)cameraRot.z);
}

void MainScene::screenSizeInfoSetup(ax::Node* sender) {
    // Переменные размерностей экрана
    _visibleSize = _director->getVisibleSize();
    _origin = _director->getVisibleOrigin();
    _safeArea = _director->getSafeAreaRect();
    _safeOrigin = _safeArea.origin;

    // Получаем строку с отладочной информацией
    std::string debugInfo = getDebugInfoString();

    // Создаем Label
    _debugInfoLabel = Label::createWithTTF(debugInfo, "fonts/Arial.ttf", 14);
    if (_debugInfoLabel == nullptr) {
        AXLOGD("Failed to create label with TTF 'fonts/Aria.ttf'");
    }
    else {
        // Позиционируем в левом верхнем углу (с учетом origin)
        _debugInfoLabel->setAnchorPoint(Vec2(0, 1)); // Anchor: лево-верх
        _debugInfoLabel->setPosition(_origin.x + 10, _origin.y + _visibleSize.height - 10); // Отступ 10 пикселей от края

        // Настраиваем цвет и прозрачность (опционально)
        _debugInfoLabel->setTextColor(Color4B::WHITE);
        _debugInfoLabel->enableShadow(Color4B::BLACK, Size(1, -1)); // Тень для лучшей читаемости

        // Добавляем в слой и явно устанавливаем маску камеры
        sender->addChild(_debugInfoLabel);
        _debugInfoLabel->setCameraMask((unsigned short)CameraFlag::USER1);
    }
}
