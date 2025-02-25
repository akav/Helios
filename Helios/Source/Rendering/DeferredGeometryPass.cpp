#include "Rendering/DeferredGeometryPass.hpp"

#include "Graphics/GraphicsDevice.hpp"

// Geometry buffer breakdown:
// float4 albedoEmissive : SV_Target0;
// float4 normalEmissive : SV_Target1;
// float4 aoMetalRoughnessEmissive : SV_Target2;

namespace helios::rendering
{
    DeferredGeometryPass::DeferredGeometryPass(const gfx::GraphicsDevice* const graphicsDevice, const uint32_t width,
                                               const uint32_t height)
    {
        // Create pipeline state.
        m_deferredGPassPipelineState = graphicsDevice->createPipelineState(gfx::GraphicsPipelineStateCreationDesc{
            .shaderModule =
                {
                    .vertexShaderPath = L"Shaders/RenderPass/DeferredGeometryPass.hlsl",
                    .pixelShaderPath = L"Shaders/RenderPass/DeferredGeometryPass.hlsl",
                },
            .rtvFormats =
                {
                    DXGI_FORMAT_R8G8B8A8_UNORM,
                    DXGI_FORMAT_R16G16B16A16_FLOAT,
                    DXGI_FORMAT_R8G8B8A8_UNORM,
                },
            .rtvCount = 3,
            .pipelineName = L"Deferred Geometry Pass Pipeline",
        });

        // Create MRT's for GBuffer.
        m_gBuffer.albedoEmissiveRT = graphicsDevice->createTexture(gfx::TextureCreationDesc{
            .usage = gfx::TextureUsage::RenderTarget,
            .width = width,
            .height = height,
            .format = DXGI_FORMAT_R8G8B8A8_UNORM,
            .optionalInitialState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
            .name = L"Deferred Pass Albedo Texture",
        });

        m_gBuffer.normalEmissiveRT = graphicsDevice->createTexture(gfx::TextureCreationDesc{
            .usage = gfx::TextureUsage::RenderTarget,
            .width = width,
            .height = height,
            .format = DXGI_FORMAT_R16G16B16A16_FLOAT,
            .optionalInitialState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
            .name = L"Deferred Pass Normal Emissive Texture",
        });

        m_gBuffer.aoMetalRoughnessEmissiveRT = graphicsDevice->createTexture(gfx::TextureCreationDesc{
            .usage = gfx::TextureUsage::RenderTarget,
            .width = width,
            .height = height,
            .format = DXGI_FORMAT_R8G8B8A8_UNORM,
            .optionalInitialState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
            .name = L"Deferred Pass AO Metal Roughness Emissive Texture",
        });
    }

    void DeferredGeometryPass::render(scene::Scene& scene, gfx::GraphicsContext* const graphicsContext,
                                      gfx::Texture& depthBuffer, const uint32_t width, const uint32_t height)
    {
        std::array<const gfx::Texture, 3u> renderTargets = {

            m_gBuffer.albedoEmissiveRT,
            m_gBuffer.normalEmissiveRT,
            m_gBuffer.aoMetalRoughnessEmissiveRT,
        };

        // Setup of resource barriers done in the SandBox code.
        // for (const auto& renderTarget : renderTargets)
        // {
        //     graphicsContext->addResourceBarrier(renderTarget.allocation.resource.Get(),
        //                                         D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        //                                         D3D12_RESOURCE_STATE_RENDER_TARGET);
        // }

        // graphicsContext->executeResourceBarriers();

        graphicsContext->setGraphicsPipelineState(m_deferredGPassPipelineState);
        graphicsContext->setRenderTarget(renderTargets, depthBuffer);
        graphicsContext->setViewport(D3D12_VIEWPORT{
            .TopLeftX = 0.0f,
            .TopLeftY = 0.0f,
            .Width = static_cast<float>(width),
            .Height = static_cast<float>(height),
            .MinDepth = 0.0f,
            .MaxDepth = 1.0f,
        });

        graphicsContext->setPrimitiveTopologyLayout(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        graphicsContext->clearRenderTargetView(renderTargets, std::array<float, 4u>{0.0f, 0.0f, 0.0f, 0.0f});
        graphicsContext->clearDepthStencilView(depthBuffer);

        interlop::DeferredGPassRenderResources deferredGPassRenderResources{};

        scene.renderModels(graphicsContext, deferredGPassRenderResources);
           
        // Considering that the GBuffer will be used as SRV only for the shading pass, the barrier setup and execution is moved to the sandbox render function.
        // The resource barriers are executed in the SandBox's render loop for batching barriers.
        // Uncomment if barrier execution is to happen here.
        // for (const auto& renderTarget : renderTargets)
        // {
        //     graphicsContext->addResourceBarrier(renderTarget.allocation.resource.Get(),
        //                                         D3D12_RESOURCE_STATE_RENDER_TARGET,
        //                                         D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        // }

        // graphicsContext->executeResourceBarriers();
    }
} // namespace helios::renderpass