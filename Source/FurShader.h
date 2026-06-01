#pragma once
#include "axmol.h"
#include "renderer/backend/ProgramState.h"
#include "renderer/backend/ProgramManager.h"

class FurShader
{
public:
    static constexpr int DEFAULT_LAYER_COUNT = 8;
    static constexpr float FUR_LENGTH = 0.15f;
    static constexpr float FUR_DENSITY = 0.7f;
    static constexpr int NOISE_SIZE = 8;

    static ax::backend::ProgramState* createProgramState()
    {
        auto* mgr = ax::backend::ProgramManager::getInstance();
        auto* prog = mgr->loadProgram(
            "custom/fur_vs", "custom/fur_fs",
            ax::backend::VertexLayoutType::Terrain3D
        );
        if (!prog)
        {
            AXLOGE("FurShader: failed to load fur shaders");
            return nullptr;
        }
        auto* ps = new ax::backend::ProgramState(prog);
        ps->autorelease();
        return ps;
    }

    static ax::Texture2D* createNoiseTexture()
    {
        std::vector<uint8_t> pixels(NOISE_SIZE * NOISE_SIZE * 4);
        for (int i = 0; i < NOISE_SIZE * NOISE_SIZE; ++i)
        {
            float v = static_cast<float>(rand()) / RAND_MAX;
            uint8_t b = static_cast<uint8_t>(v * 255);
            pixels[i * 4 + 0] = b;
            pixels[i * 4 + 1] = b;
            pixels[i * 4 + 2] = b;
            pixels[i * 4 + 3] = 255;
        }
        auto* image = new ax::Image();
        if (!image->initWithRawData(pixels.data(), pixels.size(), NOISE_SIZE, NOISE_SIZE, 8))
        {
            AXLOGE("FurShader: failed to create noise image");
            image->release();
            return nullptr;
        }
        auto* tex = ax::Director::getInstance()->getTextureCache()->addImage(image, "fur_noise_tex");
        image->release();
        if (!tex) return nullptr;

        ax::Texture2D::TexParams params{};
        params.magFilter = ax::backend::SamplerFilter::NEAREST;
        params.minFilter = ax::backend::SamplerFilter::NEAREST;
        params.sAddressMode = ax::backend::SamplerAddressMode::REPEAT;
        params.tAddressMode = ax::backend::SamplerAddressMode::REPEAT;
        tex->setTexParameters(params);
        return tex;
    }

    static ax::Material* createFurMaterial(ax::Texture2D* atlas, ax::Texture2D* noiseTex, int layerCount = DEFAULT_LAYER_COUNT)
    {
        auto* program = ax::backend::ProgramManager::getInstance()->loadProgram(
            "custom/fur_vs", "custom/fur_fs",
            ax::backend::VertexLayoutType::Terrain3D
        );
        if (!program) return nullptr;

        // Рендерим слои BACK-TO-FRONT (дальний → ближний) с screen-door дизерингом.
        // Каждый слой: opaque (без блендинга), пишет depth, depth-test LEQUAL.
        // Дизеринг в фрагментном шейдере заменяет альфа-блендинг,
        // устраняя артефакты сортировки прозрачных объектов.

        // Создаём первый (самый дальний, layer = 1.0) ProgramState
        auto* firstPs = new ax::backend::ProgramState(program);
        float firstLayer = 1.0f;
        firstPs->setUniform(firstPs->getUniformLocation("u_Layer"), &firstLayer, sizeof(float));
        firstPs->setUniform(firstPs->getUniformLocation("u_Length"), &FUR_LENGTH, sizeof(float));
        firstPs->setUniform(firstPs->getUniformLocation("u_Density"), &FUR_DENSITY, sizeof(float));
        if (atlas)
            firstPs->setTexture(firstPs->getUniformLocation("u_tex0"), 0, atlas->getBackendTexture());
        if (noiseTex)
            firstPs->setTexture(firstPs->getUniformLocation("u_noiseTex"), 1, noiseTex->getBackendTexture());

        auto* material = ax::Material::createWithProgramState(firstPs);
        firstPs->release();
        if (!material) return nullptr;

        auto* technique = material->getTechnique();
        if (!technique)
        {
            AXLOGE("FurShader: no technique in material");
            return nullptr;
        }

        // Настраиваем первый пасс (самый дальний слой)
        {
            auto* pass = technique->getPassByIndex(0);
            if (pass)
            {
                auto& sb = pass->getStateBlock();
                sb.setBlend(false);
                sb.setDepthTest(true);
                sb.setDepthWrite(true);
                sb.setCullFace(false);
            }
        }

        // Создаём остальные пассы (от layerCount-1 вниз до 1)
        for (int i = layerCount - 1; i >= 1; --i)
        {
            auto* ps = new ax::backend::ProgramState(program);

            float layer = static_cast<float>(i) / layerCount;
            ps->setUniform(ps->getUniformLocation("u_Layer"), &layer, sizeof(float));
            ps->setUniform(ps->getUniformLocation("u_Length"), &FUR_LENGTH, sizeof(float));
            ps->setUniform(ps->getUniformLocation("u_Density"), &FUR_DENSITY, sizeof(float));

            if (atlas)
                ps->setTexture(ps->getUniformLocation("u_tex0"), 0, atlas->getBackendTexture());
            if (noiseTex)
                ps->setTexture(ps->getUniformLocation("u_noiseTex"), 1, noiseTex->getBackendTexture());

            auto* pass = ax::Pass::createWithProgramState(technique, ps);
            ps->release();

            auto& sb = pass->getStateBlock();
            sb.setBlend(false);
            sb.setDepthTest(true);
            sb.setDepthWrite(true);
            sb.setCullFace(false);

            technique->addPass(pass);
        }

        return material;
    }
};
