#include "Cube3d.h"
#include "3d/Mesh.h"
#include "3d/MeshRenderer.h"
#include "renderer/Material.h"
#include "renderer/backend/Enums.h"

using namespace ax;

// Вспомогательная функция для создания программного меша куба
static Mesh* createCubeMesh(float size) {
    float h = size / 2.0f; // половина размера для координат от -h до +h
    
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
    // Front face
    indices.emplace_back<uint16_t>(0); indices.emplace_back<uint16_t>(1); indices.emplace_back<uint16_t>(2);
    indices.emplace_back<uint16_t>(0); indices.emplace_back<uint16_t>(2); indices.emplace_back<uint16_t>(3);
    // Back face
    indices.emplace_back<uint16_t>(4); indices.emplace_back<uint16_t>(5); indices.emplace_back<uint16_t>(6);
    indices.emplace_back<uint16_t>(4); indices.emplace_back<uint16_t>(6); indices.emplace_back<uint16_t>(7);
    // Top face
    indices.emplace_back<uint16_t>(8); indices.emplace_back<uint16_t>(9); indices.emplace_back<uint16_t>(10);
    indices.emplace_back<uint16_t>(8); indices.emplace_back<uint16_t>(10); indices.emplace_back<uint16_t>(11);
    // Bottom face
    indices.emplace_back<uint16_t>(12); indices.emplace_back<uint16_t>(13); indices.emplace_back<uint16_t>(14);
    indices.emplace_back<uint16_t>(12); indices.emplace_back<uint16_t>(14); indices.emplace_back<uint16_t>(15);
    // Right face
    indices.emplace_back<uint16_t>(16); indices.emplace_back<uint16_t>(17); indices.emplace_back<uint16_t>(18);
    indices.emplace_back<uint16_t>(16); indices.emplace_back<uint16_t>(18); indices.emplace_back<uint16_t>(19);
    // Left face
    indices.emplace_back<uint16_t>(20); indices.emplace_back<uint16_t>(21); indices.emplace_back<uint16_t>(22);
    indices.emplace_back<uint16_t>(20); indices.emplace_back<uint16_t>(22); indices.emplace_back<uint16_t>(23);
    
    return Mesh::create(positions, normals, texCoords, indices);
}

Cube3D* Cube3D::create(const std::array<std::string, 6>& textureFiles, float cubeSize) {
    Cube3D* ret = new (std::nothrow) Cube3D();
    if (ret && ret->init(textureFiles, cubeSize)) {
        ret->autorelease();
        return ret;
    }
    AX_SAFE_DELETE(ret);
    return nullptr;
}

bool Cube3D::init(const std::array<std::string, 6>& textureFiles, float cubeSize) {
    if (!Node::init()) {
        return false;
    }

    // Создаем программный меш куба
    Mesh* cubeMesh = createCubeMesh(cubeSize);
    if (!cubeMesh) {
        AXLOGD("Не удалось создать меш куба");
        return false;
    }

    // Создаем MeshRenderer с программным мешем
    MeshRenderer* meshRenderer = MeshRenderer::create();
    if (!meshRenderer) {
        AXLOGD("Не удалось создать MeshRenderer");
        return false;
    }

    // Устанавливаем меш
    meshRenderer->setMesh(cubeMesh);
    
    // Для упрощения назначаем первую текстуру на все грани
    // В реальном проекте можно использовать AtlasTexture или разные материалы для каждой грани
    meshRenderer->setTexture(textureFiles[0]);
    
    // Включаем отсечение задних граней для оптимизации отрисовки
    meshRenderer->getMaterial(0)->setCullFace(backend::CullFace::BACK);
    
    this->addChild(meshRenderer);

    return true;
}

ax::Mesh* Cube3D::createCubeMesh(float size) {
    return ::createCubeMesh(size);
}
