#include "Graphics/GraphicsDevice.hpp"

#include "Core/FileSystem.hpp"

#include <stb_image.h>

namespace helios::gfx
{
    GraphicsDevice::GraphicsDevice(const uint32_t windowWidth, const uint32_t windowHeight,
                                   const DXGI_FORMAT swapchainFormat, const HWND windowHandle)
        : m_swapchainBackBufferFormat(swapchainFormat), m_windowHandle(windowHandle)
    {
        initDeviceResources();
        initSwapchainResources(windowWidth, windowHeight);

        m_isInitialized = true;
    }

    GraphicsDevice::~GraphicsDevice()
    {
        m_directCommandQueue->flush();
    }

    void GraphicsDevice::initDeviceResources()
    {
        initD3D12Core();
        initCommandQueues();
        initDescriptorHeaps();
        initMemoryAllocator();
        initContexts();
        initBindlessRootSignature();
        initMipMapGenerator();
    }

    void GraphicsDevice::initSwapchainResources(const uint32_t windowWidth, const uint32_t windowHeight)
    {
        // Check if the tearing feature is supported. Vsync currently not supported.
        BOOL tearingSupported = TRUE;
        if (FAILED(m_factory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearingSupported,
                                                  sizeof(tearingSupported))))
        {
            tearingSupported = FALSE;
        }

        m_tearingSupported = tearingSupported;

        const DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {
            .Width = windowWidth,
            .Height = windowHeight,
            .Format = m_swapchainBackBufferFormat,
            .Stereo = FALSE,
            .SampleDesc{
                .Count = 1,
                .Quality = 0,
            },
            .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
            .BufferCount = FRAMES_IN_FLIGHT,
            .Scaling = DXGI_SCALING_STRETCH,
            .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
            .AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED,
            .Flags = m_tearingSupported ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0u,
        };

        wrl::ComPtr<IDXGISwapChain1> swapChain1;
        throwIfFailed(m_factory->CreateSwapChainForHwnd(m_directCommandQueue->getCommandQueue(), m_windowHandle,
                                                        &swapChainDesc, nullptr, nullptr, &swapChain1));

        // Prevent DXGI from switching to full screen state automatically while using ALT + ENTER combination.
        throwIfFailed(m_factory->MakeWindowAssociation(m_windowHandle, DXGI_MWA_NO_ALT_ENTER));

        throwIfFailed(swapChain1.As(&m_swapchain));

        m_currentFrameIndex = m_swapchain->GetCurrentBackBufferIndex();

        createBackBufferRTVs();
    }

    void GraphicsDevice::initD3D12Core()
    {
        // Enable the D3D12 debug layer in Debug build configurations.
        if constexpr (HELIOS_DEBUG_MODE)
        {
            throwIfFailed(::D3D12GetDebugInterface(IID_PPV_ARGS(&m_debug)));
            m_debug->EnableDebugLayer();
            m_debug->SetEnableGPUBasedValidation(true);
            m_debug->SetEnableSynchronizedCommandQueueValidation(true);
        }

        // Create the DXGI factory (can be used to enumerate adapters and other information on the graphics subsystem
        // (memory, adapters, etc)).
        uint32_t dxgiFactoryCreationFlags{};
        if constexpr (HELIOS_DEBUG_MODE)
        {
            dxgiFactoryCreationFlags = DXGI_CREATE_FACTORY_DEBUG;
        }

        throwIfFailed(::CreateDXGIFactory2(dxgiFactoryCreationFlags, IID_PPV_ARGS(&m_factory)));

        // Select the adapter (in this case GPU with best performance).
        throwIfFailed(
            m_factory->EnumAdapterByGpuPreference(0u, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&m_adapter)));

        if (!m_adapter)
        {
            fatalError("Failed to find D3D12 compatible adapter");
        }

        // Display information about the selected adapter.
        DXGI_ADAPTER_DESC adapterDesc{};
        throwIfFailed(m_adapter->GetDesc(&adapterDesc));
        log(std::format(L"Chosen adapter : {}.", adapterDesc.Description));

        // Create D3D12 Device.
        throwIfFailed(::D3D12CreateDevice(m_adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&m_device)));
        m_device->SetName(L"D3D12 Device");

        // Set breakpoints on certain debug messages in debug build configurations.
        if constexpr (HELIOS_DEBUG_MODE)
        {
            wrl::ComPtr<ID3D12InfoQueue> infoQueue{};
            throwIfFailed(m_device->QueryInterface(IID_PPV_ARGS(&infoQueue)));

            infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
            infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
            infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, true);

            infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_INFO, false);

            // Configure queue filter to ignore info message severity.
            std::array<D3D12_MESSAGE_SEVERITY, 1> ignoreMessageSeverities = {
                D3D12_MESSAGE_SEVERITY_INFO,
            };

            // Configure queue filter to ignore individual messages using their ID.
            std::array<D3D12_MESSAGE_ID, 2> ignoreMessageIDs = {
                D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE,
                D3D12_MESSAGE_ID_CLEARDEPTHSTENCILVIEW_MISMATCHINGCLEARVALUE,
            };

            D3D12_INFO_QUEUE_FILTER infoQueueFilter = {
                .DenyList =
                    {
                        .NumSeverities = static_cast<UINT>(ignoreMessageSeverities.size()),
                        .pSeverityList = ignoreMessageSeverities.data(),
                        .NumIDs = static_cast<UINT>(ignoreMessageIDs.size()),
                        .pIDList = ignoreMessageIDs.data(),
                    },
            };

            throwIfFailed(infoQueue->PushStorageFilter(&infoQueueFilter));

            // Get the debug device. It represents a graphics device for debugging, while the debug interface controls
            // debug settings and validates pipeline state. Debug device can be used to check for reporting live objects
            // and leaks.
            throwIfFailed(m_device->QueryInterface(IID_PPV_ARGS(&m_debugDevice)));
        }
    }

    void GraphicsDevice::initCommandQueues()
    {
        // Create the command queue's.
        m_directCommandQueue =
            std::make_unique<CommandQueue>(m_device.Get(), D3D12_COMMAND_LIST_TYPE_DIRECT, L"Direct Command Queue");

        m_copyCommandQueue =
            std::make_unique<CommandQueue>(m_device.Get(), D3D12_COMMAND_LIST_TYPE_COPY, L"Copy Command Queue");

        m_computeCommandQueue =
            std::make_unique<CommandQueue>(m_device.Get(), D3D12_COMMAND_LIST_TYPE_COMPUTE, L"Compute Command Queue");
    }

    void GraphicsDevice::initDescriptorHeaps()
    {
        // Create descriptor heaps.
        m_cbvSrvUavDescriptorHeap = std::make_unique<DescriptorHeap>(
            m_device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 10'000u, L"CBV SRV UAV Descriptor Heap");

        m_rtvDescriptorHeap = std::make_unique<DescriptorHeap>(m_device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 50u,
                                                               L"RTV Descriptor Heap");

        m_dsvDescriptorHeap = std::make_unique<DescriptorHeap>(m_device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 50u,
                                                               L"DSV Descriptor Heap");

        m_samplerDescriptorHeap = std::make_unique<DescriptorHeap>(m_device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,
                                                                   1024u, L"Sampler Descriptor Heap");
    }

    void GraphicsDevice::initMemoryAllocator()
    {
        m_memoryAllocator = std::make_unique<MemoryAllocator>(m_device.Get(), m_adapter.Get());
    }

    void GraphicsDevice::initContexts()
    {
        // Create graphics contexts (one per frame in flight).
        for (const uint32_t i : std::views::iota(0u, FRAMES_IN_FLIGHT))
        {
            for (auto& context : m_perFrameGraphicsContexts[i])
            {
                context = std::make_unique<GraphicsContext>(this);
            }
        }

        m_copyContext = std::make_unique<CopyContext>(this);
        m_computeContextQueue.push(std::make_unique<ComputeContext>(this));
    }

    void GraphicsDevice::initBindlessRootSignature()
    {
        // Setup bindless root signature.
        gfx::PipelineState::createBindlessRootSignature(m_device.Get(), L"Shaders/Triangle.hlsl");
    }

    void GraphicsDevice::initMipMapGenerator()
    {
        m_mipMapGenerator = std::make_unique<MipMapGenerator>(this);
    }

    void GraphicsDevice::createBackBufferRTVs()
    {
        DescriptorHandle rtvHandle = m_rtvDescriptorHeap->getDescriptorHandleFromStart();

        // Create Backbuffer render target views.
        for (const uint32_t i : std::views::iota(0u, FRAMES_IN_FLIGHT))
        {
            wrl::ComPtr<ID3D12Resource> backBuffer{};
            throwIfFailed(m_swapchain->GetBuffer(i, IID_PPV_ARGS(&backBuffer)));

            m_device->CreateRenderTargetView(backBuffer.Get(), nullptr, rtvHandle.cpuDescriptorHandle);

            m_backBuffers[i].allocation.resource = backBuffer;
            m_backBuffers[i].allocation.resource->SetName(L"SwapChain BackBuffer");
            m_backBuffers[i].rtvIndex = m_rtvDescriptorHeap->getDescriptorIndex(rtvHandle);

            m_rtvDescriptorHeap->offsetDescriptor(rtvHandle);
        }

        if (!m_isInitialized)
        {
            m_rtvDescriptorHeap->offsetCurrentHandle(FRAMES_IN_FLIGHT);
        }
    }

    std::unique_ptr<ComputeContext> GraphicsDevice::getComputeContext()
    {
        if (!m_computeContextQueue.empty())
        {
            std::unique_ptr<ComputeContext> context = std::move(m_computeContextQueue.front());
            m_computeContextQueue.pop();
            return context;
        }
        else
        {
            // Create a compute context.
            std::unique_ptr<ComputeContext> context = std::make_unique<ComputeContext>(this);
            return context;
        }
    }

    void GraphicsDevice::executeAndFlushComputeContext(std::unique_ptr<ComputeContext>&& computeContext)
    {
        // Execute compute context and push to the queue.
        std::array<const Context*, 1u> contexts = {computeContext.get()};
        m_computeCommandQueue->executeContext(contexts);
        m_computeCommandQueue->flush();

        m_computeContextQueue.emplace(std::move(computeContext));
    }

    void GraphicsDevice::beginFrame()
    {
        for (auto& context : m_perFrameGraphicsContexts[m_currentFrameIndex])
        {
            context->reset();
        }
    }

    void GraphicsDevice::present()
    {
        throwIfFailed(m_swapchain->Present(1u, 0u));
    }

    void GraphicsDevice::endFrame()
    {
        m_fenceValues[m_currentFrameIndex].directQueueFenceValue = m_directCommandQueue->signal();

        m_currentFrameIndex = m_swapchain->GetCurrentBackBufferIndex();

        m_directCommandQueue->waitForFenceValue(m_fenceValues[m_currentFrameIndex].directQueueFenceValue);
    }

    void GraphicsDevice::resizeWindow(const uint32_t windowWidth, const uint32_t windowHeight)
    {
        m_directCommandQueue->flush();
        m_copyCommandQueue->flush();

        // All swapchain back buffers need to be released.
        for (const uint32_t i : std::views::iota(0u, FRAMES_IN_FLIGHT))
        {
            m_backBuffers[i].allocation.resource.Reset();
            m_fenceValues[i].directQueueFenceValue = m_directCommandQueue->getCurrentCompletedFenceValue();
        }

        DXGI_SWAP_CHAIN_DESC swapchainDesc{};
        throwIfFailed(m_swapchain->GetDesc(&swapchainDesc));

        throwIfFailed(m_swapchain->ResizeBuffers(FRAMES_IN_FLIGHT, windowWidth, windowHeight,
                                                 m_swapchainBackBufferFormat, swapchainDesc.Flags));

        m_currentFrameIndex = m_swapchain->GetCurrentBackBufferIndex();

        createBackBufferRTVs();
    }

    Texture GraphicsDevice::createTexture(const TextureCreationDesc& paramTextureCreationDesc, const void* data) const
    {
        Texture texture{};

        // The memory allocator changes some variables of the TextureCreationDesc, so to prevent making the function's
        // input parameter non const, this approach of making a local copy is taken.
        TextureCreationDesc textureCreationDesc = paramTextureCreationDesc;

        textureCreationDesc.path = core::FileSystem::getFullPath(textureCreationDesc.path);

        int32_t componentCount = 4;
        int32_t width{};
        int32_t height{};

        // For use only by HDR textures (mostly for Cube Map equirectangular textures).
        float* hdrTextureData{nullptr};

        // For use by textures that are non HDR and going to be loaded using stbi.
        void* textureData{(void*)(data)};

        if (textureCreationDesc.usage == TextureUsage::TextureFromData)
        {
            width = textureCreationDesc.width;
            height = textureCreationDesc.height;
        }
        else if (textureCreationDesc.usage == TextureUsage::TextureFromPath)
        {
            textureData =
                stbi_load(wStringToString(textureCreationDesc.path).c_str(), &width, &height, nullptr, componentCount);

            if (!textureData)
            {
                fatalError(
                    std::format("Failed to load texture from path : {}.", wStringToString(textureCreationDesc.path)));
            }

            textureCreationDesc.width = width;
            textureCreationDesc.height = height;
        }
        else if (textureCreationDesc.usage == TextureUsage::HDRTextureFromPath)
        {
            hdrTextureData =
                stbi_loadf(wStringToString(textureCreationDesc.path).c_str(), &width, &height, nullptr, componentCount);

            if (!hdrTextureData)
            {
                fatalError(
                    std::format("Failed to load texture from path : {}.", wStringToString(textureCreationDesc.path)));
            }

            textureCreationDesc.width = width;
            textureCreationDesc.height = height;
        }

        std::scoped_lock<std::recursive_mutex> resourceLockGuard(m_resourceMutex);

        // Create a Allocation for the texture (GPU only memory).
        texture.allocation = m_memoryAllocator->createTextureResourceAllocation(textureCreationDesc);

        texture.width = textureCreationDesc.width;
        texture.height = textureCreationDesc.height;

        const uint32_t mipLevels = textureCreationDesc.mipLevels;

        // Needed here as we can pass formats specific for depth stencil texture (DXGI_FORMAT_D32_FLOAT) or formats used
        // by textures / render targets (DXGI_FORMAT_R32G32B32A32_FLOAT).
        DXGI_FORMAT format = textureCreationDesc.format;
        DXGI_FORMAT dsFormat{};

        switch (textureCreationDesc.format)
        {
        case DXGI_FORMAT_R32_FLOAT:
        case DXGI_FORMAT_D32_FLOAT:
        case DXGI_FORMAT_R32_TYPELESS: {
            dsFormat = DXGI_FORMAT_D32_FLOAT;
            format = DXGI_FORMAT_R32_FLOAT;
        }
        break;

        case DXGI_FORMAT_D24_UNORM_S8_UINT: {
            fatalError("Currently, the renderer does not support depth format of the type D24_S8_UINT. "
                       "Please use one of the X32 types.");
        }
        break;
        }

        // If texture created from file, load data (using stb_image currently) into a upload buffer and copy sub
        // resource data from a upload buffer into the GPU only texture.
        if (textureData || hdrTextureData)
        {
            // Create upload buffer.
            const BufferCreationDesc uploadBufferCreationDesc = {
                .usage = BufferUsage::UploadBuffer,
                .name = L"Upload buffer - " + std::wstring(textureCreationDesc.name),
            };

            const UINT64 uploadBufferSize = GetRequiredIntermediateSize(texture.allocation.resource.Get(), 0, 1);

            const ResourceCreationDesc resourceCreationDesc =
                ResourceCreationDesc::createBufferResourceCreationDesc(uploadBufferSize);

            Allocation uploadAllocation =
                m_memoryAllocator->createBufferResourceAllocation(uploadBufferCreationDesc, resourceCreationDesc);

            // Specify data to copy.
            D3D12_SUBRESOURCE_DATA textureSubresourceData{};

            if (textureCreationDesc.usage == TextureUsage::HDRTextureFromPath)
            {
                textureSubresourceData = {
                    .pData = hdrTextureData,
                    .RowPitch = width * textureCreationDesc.bytesPerPixel,
                    .SlicePitch = width * height * textureCreationDesc.bytesPerPixel,
                };
            }
            else // TexureUsage:: TextureFromPath (non HDR).
            {
                textureSubresourceData = {
                    .pData = textureData,
                    .RowPitch = width * textureCreationDesc.bytesPerPixel,
                    .SlicePitch = width * height * textureCreationDesc.bytesPerPixel,
                };
            }

            // Use the copy context and execute UpdateSubresources functions on the copy command queue.
            m_copyContext->reset();

            UpdateSubresources(m_copyContext->getCommandList(), texture.allocation.resource.Get(),
                               uploadAllocation.resource.Get(), 0u, 0u, 1u, &textureSubresourceData);

            const std::array<helios::gfx::Context* const, 1u> contexts = {
                m_copyContext.get(),
            };

            m_copyCommandQueue->executeContext(contexts);
            m_copyCommandQueue->flush();

            uploadAllocation.reset();
        }

        // Create descriptors.

        // Create SRV.
        SrvCreationDesc srvCreationDesc{};

        if (textureCreationDesc.depthOrArraySize == 1u)
        {
            srvCreationDesc = {
                .srvDesc =
                    {

                        .Format = format,
                        .ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D,
                        .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
                        .Texture2D =
                            {
                                .MostDetailedMip = 0u,
                                .MipLevels = mipLevels,
                            },
                    },
            };
        }
        else if (textureCreationDesc.depthOrArraySize == 6u)
        {
            srvCreationDesc = {
                .srvDesc =
                    {
                        .Format = format,
                        .ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE,
                        .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
                        .TextureCube =
                            {
                                .MostDetailedMip = 0u,
                                .MipLevels = mipLevels,
                            },
                    },
            };
        }

        texture.srvIndex = createSrv(srvCreationDesc, texture.allocation.resource.Get());

        // Create SRV's for mip levels. Can be accessed by in code by texture.srvIndex + i.
        // Only doing this for textures which are specified as UAV textures.
        if (textureCreationDesc.mipLevels > 1 && textureCreationDesc.usage == TextureUsage::UAVTexture)
        {
            for (const uint32_t i : std::views::iota(1u, textureCreationDesc.mipLevels))
            {
                const uint32_t srvIndex = createSrv(
                    gfx::SrvCreationDesc{
                        .srvDesc =
                            {
                                .Format = format,
                                .ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D,
                                .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
                                .Texture2D =
                                    {
                                        .MostDetailedMip = i,
                                        .MipLevels = 1,
                                    },
                            },
                    },
                    texture.allocation.resource.Get());
            }
        }

        // Create DSV (if applicable).
        if (textureCreationDesc.usage == TextureUsage::DepthStencil)
        {
            const DsvCreationDesc dsvCreationDesc = {
                .dsvDesc =
                    {
                        .Format = dsFormat,
                        .ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D,
                        .Flags = D3D12_DSV_FLAG_NONE,
                        .Texture2D{
                            .MipSlice = 0u,
                        },
                    },
            };

            texture.dsvIndex = createDsv(dsvCreationDesc, texture.allocation.resource.Get());
        }

        // Create RTV (if applicable).
        if (textureCreationDesc.usage == TextureUsage::RenderTarget)
        {
            const RtvCreationDesc rtvCreationDesc = {
                .rtvDesc =
                    {
                        .Format = format,
                        .ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D,
                        .Texture2D{.MipSlice = 0u, .PlaneSlice = 0u},
                    },
            };

            texture.rtvIndex = createRtv(rtvCreationDesc, texture.allocation.resource.Get());
        }

        // Create UAV's is applicable.
        if (textureCreationDesc.usage != TextureUsage::DepthStencil)
        {
            // The Texture will hold the index to only the first uav, but they will be contiguous in nature
            // since only single large descriptor heap is used for each descriptor type.

            if (textureCreationDesc.depthOrArraySize > 1u)
            {
                // Create the remaining uav's.
                for (const uint32_t i : std::views::iota(0u, textureCreationDesc.mipLevels))
                {
                    // uavIndex will not be directly accesible to user, user must add the index to texture.uav
                    // index to retrive it.
                    const uint32_t uavIndex = createUav(
                        UavCreationDesc{
                            .uavDesc =
                                {
                                    .Format = gfx::Texture::getNonSRGBFormat(textureCreationDesc.format),
                                    .ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY,
                                    .Texture2DArray =
                                        {
                                            .MipSlice = i,
                                            .FirstArraySlice = 0u,
                                            .ArraySize = textureCreationDesc.depthOrArraySize,
                                        },
                                },
                        },
                        texture.allocation.resource.Get());

                    if (i == 0u)
                    {
                        texture.uavIndex = uavIndex;
                    }
                }
            }
            else // Texture is just a Texture 2D.
            {
                // Create the remaining uav's.
                for (const uint32_t i : std::views::iota(0u, textureCreationDesc.mipLevels))
                {
                    // uavIndex will not be directly accesible to user, user must add the index to texture.uav
                    // index to retrive it.
                    const uint32_t uavIndex = createUav(
                        UavCreationDesc{
                            .uavDesc =
                                {
                                    .Format = gfx::Texture::getNonSRGBFormat(textureCreationDesc.format),
                                    .ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D,
                                    .Texture2D =
                                        {
                                            .MipSlice = i,
                                            .PlaneSlice = 0u,
                                        },
                                },
                        },
                        texture.allocation.resource.Get());

                    if (i == 0u)
                    {
                        texture.uavIndex = uavIndex;
                    }
                }
            }
        }

        // Generate mip maps.
        m_mipMapGenerator->generateMips(texture);

        return texture;
    }

    Sampler GraphicsDevice::createSampler(const SamplerCreationDesc& samplerCreationDesc) const
    {
        Sampler sampler{};

        sampler.samplerIndex = m_samplerDescriptorHeap->getCurrentDescriptorIndex();
        gfx::DescriptorHandle samplerDescriptorHandle = m_samplerDescriptorHeap->getCurrentDescriptorHandle();

        m_device->CreateSampler(&samplerCreationDesc.samplerDesc, samplerDescriptorHandle.cpuDescriptorHandle);

        m_samplerDescriptorHeap->offsetCurrentHandle();

        return sampler;
    }

    PipelineState GraphicsDevice::createPipelineState(
        const GraphicsPipelineStateCreationDesc& graphicsPipelineStateCreationDesc) const
    {
        PipelineState pipelineState(m_device.Get(), graphicsPipelineStateCreationDesc);

        return pipelineState;
    }

    PipelineState GraphicsDevice::createPipelineState(
        const ComputePipelineStateCreationDesc& computePipelineStateCreationDesc) const
    {
        PipelineState pipelineState(m_device.Get(), computePipelineStateCreationDesc);

        return pipelineState;
    }

    uint32_t GraphicsDevice::createCbv(const CbvCreationDesc& cbvCreationDesc) const
    {
        const uint32_t cbvIndex = m_cbvSrvUavDescriptorHeap->getCurrentDescriptorIndex();

        m_device->CreateConstantBufferView(&cbvCreationDesc.cbvDesc,
                                           m_cbvSrvUavDescriptorHeap->getCurrentDescriptorHandle().cpuDescriptorHandle);

        m_cbvSrvUavDescriptorHeap->offsetCurrentHandle();

        return cbvIndex;
    }

    uint32_t GraphicsDevice::createSrv(const SrvCreationDesc& srvCreationDesc, ID3D12Resource* const resource) const
    {
        const uint32_t srvIndex = m_cbvSrvUavDescriptorHeap->getCurrentDescriptorIndex();

        m_device->CreateShaderResourceView(resource, &srvCreationDesc.srvDesc,
                                           m_cbvSrvUavDescriptorHeap->getCurrentDescriptorHandle().cpuDescriptorHandle);

        m_cbvSrvUavDescriptorHeap->offsetCurrentHandle();

        return srvIndex;
    }

    uint32_t GraphicsDevice::createUav(const UavCreationDesc& uavCreationDesc, ID3D12Resource* const resource) const
    {
        const uint32_t uavIndex = m_cbvSrvUavDescriptorHeap->getCurrentDescriptorIndex();

        m_device->CreateUnorderedAccessView(
            resource, nullptr, &uavCreationDesc.uavDesc,
            m_cbvSrvUavDescriptorHeap->getCurrentDescriptorHandle().cpuDescriptorHandle);

        m_cbvSrvUavDescriptorHeap->offsetCurrentHandle();

        return uavIndex;
    }

    uint32_t GraphicsDevice::createRtv(const RtvCreationDesc& rtvCreationDesc, ID3D12Resource* const resource) const
    {
        const uint32_t rtvIndex = m_rtvDescriptorHeap->getCurrentDescriptorIndex();

        m_device->CreateRenderTargetView(resource, &rtvCreationDesc.rtvDesc,
                                         m_rtvDescriptorHeap->getCurrentDescriptorHandle().cpuDescriptorHandle);

        m_rtvDescriptorHeap->offsetCurrentHandle();

        return rtvIndex;
    }

    uint32_t GraphicsDevice::createDsv(const DsvCreationDesc& dsvCreationDesc, ID3D12Resource* const resource) const
    {
        const uint32_t dsvIndex = m_dsvDescriptorHeap->getCurrentDescriptorIndex();

        m_device->CreateDepthStencilView(resource, &dsvCreationDesc.dsvDesc,
                                         m_dsvDescriptorHeap->getCurrentDescriptorHandle().cpuDescriptorHandle);

        m_dsvDescriptorHeap->offsetCurrentHandle();

        return dsvIndex;
    }
} // namespace helios::gfx