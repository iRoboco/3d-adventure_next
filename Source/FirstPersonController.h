#include "axmol.h"
#include "physics3d/Physics3D.h"
 
using namespace ax;

/**
 * @brief Контроллер для управления камерой от первого лица (First Person)
 *
 * Класс обеспечивает простое управление камерой с помощью клавиш WASD и мыши.
 * Поддерживает движение вперед, назад, вправо и влево, а также вращение камеры.
 */
class FirstPersonController : public Node {
public:
    /**
     * Создает экземпляр контроллера первого лица
     * @param camera Указатель на камеру, которой будет управлять контроллер
     * @param moveSpeed Скорость движения камеры (по умолчанию 100.0f)
     * @param mouseSensitivity Чувствительность мыши (по умолчанию 0.1f)
     * @param setFreeFlightMode Активирует режим свободного полета (по умолчанию false)
     * @return Указатель на созданный экземпляр FirstPersonController
     */
    static FirstPersonController* create(Camera* camera, float moveSpeed = 100.0f, float mouseSensitivity = 0.1f);

    // Инициализация
    virtual bool init(Camera* camera, float moveSpeed, float mouseSensitivity);

    // Активирует или деактивирует контроллер
    void setEnabled(bool enabled);
    bool isEnabled() const { return _enabled; }

    // Активирует режим свободного полета
    void setFreeFlightMode(bool enabled);   // Реализация в файле .cpp
    bool isFreeFlightMode() const { return _freeFlightMode; };
    void toggleFlightMode();

    // Устанавливает скорость движения
    void setMoveSpeed(float moveSpeed) { _moveSpeed = moveSpeed; }
    float getMoveSpeed() const { return _moveSpeed; }

    // Устанавливает чувствительность мыши
    void setMouseSensitivity(float sensitivity) { _mouseSensitivity = sensitivity; }
    float getMouseSensitivity() const { return _mouseSensitivity; }

    // Получение векторов камеры
    Vec3 getForwardVector() const;
    Vec3 getRightVector() const;

    // Обновление на каждом кадре
    void update(float delta) override;

protected:
    // Настройка обработчиков событий
    void setupEventListeners();

    // Обработчики событий мыши
    void onMouseDown(Event* event);
    void onMouseUp(Event* event);
    void onMouseMove(Event* event);

    // Обработчики событий клавиатуры
    void onKeyPressed(EventKeyboard::KeyCode code, Event* event);
    void onKeyReleased(EventKeyboard::KeyCode code, Event* event);

    FirstPersonController();
    ~FirstPersonController();

private:
    Camera* _camera;                        // Указатель на контролируемую камеру
    float _moveSpeed;                       // Скорость движения
    float _mouseSensitivity;                // Чувствительность мыши
    
    bool _enabled;                          // Активен ли контроллер
    bool _freeFlightMode;                   // Активация режима свободного полета

    bool _isLeftMousePressed;               // Нажата ли левая кнопка мыши
    Vec2 _lastMousePos;                     // Последнее положение мыши

    Physics3DRigidBody* _physicsBody;       // Физическое тело
    Physics3DComponent* _physicsComponent; 
    bool _isGrounded;                       // На земле ли персонаж
    float _gravity;                         // Сила гравитации
    float _jumpForce;                       // Сила прыжка
    Vec3 _velocity;                         // Текущая скорость

    // Состояние клавиш WASD
    bool _keyW;
    bool _keyA;
    bool _keyS;
    bool _keyD;
    bool _keySpace;

    // Слушатели событий
    EventListenerMouse* _mouseListener;
    EventListenerKeyboard* _keyboardListener;
    EventListenerPhysicsContact* _collisionListener;
};