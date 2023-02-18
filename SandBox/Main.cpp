#include "Helios.hpp"

using namespace helios;

class SandBox final : public helios::core::Application
{
  public:
    explicit SandBox(const std::string_view windowTitle) : Application(windowTitle)
    {
    }

    void loadContent() override
    {
        loadScene();

        loadTextures();

        loadPipelineStates();

        m_postProcessingBuffer = m_graphicsDevice->createBuffer<interlop::PostProcessingBuffer>(gfx::BufferCreationDesc{
            .usage = gfx::BufferUsage::ConstantBuffer,
            .name = L"Post Processing Buffer",
        });

        m_deferredGPass = rendering::DeferredGeometryPass(m_graphicsDevice.get(), m_windowWidth, m_windowHeight);

        m_ibl = rendering::IBL(m_graphicsDevice.get());

        m_irradianceTexture =
            m_ibl->generateIrradianceTexture(m_graphicsDevice.get(), m_scene->m_cubeMap->m_cubeMapTexture);

        m_prefilterTexture =
            m_ibl->generatePrefilterTexture(m_graphicsDevice.get(), m_scene->m_cubeMap->m_cubeMapTexture);

        m_brdfLUTTexture = m_ibl->generateBRDFLutTexture(m_graphicsDevice.get());

        m_shadowMappingPass = rendering::PCFShadowMappingPass(m_graphicsDevice.get());

        m_ssaoPass = rendering::SSAOPass(m_graphicsDevice.get(), m_windowWidth, m_windowHeight);

        m_bloomPass = rendering::BloomPass(m_graphicsDevice.get(), m_windowWidth, m_windowHeight);
    }

    void loadScene()
    {
        m_scene->addModel(m_graphicsDevice.get(),
                          scene::ModelCreationDesc{
                              .modelPath = L"Assets/Models/DamagedHelmet/glTF/DamagedHelmet.gltf",
                              .modelName = L"Damaged Helmet",
                          });
    
    //    m_scene->addModel(m_graphicsDevice.get(),
    //                      scene::ModelCreationDesc{
    //                          .modelPath = L"Assets/Models/MetalRoughSpheres/glTF/MetalRoughSpheres.gltf",
    //                          .modelName = L"MetalRough spheres",
    //                      });
    // 
       // m_scene->addModel(m_graphicsDevice.get(), scene::ModelCreationDesc{
       //                                               .modelPath = L"Assets/Models/Sponza/sponza.glb",
       //                                               .modelName = L"Sponza",
       //                                               .scale =
       //                                                   {
       //                                                       0.1f,
       //                                                       0.1f,
       //                                                       0.1f,
       //                                                   },
       //                                           });
       //
        m_scene->addLight(m_graphicsDevice.get(),
                          scene::LightCreationDesc{.lightType = scene::LightTypes::PointLightData});

                m_scene->addLight(m_graphicsDevice.get(),
                          scene::LightCreationDesc{.lightType = scene::LightTypes::PointLightData});

                        m_scene->addLight(m_graphicsDevice.get(),
                                  scene::LightCreationDesc{.lightType = scene::LightTypes::PointLightData});

                                m_scene->addLight(m_graphicsDevice.get(),
                                          scene::LightCreationDesc{.lightType = scene::LightTypes::PointLightData});

                                        m_scene->addLight(
                                    m_graphicsDevice.get(),
                                    scene::LightCreationDesc{.lightType = scene::LightTypes::PointLightData});
        m_scene->addLight(m_graphicsDevice.get(),
                          scene::LightCreationDesc{.lightType = scene::LightTypes::DirectionalLightData});

        m_scene->addCubeMap(m_graphicsDevice.get(),
                            scene::CubeMapCreationDesc{
                                .equirectangularTexturePath = L"Assets/Textures/syferfontein_1d_clear_puresky_4k.hdr",
                                .name = L"Environment Cube Map",
                            });
    }

    void loadPipelineStates()
    {

        m_pipelineState = m_graphicsDevice->createPipelineState(gfx::GraphicsPipelineStateCreationDesc{
            .shaderModule =
                {
                    .vertexShaderPath = L"Shaders/Shading/PBR.hlsl",
                    .pixelShaderPath = L"Shaders/Shading/PBR.hlsl",
                },
            .depthFormat = DXGI_FORMAT_UNKNOWN,
            .pipelineName = L"PBR Pipeline",
        });

        m_postProcessingPipelineState = m_graphicsDevice->createPipelineState(gfx::GraphicsPipelineStateCreationDesc{
            .shaderModule =
                {
                    .vertexShaderPath = L"Shaders/PostProcessing/PostProcessing.hlsl",
                    .pixelShaderPath = L"Shaders/PostProcessing/PostProcessing.hlsl",
                },
            .rtvFormats = {DXGI_FORMAT_R10G10B10A2_UNORM},
            .rtvCount = 1u,
            .depthFormat = DXGI_FORMAT_D32_FLOAT,
            .pipelineName = L"Post Processing Pipeline",
        });

        m_fullScreenTrianglePassPipelineState =
            m_graphicsDevice->createPipelineState(gfx::GraphicsPipelineStateCreationDesc{
                .shaderModule =
                    {
                        .vertexShaderPath = L"Shaders/RenderPass/FullScreenTrianglePass.hlsl",
                        .pixelShaderPath = L"Shaders/RenderPass/FullScreenTrianglePass.hlsl",
                    },
                .rtvFormats = {DXGI_FORMAT_R10G10B10A2_UNORM},
                .rtvCount = 1u,
                .depthFormat = DXGI_FORMAT_UNKNOWN,
                .pipelineName = L"Full Screen Triangle Pass Pipeline",
            });
    }

    void loadTextures()
    {
        static constexpr std::array<uint32_t, 3u> indices = {
            0u,
            1u,
            2u,
        };

        m_renderTargetIndexBuffer = m_graphicsDevice->createBuffer<uint32_t>(
            gfx::BufferCreationDesc{
                .usage = gfx::BufferUsage::IndexBuffer,
                .name = L"Render Target Index Buffer",
            },
            indices);

        m_depthTexture = m_graphicsDevice->createTexture(gfx::TextureCreationDesc{
            .usage = gfx::TextureUsage::DepthStencil,
            .width = m_windowWidth,
            .height = m_windowHeight,
            .format = DXGI_FORMAT_D32_FLOAT,
            .name = L"Depth Texture",
        });

        m_fullScreenPassDepthTexture = m_graphicsDevice->createTexture(gfx::TextureCreationDesc{
            .usage = gfx::TextureUsage::DepthStencil,
            .width = m_windowWidth,
            .height = m_windowHeight,
            .format = DXGI_FORMAT_D32_FLOAT,
            .name = L"Full Screen Pass Depth Texture",
        });

        m_offscreenRenderTarget = m_graphicsDevice->createTexture(gfx::TextureCreationDesc{
            .usage = gfx::TextureUsage::RenderTarget,
            .width = m_windowWidth,
            .height = m_windowHeight,
            .format = DXGI_FORMAT_R16G16B16A16_FLOAT,
            .name = L"OffScreen Render Target",
        });

        m_postProcessingRenderTarget = m_graphicsDevice->createTexture(gfx::TextureCreationDesc{
            .usage = gfx::TextureUsage::RenderTarget,
            .width = m_windowWidth,
            .height = m_windowHeight,
            .format = DXGI_FORMAT_R10G10B10A2_UNORM,
            .name = L"Post Processing Render Target",
        });
    }

    void update(const float deltaTime) override
    {
        m_scene->update(deltaTime, m_input, static_cast<float>(m_windowWidth) / m_windowHeight);

        m_postProcessingBuffer.update(&m_postProcessingBufferData);
    }

    void render() override
    {
        m_graphicsDevice->beginFrame();

        std::unique_ptr<gfx::GraphicsContext>& gctx = m_graphicsDevice->getCurrentGraphicsContext();
        gfx::Texture& currentBackBuffer = m_graphicsDevice->getCurrentBackBuffer();

        // const std::array<float, 4> clearColor = {std::abs(std::cosf(m_frameCount / 120.0f)), 0.0f,
        //                                          std::abs(std::sinf(m_frameCount / 120.0f)), 1.0f};
        static std::array<float, 4> clearColor = {0.0f, 0.0f, 0.0f, 1.0f};

        gctx->clearRenderTargetView(m_offscreenRenderTarget, clearColor);
        gctx->clearRenderTargetView(m_postProcessingRenderTarget, clearColor);

        gctx->clearDepthStencilView(m_depthTexture);
        gctx->clearDepthStencilView(m_fullScreenPassDepthTexture);

        // RenderPass 0 : Deferred GPass.
        {
            m_deferredGPass->render(m_scene.get(), gctx.get(), m_depthTexture, m_windowWidth, m_windowHeight);
        }

        // RenderPass 2 : SSAO Pass.
        {
            gctx->addResourceBarrier(m_depthTexture.allocation.resource.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE,
                                     D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            gctx->executeResourceBarriers();

            interlop::SSAORenderResources renderResources = {
                .positionTextureIndex = m_deferredGPass->m_gBuffer.positionEmissiveRT.srvIndex,
                .normalTextureIndex = m_deferredGPass->m_gBuffer.normalEmissiveRT.srvIndex,
                .depthTextureIndex = m_depthTexture.srvIndex,
                .sceneBufferIndex = m_scene->m_sceneBuffer.cbvIndex,
            };

            m_ssaoPass->render(gctx.get(), m_renderTargetIndexBuffer, renderResources, m_windowWidth, m_windowHeight);

            gctx->addResourceBarrier(m_depthTexture.allocation.resource.Get(),
                                     D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
            gctx->executeResourceBarriers();
        }

        // RenderPass 3 : Shadow mapping pass.
        {
            m_shadowMappingPass->render(m_scene.get(), gctx.get());
        }

        // RenderPass 4 : Shading Pass
        {
            gctx->setGraphicsRootSignatureAndPipeline(m_pipelineState);
            gctx->setRenderTarget(m_offscreenRenderTarget, m_fullScreenPassDepthTexture);
            gctx->setViewport(D3D12_VIEWPORT{
                .TopLeftX = 0.0f,
                .TopLeftY = 0.0f,
                .Width = static_cast<float>(m_windowWidth),
                .Height = static_cast<float>(m_windowHeight),
                .MinDepth = 0.0f,
                .MaxDepth = 1.0f,
            });

            gctx->setPrimitiveTopologyLayout(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

            interlop::PBRRenderResources renderResources = {
                .albedoGBufferIndex = m_deferredGPass->m_gBuffer.albedoRT.srvIndex,
                .positionEmissiveGBufferIndex = m_deferredGPass->m_gBuffer.positionEmissiveRT.srvIndex,
                .normalEmissiveGBufferIndex = m_deferredGPass->m_gBuffer.normalEmissiveRT.srvIndex,
                .aoMetalRoughnessEmissiveGBufferIndex = m_deferredGPass->m_gBuffer.aoMetalRoughnessEmissiveRT.srvIndex,
                .irradianceTextureIndex = m_irradianceTexture.srvIndex,
                .prefilterTextureIndex = m_prefilterTexture.srvIndex,
                .brdfLUTTextureIndex = m_brdfLUTTexture.srvIndex,
                .shadowBufferIndex = m_shadowMappingPass->m_shadowBuffer.cbvIndex,
                .shadowDepthTextureIndex = m_shadowMappingPass->m_shadowDepthBuffer.srvIndex,
                .blurredSSAOTextureIndex = m_ssaoPass->m_blurSSAOTexture.srvIndex,
            };

            m_scene->renderModels(gctx.get(), renderResources);
        }

        // RenderPass 5 : Render lights and cube map using forward rendering.
        {
            gctx->setViewport(D3D12_VIEWPORT{
                .TopLeftX = 0.0f,
                .TopLeftY = 0.0f,
                .Width = static_cast<float>(m_windowWidth),
                .Height = static_cast<float>(m_windowHeight),
                .MinDepth = 0.0f,
                .MaxDepth = 1.0f,
            });

            gctx->setPrimitiveTopologyLayout(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            gctx->setRenderTarget(m_offscreenRenderTarget, m_depthTexture);

            m_scene->renderLights(gctx.get());

           m_scene->renderCubeMap(gctx.get());
            gctx->addResourceBarrier(m_offscreenRenderTarget.allocation.resource.Get(),
                                     D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            gctx->executeResourceBarriers();
        }

        // RenderPass 6 : Bloom Pass
        {
            m_bloomPass->render(gctx.get(), m_offscreenRenderTarget, m_windowWidth, m_windowHeight);
        }

        // RenderPass 7 : Post Processing Stage:
        {
            gctx->setGraphicsRootSignatureAndPipeline(m_postProcessingPipelineState);
            gctx->setViewport(D3D12_VIEWPORT{
                .TopLeftX = 0.0f,
                .TopLeftY = 0.0f,
                .Width = static_cast<float>(m_windowWidth),
                .Height = static_cast<float>(m_windowHeight),
                .MinDepth = 0.0f,
                .MaxDepth = 1.0f,
            });

            gctx->setPrimitiveTopologyLayout(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            gctx->setRenderTarget(m_postProcessingRenderTarget, m_fullScreenPassDepthTexture);

            interlop::PostProcessingRenderResources renderResources = {
                .postProcessBufferIndex = m_postProcessingBuffer.cbvIndex,
                .renderTextureIndex = m_offscreenRenderTarget.srvIndex,
                .ssaoTextureIndex = m_ssaoPass->m_blurSSAOTexture.srvIndex,
                .bloomTextureIndex = m_bloomPass->m_bloomUpSampleTexture.srvIndex,
            };

            gctx->set32BitGraphicsConstants(&renderResources);
            gctx->setIndexBuffer(m_renderTargetIndexBuffer);
            gctx->drawInstanceIndexed(3u);
        }

        // Render pass 8 : Render post processing render target to swapchain backbuffer via a full screen triangle pass.
        {
            gctx->addResourceBarrier(m_offscreenRenderTarget.allocation.resource.Get(),
                                     D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

            gctx->addResourceBarrier(m_postProcessingRenderTarget.allocation.resource.Get(),
                                     D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

            gctx->addResourceBarrier(currentBackBuffer.allocation.resource.Get(), D3D12_RESOURCE_STATE_PRESENT,
                                     D3D12_RESOURCE_STATE_RENDER_TARGET);

            gctx->executeResourceBarriers();

            gctx->clearRenderTargetView(currentBackBuffer, clearColor);

            gctx->setGraphicsRootSignatureAndPipeline(m_fullScreenTrianglePassPipelineState);
            gctx->setViewport(D3D12_VIEWPORT{
                .TopLeftX = 0.0f,
                .TopLeftY = 0.0f,
                .Width = static_cast<float>(m_windowWidth),
                .Height = static_cast<float>(m_windowHeight),
                .MinDepth = 0.0f,
                .MaxDepth = 1.0f,
            });

            gctx->setPrimitiveTopologyLayout(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            gctx->setRenderTarget(currentBackBuffer);

            interlop::FullScreenTrianglePassRenderResources renderResources = {
                .renderTextureIndex = m_postProcessingRenderTarget.srvIndex,
            };

            gctx->set32BitGraphicsConstants(&renderResources);
            gctx->setIndexBuffer(m_renderTargetIndexBuffer);
            gctx->drawInstanceIndexed(3u);

            m_editor->render(m_graphicsDevice.get(), m_scene.get(), m_deferredGPass->m_gBuffer,
                             m_shadowMappingPass.value(), m_ssaoPass.value(), m_bloomPass.value(),
                             m_postProcessingBufferData, m_postProcessingRenderTarget, gctx.get());

            gctx->addResourceBarrier(m_postProcessingRenderTarget.allocation.resource.Get(),
                                     D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

            gctx->addResourceBarrier(currentBackBuffer.allocation.resource.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
                                     D3D12_RESOURCE_STATE_PRESENT);

            gctx->executeResourceBarriers();
        }

        const std::array<gfx::Context* const, 1u> contexts = {
            gctx.get(),
        };

        m_graphicsDevice->getDirectCommandQueue()->executeContext(contexts);

        m_graphicsDevice->present();
        m_graphicsDevice->endFrame();

        m_frameCount++;
    }

  private:
    gfx::Texture m_offscreenRenderTarget{};
    gfx::Texture m_postProcessingRenderTarget{};

    gfx::PipelineState m_pipelineState{};
    gfx::PipelineState m_postProcessingPipelineState{};
    gfx::PipelineState m_fullScreenTrianglePassPipelineState{};

    gfx::Texture m_depthTexture{};
    gfx::Texture m_fullScreenPassDepthTexture{};

    gfx::Buffer m_renderTargetIndexBuffer{};

    gfx::Buffer m_postProcessingBuffer{};
    interlop::PostProcessingBuffer m_postProcessingBufferData{};

    std::optional<rendering::DeferredGeometryPass> m_deferredGPass{};
    std::optional<rendering::IBL> m_ibl{};
    std::optional<rendering::PCFShadowMappingPass> m_shadowMappingPass{};
    std::optional<rendering::SSAOPass> m_ssaoPass{};
    std::optional<rendering::BloomPass> m_bloomPass{};

    gfx::Texture m_irradianceTexture{};
    gfx::Texture m_prefilterTexture{};
    gfx::Texture m_brdfLUTTexture{};

    uint64_t m_frameCount{};
};

int main()
{
    SandBox sandbox{"Helios::SandBox"};
    sandbox.run();

    return 0;
}