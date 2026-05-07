#include "axmol.h"
#include <array>
#include <string>

/**
 * @brief 3D-куб с индивидуальными текстурами для каждой грани.
 *
 * Класс Cube3D наследуется от ax::Node и предоставляет возможность создания 3D-куба,
 * каждая грань которого может иметь свою текстуру. Геометрия куба создается программно.
 */
class Cube3D : public ax::Node {
public:
    /**
     * @brief Создаёт экземпляр Cube3D.
     * 
     * @param meshFile Путь к файлу меша (не используется, геометрия создается программно).
     * @param textureFiles Массив из 6 путей к текстурам для каждой грани куба.
     *        Порядок: Front, Back, Left, Right, Top, Bottom.
     * @param cubeSize Размер куба (по умолчанию 1.0f).
     * @return Указатель на созданный объект Cube3D или nullptr в случае ошибки.
     *
     * @note Объект создаётся с помощью create(), как принято в Axmol.
     */
    static Cube3D* create(const std::string& meshFile,
        const std::array<std::string, 6>& textureFiles,
        float cubeSize = 1.0f);

    /**
     * @brief Инициализирует объект Cube3D.
     * 
     * @param textureFiles Массив из 6 путей к текстурам для каждой грани куба.
     *        Порядок: Front, Back, Left, Right, Top, Bottom.
     * @param cubeSize Размер куба.
     * @return true, если инициализация прошла успешно, иначе false.
     *
     * @note Обычно вызывается внутри create().
     */
    virtual bool init(const std::array<std::string, 6>& textureFiles,
        float cubeSize);

protected:
    /**
     * @brief Защищённый конструктор по умолчанию.
     */
    Cube3D() = default;

    /**
     * @brief Виртуальный защищённый деструктор.
     */
    virtual ~Cube3D() = default;
};
