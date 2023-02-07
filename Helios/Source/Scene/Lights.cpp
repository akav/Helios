#include "Scene/Lights.hpp"

#include "Graphics/GraphicsContext.hpp"
#include "Graphics/GraphicsDevice.hpp"

namespace helios::scene
{
    Lights::Lights(const gfx::GraphicsDevice* const graphicsDevice)
    {
        m_lightsBuffer = graphicsDevice->createBuffer<interlop::LightBuffer>(gfx::BufferCreationDesc{
            .usage = gfx::BufferUsage::ConstantBuffer,
            .name = L"Light Constant Buffer",
        });

        m_lightsInstanceBuffer =
            graphicsDevice->createBuffer<interlop::LightInstancedRenderingBuffer>(gfx::BufferCreationDesc{
                .usage = gfx::BufferUsage::ConstantBuffer,
                .name = L"Light Instance Constant Buffer",
            });

        m_lightModel = std::make_unique<Model>(graphicsDevice, ModelCreationDesc{
                                                                   .modelPath = L"Assets/Models/Cube/glTF/Cube.gltf",
                                                                   .modelName = L"Light Model",
                                                                   .scale = {0.2f, 0.2f, 0.2f},
                                                               });

        m_lightPipelineState = graphicsDevice->createPipelineState(
            gfx::GraphicsPipelineStateCreationDesc{.shaderModule{.vertexShaderPath = L"Shaders/Lights/Lights.hlsl",
                                                                 .pixelShaderPath = L"Shaders/Lights/Lights.hlsl"},
                                                   .pipelineName = L"Lights Pipeline"});
    }

    void Lights::update()
    {
        for (const uint32_t i : std::views::iota(0u, interlop::TOTAL_POINT_LIGHTS))
        {
            // Make the model matrix.

            const math::XMVECTOR translationVector = math::XMLoadFloat4(&m_lightsBufferData.lightPosition[i]);

            m_lightsInstancedBufferData.modelMatrix[i] =
                math::XMMatrixScaling(m_lightsBufferData.radiusIntensity[i].x, m_lightsBufferData.radiusIntensity[i].x,
                                      m_lightsBufferData.radiusIntensity[i].x) *
                math::XMMatrixTranslationFromVector(translationVector);

        }

        m_lightsInstanceBuffer.update(&m_lightsInstancedBufferData);
        m_lightsBuffer.update(&m_lightsBufferData);
    }

    void Lights::render(const gfx::GraphicsContext* const graphicsContext,
                        interlop::LightRenderResources& lightRenderResources)
    {
        graphicsContext->setGraphicsRootSignatureAndPipeline(m_lightPipelineState);

        lightRenderResources.lightBufferIndex = m_lightsBuffer.cbvIndex;
        lightRenderResources.transformBufferIndex = m_lightsInstanceBuffer.cbvIndex;

        m_lightModel->render(graphicsContext, lightRenderResources);
    }
} // namespace helios::scene