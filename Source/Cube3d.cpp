#include "Cube3d.h"
#include "3d/Mesh.h"
#include "3d/MeshRenderer.h"
#include "physics3d/PhysicsMeshRenderer.h"
#include "renderer/Material.h"
#include "renderer/backend/Enums.h"

using namespace ax;

// Вспомогательная функция для создания программного меша куба
static Mesh* createCubeMesh(float size) {
    float h = size; // половина размера для координат от -h до +h
    
    // Вершины куба (24 вершины для 6 граней с отдельными UV и нормалями)
    // Формат: позиция (x, y, z), нормаль (nx, ny, nz), UV (u, v)
    std::vector<float> positions = {
        // Front face (z = +h)
        -h, -h,  h,   h, -h,  h,   h,  h,  h,  -h,  h,  h,
        // Back face (z = -h)
        h, -h, -h,  -h, -h, -h,  -h,  h, -h,   h,  h, -h,
        // Top face (y = +h)
        -h,  h,  h,   h,  h,  h,   h,  h, -h,  -h,  h, -h,
        // Bottom face (y = -h)
        -h, -h, -h,   h, -h, -h,   h, -h,  h,  -h, -h,  h,
        // Right face (x = +h)
        h, -h,  h,   h, -h, -h,   h,  h, -h,   h,  h,  h,
        // Left face (x = -h)
        -h, -h, -h,  -h, -h,  h,  -h,  h,  h,  -h,  h, -h
    };
    
    // Нормали для каждой грани
    std::vector<float> normals = {
        // Front face (z = +h)
        0, 0, 1,   0, 0, 1,   0, 0, 1,   0, 0, 1,
        // Back face (z = -h)
        0, 0, -1,  0, 0, -1,  0, 0, -1,  0, 0, -1,
        // Top face (y = +h)
        0, 1, 0,   0, 1, 0,   0, 1, 0,   0, 1, 0,
        // Bottom face (y = -h)
        0, -1, 0,  0, -1, 0,  0, -1, 0,  0, -1, 0,
        // Right face (x = +h)
        1, 0, 0,   1, 0, 0,   1, 0, 0,   1, 0, 0,
        // Left face (x = -h)
        -1, 0, 0,  -1, 0, 0,  -1, 0, 0,  -1, 0, 0
    };
    
    // UV координаты для каждой грани
    std::vector<float> texCoords = {
        // Front face
        0, 0,   1, 0,   1, 1,   0, 1,
        // Back face
        0, 0,   1, 0,   1, 1,   0, 1,
        // Top face
        0, 0,   1, 0,   1, 1,   0, 1,
        // Bottom face
        0, 0,   1, 0,   1, 1,   0, 1,
        // Right face
        0, 0,   1, 0,   1, 1,   0, 1,
        // Left face
        0, 0,   1, 0,   1, 1,   0, 1
    };
    
    // Индексы для 6 граней (каждая грань - 2 треугольника = 6 индексов)
    IndexArray indices(backend::IndexFormat::U_SHORT);
    indices.push_back_many({
        // Front face
        0, 1, 2,    0, 2, 3,
        // Back face
        4, 5, 6,    4, 6, 7,
        // Top face
        8, 9, 10,   8, 10, 11,
        // Bottom face
        12, 13, 14,  12, 14, 15,
        // Right face
        16, 17, 18,  16, 18, 19,
        // Left face
        20, 21, 22,  20, 22, 23
    });
    
    return Mesh::create(positions, normals, texCoords, indices);
}

// Вспомогательная функция для создания PhysicsMeshRenderer с программным мешем
static PhysicsMeshRenderer* createPhysicsCube(Physics3DRigidBodyDes* rigidDes, float cubeSize) {
    auto renderer = new PhysicsMeshRenderer();
    
    if (renderer->init()) {
        // Создаем программный меш куба
        Mesh* cubeMesh = createCubeMesh(cubeSize);
        
        if (cubeMesh) {
            renderer->addMesh(cubeMesh);
            
            // Настраиваем физику
            auto obj = Physics3DRigidBody::create(rigidDes);
            renderer->_physicsComponent = Physics3DComponent::create(obj);
            renderer->addComponent(renderer->_physicsComponent);
            
            renderer->_contentSize = Size(cubeSize * 2, cubeSize * 2);
            renderer->autorelease();
            return renderer;
        }
    }
    
    AX_SAFE_DELETE(renderer);
    return nullptr;
}

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
