#pragma once

#include "../Graphics/PipelineState.hpp"
#include "../Graphics/Resources.hpp"

#include "../Scene/Scene.hpp"

namespace helios::gfx
{
    class GraphicsDevice;
    class GraphicsContext;
}

namespace helios::rendering
{
    // Holds the different render targets that constitute the geometry buffer (GBuffer).
    // The view space position will be reconstructed from the depth buffer.

    // Geometry buffer breakdown:
    // float4 albedoEmissive : SV_Target0;
    // float4 normalEmissive : SV_Target1;
    // float4 aoMetalRoughnessEmissive : SV_Target2;
    struct DeferredGeometryBuffer
    {
        gfx::Texture albedoEmissiveRT{};
        gfx::Texture normalEmissiveRT{};
        gfx::Texture aoMetalRoughnessEmissiveRT{};
    };

    // This abstraction produces MRT's for various attributes (aoMetalRoughness, albedo, normal etc) for a given scene.
    class DeferredGeometryPass
    {
      public:
        DeferredGeometryPass(const gfx::GraphicsDevice* const device, const uint32_t width, const uint32_t height);

        void render(scene::Scene& scene, gfx::GraphicsContext* const graphicsContext, gfx::Texture& depthBuffer, const uint32_t width, const uint32_t height);

      public:
        DeferredGeometryBuffer m_gBuffer{};
        gfx::PipelineState m_deferredGPassPipelineState{};
    };

} // namespace helios::gfx