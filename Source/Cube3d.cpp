#include "Cube3D.h"

using namespace ax;

// Структура вершины для меша куба
struct Vertex3D {
    Vec3 position;
    Vec3 normal;
    Vec2 texCoord;
};

// Функция для создания программного меша куба (единичный размер)
static Mesh* createCubeMesh() {
    // Вершины куба - 24 вершины (по 4 на грань для независимых нормалей и UV)
    std::vector<Vertex3D> vertices;
    std::vector<unsigned short> indices;
    
    float half = 0.5f;
    
    // Front face (Z+)
    vertices.push_back({{-half, -half, half}, {0, 0, 1}, {0, 0}});
    vertices.push_back({{half, -half, half}, {0, 0, 1}, {1, 0}});
    vertices.push_back({{half, half, half}, {0, 0, 1}, {1, 1}});
    vertices.push_back({{-half, half, half}, {0, 0, 1}, {0, 1}});
    
    // Back face (Z-)
    vertices.push_back({{half, -half, -half}, {0, 0, -1}, {0, 0}});
    vertices.push_back({{-half, -half, -half}, {0, 0, -1}, {1, 0}});
    vertices.push_back({{-half, half, -half}, {0, 0, -1}, {1, 1}});
    vertices.push_back({{half, half, -half}, {0, 0, -1}, {0, 1}});
    
    // Left face (X-)
    vertices.push_back({{-half, -half, -half}, {-1, 0, 0}, {0, 0}});
    vertices.push_back({{-half, -half, half}, {-1, 0, 0}, {1, 0}});
    vertices.push_back({{-half, half, half}, {-1, 0, 0}, {1, 1}});
    vertices.push_back({{-half, half, -half}, {-1, 0, 0}, {0, 1}});
    
    // Right face (X+)
    vertices.push_back({{half, -half, half}, {1, 0, 0}, {0, 0}});
    vertices.push_back({{half, -half, -half}, {1, 0, 0}, {1, 0}});
    vertices.push_back({{half, half, -half}, {1, 0, 0}, {1, 1}});
    vertices.push_back({{half, half, half}, {1, 0, 0}, {0, 1}});
    
    // Top face (Y+)
    vertices.push_back({{-half, half, half}, {0, 1, 0}, {0, 0}});
    vertices.push_back({{half, half, half}, {0, 1, 0}, {1, 0}});
    vertices.push_back({{half, half, -half}, {0, 1, 0}, {1, 1}});
    vertices.push_back({{-half, half, -half}, {0, 1, 0}, {0, 1}});
    
    // Bottom face (Y-)
    vertices.push_back({{-half, -half, -half}, {0, -1, 0}, {0, 0}});
    vertices.push_back({{half, -half, -half}, {0, -1, 0}, {1, 0}});
    vertices.push_back({{half, -half, half}, {0, -1, 0}, {1, 1}});
    vertices.push_back({{-half, -half, half}, {0, -1, 0}, {0, 1}});
    
    // Индексы для каждой грани (2 треугольника на грань)
    for (int i = 0; i < 6; i++) {
        unsigned short base = i * 4;
        indices.push_back(base);
        indices.push_back(base + 1);
        indices.push_back(base + 2);
        indices.push_back(base);
        indices.push_back(base + 2);
        indices.push_back(base + 3);
    }
    
    // Создаем атрибуты вершин
    std::vector<MeshVertexAttribute> attributes;
    
    MeshVertexAttribute posAttr;
    posAttr.attribName = "a_position";
    posAttr.format = VertexFormat::FLOAT3;
    posAttr.offset = offsetof(Vertex3D, position);
    attributes.push_back(posAttr);
    
    MeshVertexAttribute normAttr;
    normAttr.attribName = "a_normal";
    normAttr.format = VertexFormat::FLOAT3;
    normAttr.offset = offsetof(Vertex3D, normal);
    attributes.push_back(normAttr);
    
    MeshVertexAttribute uvAttr;
    uvAttr.attribName = "a_texcoord";
    uvAttr.format = VertexFormat::FLOAT2;
    uvAttr.offset = offsetof(Vertex3D, texCoord);
    attributes.push_back(uvAttr);
    
    // Создаем меш
    Mesh* mesh = Mesh::create(
        &vertices[0],
        sizeof(Vertex3D),
        static_cast<unsigned int>(vertices.size()),
        indices.data(),
        static_cast<unsigned int>(indices.size()),
        attributes,
        Mesh::PrimitiveType::TRIANGLES
    );
    
    return mesh;
}

Cube3D* Cube3D::create(const std::string& /*meshFile*/,
    const std::array<std::string, 6>& textureFiles, float cubeSize) {
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

    // Половина размера куба для определения смещений граней
    float halfSize = cubeSize * 1.0f;

    // Определяем трансформации для каждой грани:
    // Порядок граней: Front, Back, Left, Right, Top, Bottom
    struct FaceTransform {
        Vec3 position;
        Vec3 rotation; // в градусах
    };
    const std::array<FaceTransform, 6> faceTransforms = {{
        { Vec3(0, 0, halfSize),     Vec3(90, 0, 0) },        // Front
        { Vec3(0, 0, -halfSize),    Vec3(-90, 0, 180) },     // Back
        { Vec3(-halfSize, 0, 0),    Vec3(0, -90, -90) },     // Left
        { Vec3(halfSize, 0, 0),     Vec3(0, 90, 90) },       // Right
        { Vec3(0, halfSize, 0),     Vec3(0, 0, 0) },         // Top
        { Vec3(0, -halfSize, 0),    Vec3(0, 0, 180) }        // Bottom
    }};

    // Создаем программный меш куба
    Mesh* cubeMesh = createCubeMesh();
    if (!cubeMesh) {
        AXLOGD("Не удалось создать меш куба");
        return false;
    }

    // Создаем и располагаем каждую грань как дочерний узел
    for (int i = 0; i < 6; i++) {
        // Создаем меш-рендерер для грани с программным мешом
        MeshRenderer* face = MeshRenderer::createWithMesh(cubeMesh);
        if (!face) {
            AXLOGD("Не удалось создать меш-рендерер для грани %d", i);
            continue;
        }
        // Назначаем текстуру для грани
        face->setTexture(textureFiles[i]);
        // Применяем трансформации
        face->setPosition3D(faceTransforms[i].position);
        face->setRotation3D(faceTransforms[i].rotation);
        // Масштабируем грань до нужного размера
        face->setScale(cubeSize);
        // Добавляем грань как дочерний узел
        this->addChild(face);
    }

    return true;
}
