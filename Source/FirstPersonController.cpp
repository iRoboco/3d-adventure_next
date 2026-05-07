#include "FirstPersonController.h" 

using namespace ax;

FirstPersonController::FirstPersonController()
    : _camera(nullptr)
    , _moveSpeed(100.0f)
    , _mouseSensitivity(0.1f)
    , _enabled(true)
    , _freeFlightMode(false)
    , _isLeftMousePressed(false)
    , _lastMousePos(Vec2::ZERO)
    , _keyW(false)
    , _keyA(false)
    , _keyS(false)
    , _keyD(false)
    , _keySpace(false)
    , _mouseListener(nullptr)
    , _keyboardListener(nullptr)
    , _physicsBody(nullptr)          // Инициализация физического тела
    , _isGrounded(false)             // Флаг для проверки нахождения на земле
    , _gravity(-300.0f)              // Гравитация, независимо от общей гравитации мира
    , _jumpForce(250.0f)             // Сила прыжка
    , _velocity(Vec3::ZERO)          // Начальная скорость
{}

FirstPersonController::~FirstPersonController() {
    // Удаляем слушателей событий, если они были созданы
    if (_mouseListener) {
        _eventDispatcher->removeEventListener(_mouseListener);
    }

    if (_keyboardListener) {
        _eventDispatcher->removeEventListener(_keyboardListener);
    }
}

FirstPersonController* FirstPersonController::create(Camera* camera, float moveSpeed, float mouseSensitivity) {
    auto controller = new (std::nothrow) FirstPersonController();
    if (controller && controller->init(camera, moveSpeed, mouseSensitivity)) {
        controller->autorelease();
        return controller;
    }
    AX_SAFE_DELETE(controller);
    return nullptr;
}

bool FirstPersonController::init(Camera* camera, float moveSpeed, float mouseSensitivity) {
    if (!Node::init()) { return false; }

    if (camera) { _camera = camera; }
    else { return false; }
    AXLOGD("Camera FOV: {}", _camera->getFOV());
    _moveSpeed = moveSpeed;
    _mouseSensitivity = mouseSensitivity;

    // Создаем физическое тело (капсула для персонажа)
    auto rigidDes = Physics3DRigidBodyDes();
    rigidDes.mass = 50.0f;
    rigidDes.shape = Physics3DShape::createCapsule(25.0f, 75.0f);
    _physicsBody = Physics3DRigidBody::create(&rigidDes);
    _physicsComponent = Physics3DComponent::create(_physicsBody);
    _physicsComponent->setName("Body");
    _physicsComponent->retain();
    this->addComponent(_physicsComponent);

    // Фиксируем вращение тела
    _physicsBody->setAngularFactor(Vec3::ZERO);
    // Разрешаем движение по всем осям
    _physicsBody->setLinearFactor(Vec3(1, 1, 1));

    // Определяем нахождение на земле
    _physicsBody->setCollisionCallback([this](const Physics3DCollisionInfo& ci) {
        if (!ci.collisionPointList.empty())
            if (ci.objA->getMask() != 0) {
                // AXLOGD("Collision detected objA mask {} objB mask {}", ci.objA->getMask(), ci.objB->getMask());
                // AXLOGD("PointList has {} entries", ci.collisionPointList.size());
                // auto lPoA = ci.collisionPointList[0].localPositionOnA;
                // AXLOGD("lPoA {}, {}, {}", lPoA.x, lPoA.y, lPoA.z);
                // auto wPoA = ci.collisionPointList[0].worldPositionOnA;
                // AXLOGD("wPoA {}, {}, {}", wPoA.x, wPoA.y, wPoA.z);
                // auto lPoB = ci.collisionPointList[0].localPositionOnB;
                // AXLOGD("lPoB {}, {}, {}", lPoB.x, lPoB.y, lPoB.z);
                // auto wPoB = ci.collisionPointList[0].worldPositionOnB;
                // AXLOGD("wPoB {}, {}, {}", wPoB.x, wPoB.y, wPoB.z);
                // auto wNoB = ci.collisionPointList[0].worldNormalOnB;
                // AXLOGD("wNoB {}, {}, {}", wNoB.x, wNoB.y, wNoB.z);

                // Проверяем, что столкновение произошло с землей или другим объектом
                // Если нормаль направлена вверх (персонаж на земле) и маска 1000 (земля)
                if (ci.collisionPointList[0].worldNormalOnB.y > 0.9f && ci.objB->getMask() == 1000) {
                    _isGrounded = true;
                    _velocity.y = 0; // Сбрасываем вертикальную скорость
                    //AXLOGD("Grounded!");
                }
            }
        });

    setupEventListeners();

    // Включаем планировщик обновления
    scheduleUpdate();

    return true;
}

void FirstPersonController::setEnabled(bool enabled) {
    _enabled = enabled;

    // Если контроллер отключен, сбрасываем все состояния клавиш
    if (!_enabled) {
        _keyW = false;
        _keyA = false;
        _keyS = false;
        _keyD = false;
        _keySpace = false;
        _isLeftMousePressed = false;

    }
}

void FirstPersonController::setFreeFlightMode(bool enabled) {
    _freeFlightMode = enabled;

    auto glView = static_cast<GLViewImpl*>(ax::Director::getInstance()->getGLView());
    auto glfwWindow = glView->getWindow();
    if (_freeFlightMode) {
        _physicsBody->setActive(false);
        // В режиме полета показываем курсор мыши
        glfwSetInputMode(glfwWindow, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        // Отключаем физическое тело от ноды игрока
        if (this->getComponent("Body")) this->removeComponent(_physicsComponent);
    }
    else {
        // Корректировка позиции мыши после перехода в изолированный режим
        double mouse_x, mouse_y;
        //glfwGetCursorPos(glfwWindow, &mouse_x, &mouse_y);                   // DEBUG
        //AXLOGD("glfw_mouse_x = {} glfw_mouse_y = {}", mouse_x, mouse_y);    // DEBUG

        // Следующая манипуляция связана с тем, что GL отсчитывает координаты от верхнего левого угла,
        // а Axmol от нижнего левого, соответственно 720 это высота окна, можно получить от Director если понадобится
        glfwSetCursorPos(glfwWindow, 0 + _lastMousePos.x, 720 - _lastMousePos.y);
        glfwGetCursorPos(glfwWindow, &mouse_x, &mouse_y);
        //AXLOGD("glfw_mouse_x = {} glfw_mouse_y = {}", mouse_x, mouse_y);    // DEBUG

        // В режиме FPS - скрывем
        glfwSetInputMode(glfwWindow, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

        // Подключаем физическое тело к ноде игрока
        if (!this->getComponent("Body") && _physicsComponent) this->addComponent(_physicsComponent);
    }
}

void FirstPersonController::toggleFlightMode() {
    setFreeFlightMode(!_freeFlightMode);
    AXLOGD("Flight mode is {}", _freeFlightMode ? "ENABLED" : "DISAMLED");
}

Vec3 FirstPersonController::getForwardVector() const {
    // Стандартное направление "вперед"
    if (!_camera) { return Vec3(0, 0, -1); }

    // Получаем угол поворота камеры вокруг оси Y (yaw) и X (pitch)
    Vec3 rotation = _camera->getRotation3D();
    float yawRadians = AX_DEGREES_TO_RADIANS(rotation.y);
    float pitchRadians = AX_DEGREES_TO_RADIANS(rotation.x);

    // Вычисляем вектор направления вперед на основе угла поворота
    Vec3 forward;
    forward.x = -sinf(yawRadians);
    forward.z = -cosf(yawRadians);

    if (_freeFlightMode) {
        forward.x *= cosf(pitchRadians);    // Учитываем наклон вперед/назад
        forward.y = sinf(pitchRadians);     // Вертикальная компонента
        forward.z *= cosf(pitchRadians);
    }
    else {
        forward.y = 0.0f;                   // Горизонтальное движение
    }
    forward.normalize();

    return forward;
}

Vec3 FirstPersonController::getRightVector() const {
    // Стандартное направление "вправо"
    if (!_camera) { return Vec3(1, 0, 0); }

    // Получаем угол поворота камеры вокруг оси Y (yaw)
    float yawRadians = AX_DEGREES_TO_RADIANS(_camera->getRotation3D().y);

    // Вычисляем вектор направления вправо на основе угла поворота
    Vec3 right;
    right.x = cosf(yawRadians);
    right.y = 0.0f; // Сохраняем движение в горизонтальной плоскости
    right.z = -sinf(yawRadians);
    right.normalize();

    return right;
}

void FirstPersonController::update(float delta) {
    // Обновление в режиме свободного полета
    if (_freeFlightMode) {
        if (!_enabled || !_camera) { return; }

        // Получаем текущую позицию и вектора направлений
        Vec3 position = _camera->getPosition3D();
        Vec3 forward = getForwardVector();
        Vec3 right = getRightVector();

        // Применяем движение в зависимости от нажатых клавиш
        if (_keyW) {
            position += forward * _moveSpeed * delta;
        }
        if (_keyS) {
            position -= forward * _moveSpeed * delta;
        }
        if (_keyD) {
            position += right * _moveSpeed * delta;
        }
        if (_keyA) {
            position -= right * _moveSpeed * delta;
        }

        // Обновляем позицию камеры
        _camera->setPosition3D(position);
    }
    // Обновление в режиме FPS
    else {
        if (!_enabled || !_camera || !_physicsBody || !_physicsComponent) { return; }
        // Не даём заснуь физическому телу
        _physicsBody->setActive(true);

        // Получаем текущую скорость из физического тела
        _velocity = _physicsBody->getLinearVelocity();

        // Обработка движения
        Vec3 movement(0, 0, 0);
        Vec3 forward = getForwardVector();
        Vec3 right = getRightVector();

        if (_keyW) movement += forward;
        if (_keyS) movement -= forward;
        if (_keyD) movement += right;
        if (_keyA) movement -= right;

        // Нормализуем вектор движения, если игрок движется по диагонали
        if (movement.length() > 0) {
            movement.normalize();
        }

        // Применяем скорость только к горизонтальной плоскости (XZ)
        Vec3 horizontalVelocity = movement * _moveSpeed;
        _velocity.x = horizontalVelocity.x;
        _velocity.z = horizontalVelocity.z;

        // Если персонаж на земле и нажат пробел (прыжок)
        if (_isGrounded && _keySpace) {
            _velocity.y = _jumpForce;
            _isGrounded = false;
        }

        // Применяем гравитацию (если не в режиме полета)
        if (!_freeFlightMode) {
            _velocity.y += _gravity * delta;
        }

        // Устанавливаем новую скорость
        _physicsBody->setLinearVelocity(_velocity);

        // Синхронизируем позицию камеры с физическим телом
        _camera->setPosition3D(this->getPosition3D() + Vec3(0, 50.0f, 0));
    }
}

void FirstPersonController::setupEventListeners() {
    // Настраиваем слушатель мыши
    _mouseListener = EventListenerMouse::create();
    _mouseListener->onMouseMove = [this](auto* event) {
        this->onMouseMove(event);
        return true;
    };
    _mouseListener->onMouseUp = [this](auto* event) {
        this->onMouseUp(event);
        return true;
    };
    _mouseListener->onMouseDown = [this](auto* event) {
        this->onMouseDown(event);
        return true;
    };
    _eventDispatcher->addEventListenerWithSceneGraphPriority(_mouseListener, this);

    // Настраиваем слушатель клавиатуры
    _keyboardListener = EventListenerKeyboard::create();
    _keyboardListener->onKeyPressed = [this](auto code, auto* event) { onKeyPressed(code, event); };
    _keyboardListener->onKeyReleased = [this](auto code, auto* event) { onKeyReleased(code, event); };
    _eventDispatcher->addEventListenerWithFixedPriority(_keyboardListener, 11);
}

void FirstPersonController::onMouseDown(Event* event) {
    if (!_enabled) { return; }

    EventMouse* e = static_cast<EventMouse*>(event);
    if (e->getMouseButton() == EventMouse::MouseButton::BUTTON_LEFT) {
        // Режим свободного полета
        if (_freeFlightMode) {
            _isLeftMousePressed = true;
            _lastMousePos = Vec2(e->getCursorX(), e->getCursorY());
        }
        // Режим FPS
        else {
            // Логика при нажатии ЛКМ в FPS режиме
        }
    }
}

void FirstPersonController::onMouseUp(Event* event) {
    if (!_enabled) { return; }

    EventMouse* e = static_cast<EventMouse*>(event);
    if (e->getMouseButton() == EventMouse::MouseButton::BUTTON_LEFT) {
        // Режим свободного полета
        if (_freeFlightMode) {
            _isLeftMousePressed = false;
        }
        // Режим FPS
        else {
            // Логика при отпускании ЛКМ в FPS режиме
        }
    }
}

void FirstPersonController::onMouseMove(Event* event) {
    if (!_enabled || !_camera) { return; }

    EventMouse* e = static_cast<EventMouse*>(event);
    int inversion = _freeFlightMode ? 1 : -1;

    if (!_freeFlightMode || _freeFlightMode && _isLeftMousePressed) {

        // Получаем текущую позицию мыши
        Vec2 currentPos(e->getCursorX(), e->getCursorY());
        //AXLOGD("getCursorX = {} getCursorY = {}", currentPos.x, currentPos.y); //DEBUG

        // Вычисляем разницу в движении
        float deltaX = currentPos.x - _lastMousePos.x;
        float deltaY = currentPos.y - _lastMousePos.y;

        // Получаем текущий угол поворота камеры
        Vec3 currentRotation = _camera->getRotation3D();

        // Изменяем поворот:
        // - deltaX изменяет поворот вокруг оси Y (вращение влево/вправо)
        // - deltaY изменяет поворот вокруг оси X (наклон вверх/вниз)
        // Применяем чувствительность
        float newYaw = currentRotation.y + (deltaX * _mouseSensitivity) * inversion;
        float newPitch = currentRotation.x - (deltaY * _mouseSensitivity) * inversion;

        // Ограничение на угол наклона (pitch), чтобы предотвратить переворачивание камеры
        if (!_freeFlightMode) { newPitch = std::clamp(newPitch, -89.0f, 89.0f); }

        _camera->setRotation3D(Vec3(newPitch, newYaw, currentRotation.z));

        // Обновляем предыдущую позицию курсора для следующего кадра
        _lastMousePos = currentPos;
    }
}

void FirstPersonController::onKeyPressed(EventKeyboard::KeyCode code, Event* event) {
    if (!_enabled) { return; }

    switch (code) {
    case EventKeyboard::KeyCode::KEY_W:
        _keyW = true;
        break;
    case EventKeyboard::KeyCode::KEY_S:
        _keyS = true;
        break;
    case EventKeyboard::KeyCode::KEY_A:
        _keyA = true;
        break;
    case EventKeyboard::KeyCode::KEY_D:
        _keyD = true;
        break;
    case EventKeyboard::KeyCode::KEY_SPACE:
        _keySpace = true;
        if (_isGrounded && !_freeFlightMode) {
            _velocity.y = _jumpForce;
            _isGrounded = false;
        }
        break;
    case EventKeyboard::KeyCode::KEY_F5:
        toggleFlightMode();
        break;
    default:
        break;
    }
}

void FirstPersonController::onKeyReleased(EventKeyboard::KeyCode code, Event* event) {
    if (!_enabled) { return; }

    switch (code) {
    case EventKeyboard::KeyCode::KEY_W:
        _keyW = false;
        break;
    case EventKeyboard::KeyCode::KEY_S:
        _keyS = false;
        break;
    case EventKeyboard::KeyCode::KEY_A:
        _keyA = false;
        break;
    case EventKeyboard::KeyCode::KEY_D:
        _keyD = false;
        break;
    case EventKeyboard::KeyCode::KEY_SPACE:
        _keySpace = false;
        break;
    default:
        break;
    }
}
