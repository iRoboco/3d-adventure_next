#include "Cube3D.h"

using namespace ax;

Cube3D* Cube3D::create(const std::string& meshFile,
    const std::array<std::string, 6>& textureFiles, float cubeSize) {
    Cube3D* ret = new (std::nothrow) Cube3D();
    if (ret && ret->init(meshFile, textureFiles, cubeSize)) {
        ret->autorelease();
        return ret;
    }
    AX_SAFE_DELETE(ret);
    return nullptr;
}

bool Cube3D::init(const std::string& meshFile,
    const std::array<std::string, 6>& textureFiles, float cubeSize) {
    if (!Node::init()) {
        return false;
    }

    // Половина размера куба для определения смещений граней
    float halfSize = cubeSize * 1.0f;

    // Определяем трансформации для каждой грани:
    // Порядок граней: Front, Back, Left, Right, Top, Bottom
    struct FaceTransform {
        Vec3 position;
        Vec3 rotation; // в градусах
    };
    const std::array<FaceTransform, 6> faceTransforms = { {
        { Vec3(0, 0, halfSize),     Vec3(90, 0, 0) },        // Front
        { Vec3(0, 0, -halfSize),    Vec3(-90, 0, 180) },     // Back
        { Vec3(-halfSize, 0, 0),    Vec3(0, -90, -90) },     // Left
        { Vec3(halfSize, 0, 0),     Vec3(0, 90, 90) },       // Right
        { Vec3(0, halfSize, 0),     Vec3(0, 0, 0) },         // Top
        { Vec3(0, -halfSize, 0),    Vec3(0, 0, 180) }        // Bottom
    } };

    // Создаем и располагаем каждую грань как дочерний узел
    for (int i = 0; i < 6; i++) {
        // Создаем меш-рендерер для грани. Если все грани используют одинаковый геометрический шаблон,
        // можно использовать один файл для всех.
        MeshRenderer* face = MeshRenderer::create(meshFile);
        if (!face) {
            AXLOGD("Не удалось загрузить меш для грани: %s", meshFile.c_str());
            continue;
        }
        // Назначаем текстуру для грани
        face->setTexture(textureFiles[i]);
        // Применяем трансформации
        face->setPosition3D(faceTransforms[i].position);
        face->setRotation3D(faceTransforms[i].rotation);
        // Масштабируем грань до нужного размера. Предполагается, что исходная геометрия имеет 
        // размер 1x1, иначе скорректируйте коэффициент.
        face->setScale(cubeSize);
        // Добавляем грань как дочерний узел
        this->addChild(face);
    }

    return true;
}

