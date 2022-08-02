#include "Pch.hpp"
#include "SandBox.hpp"

#include "Common/BindlessRS.hlsli" 
#include "Common/ConstantBuffers.hlsli"

using namespace helios;
using namespace DirectX;

SandBox::SandBox(core::Config& config)
	: Engine(config)
{
}

void SandBox::OnInit()
{
	mDevice = std::make_unique<gfx::Device>();

	// Load scene and its data.
	mScene = std::make_unique<scene::Scene>(mDevice.get());

	scene::ModelCreationDesc DamagedHelmetCreationDesc
	{
		.modelPath = L"Assets/Models/DamagedHelmet/glTF/DamagedHelmet.glTF",
		.modelName = L"DamagedHelmet",
	};

	auto damagedHelmet = std::make_unique<scene::Model>(mDevice.get(), DamagedHelmetCreationDesc);
	damagedHelmet->GetTransform()->data.rotation = { math::XMConvertToRadians(63.0f), 0.0f, 0.0f };
	mScene->AddModel(std::move(damagedHelmet));

	scene::ModelCreationDesc sciFiHelmetCreationDesc
	{
		.modelPath = L"Assets/Models/SciFiHelmet/glTF/SciFiHelmet.gltf",
		.modelName = L"SciFiHelmet",
	};

	auto sciFiHelmet = std::make_unique<scene::Model>(mDevice.get(), sciFiHelmetCreationDesc);
	sciFiHelmet->GetTransform()->data.translate = { 5.0f, 0.0f, 0.0f };
	mScene->AddModel(std::move(sciFiHelmet));

	scene::ModelCreationDesc metalRoughSpheresCreationDesc
	{
		.modelPath = L"Assets/Models/MetalRoughSpheres/glTF/MetalRoughSpheres.gltf",
		.modelName = L"MetalRoughSpheres",
	};

	auto metalRoughSpheres = std::make_unique<scene::Model>(mDevice.get(), metalRoughSpheresCreationDesc);
	metalRoughSpheres->GetTransform()->data.translate = { -15.0f, 0.0f, 0.0f };
	mScene->AddModel(std::move(metalRoughSpheres));


	// Load lights.
	scene::LightCreationDesc directionalLightCreationDesc
	{
		.lightNumber = 0u,
		.lightType = scene::LightTypes::DirectionalLightData
	};

	mScene->AddLight(mDevice.get(), directionalLightCreationDesc);

	scene::LightCreationDesc pointLightcreationDesc1
	{
		.lightNumber = 1u,
		.lightType = scene::LightTypes::PointLightData
	};

	mScene->AddLight(mDevice.get(), pointLightcreationDesc1);

	// Create post process buffer.
	gfx::BufferCreationDesc postProcessBufferCreationDesc
	{
		.usage = gfx::BufferUsage::ConstantBuffer,
		.name = L"Post Process Buffer",
	};

	mPostProcessBuffer = std::make_unique<gfx::Buffer>(mDevice->CreateBuffer<PostProcessBuffer>(postProcessBufferCreationDesc, std::span<PostProcessBuffer, 0u>{}));
	mPostProcessBufferData =
	{
		.exposure = 1.0f
	};

	// Load pipeline states.
	gfx::GraphicsPipelineStateCreationDesc graphicsPipelineStateCreationDesc
	{
		.vsShaderPath = L"Shaders/OffscreenRTVS.cso",
		.psShaderPath = L"Shaders/OffscreenRTPS.cso",
		.rtvFormat = DXGI_FORMAT_R8G8B8A8_UNORM,
		.depthFormat = DXGI_FORMAT_D32_FLOAT,
		.pipelineName = L"Mesh Viewer Pipeline"
	};

	mPipelineState = std::make_unique<gfx::PipelineState>(mDevice->CreatePipelineState(graphicsPipelineStateCreationDesc));
	
	gfx::GraphicsPipelineStateCreationDesc pbrPipelineStateCreationDesc
	{
		.vsShaderPath = L"Shaders/PBR/PBRVS.cso",
		.psShaderPath = L"Shaders/PBR/PBRPS.cso",
		.rtvFormat = DXGI_FORMAT_R16G16B16A16_FLOAT,
		.depthFormat = DXGI_FORMAT_D32_FLOAT,
		.pipelineName = L"PBR Pipeline"
	};

	mPBRPipelineState = std::make_unique<gfx::PipelineState>(mDevice->CreatePipelineState(pbrPipelineStateCreationDesc));
	
	gfx::GraphicsPipelineStateCreationDesc finalRenderPassPipelineStateCreationDesc
	{
		.vsShaderPath = L"Shaders/RenderPass/FinalRenderPassVS.cso",
		.psShaderPath = L"Shaders/RenderPass/FinalRenderPassPS.cso",
		.rtvFormat = DXGI_FORMAT_R8G8B8A8_UNORM,
		.depthFormat = DXGI_FORMAT_D32_FLOAT,
		.pipelineName = L"Final Render Target Pipeline"
	};

	mFinalPipelineState = std::make_unique<gfx::PipelineState>(mDevice->CreatePipelineState(finalRenderPassPipelineStateCreationDesc));


	// Load depth stencil texture.
	gfx::TextureCreationDesc depthStencilTextureCreationDesc
	{
		.usage = gfx::TextureUsage::DepthStencil,
		.dimensions = mDimensions,
		.format = DXGI_FORMAT_R32_FLOAT,
		.name = L"Depth Stencil Texture"
	};

	mDepthStencilTexture = std::make_unique<gfx::Texture>(mDevice->CreateTexture(depthStencilTextureCreationDesc));

	// Load render targets.
	gfx::TextureCreationDesc offscreenRenderTargetTextureCreationDesc
	{
		.usage = gfx::TextureUsage::RenderTarget,
		.dimensions = mDimensions,
		.format = DXGI_FORMAT_R16G16B16A16_FLOAT,
		.name = L"Offscreen Render Texture"
	};

	mOffscreenRT = std::make_unique<gfx::RenderTarget>(mDevice->CreateRenderTarget(offscreenRenderTargetTextureCreationDesc));

	gfx::TextureCreationDesc postProcessRenderTargetsTextureCreationDesc
	{
		.usage = gfx::TextureUsage::RenderTarget,
		.dimensions = mDimensions,
		.format = DXGI_FORMAT_R8G8B8A8_UNORM,
		.name = L"Post Process Render Texture"
	};

	mPostProcessingRT = std::make_unique<gfx::RenderTarget>(mDevice->CreateRenderTarget(postProcessRenderTargetsTextureCreationDesc));
	
	gfx::TextureCreationDesc finalRenderTargetsTextureCreationDesc
	{
		.usage = gfx::TextureUsage::RenderTarget,
		.dimensions = mDimensions,
		.format = DXGI_FORMAT_R8G8B8A8_UNORM,
		.name = L"Final Render Texture"
	};

	mFinalRT = std::make_unique<gfx::RenderTarget>(mDevice->CreateRenderTarget(finalRenderTargetsTextureCreationDesc));

	// Init other scene objects.
	mEditor = std::make_unique<editor::Editor>(mDevice.get());
}

void SandBox::OnUpdate()
{
	mScene->Update(mAspectRatio);

	mPostProcessBuffer->Update(&mPostProcessBufferData);
}

void SandBox::OnRender()
{
	std::unique_ptr<gfx::GraphicsContext> graphicsContext = mDevice->GetGraphicsContext();
	gfx::BackBuffer* backBuffer = mDevice->GetCurrentBackBuffer();

	mDevice->BeginFrame();

	// Configure offscreen render target's.
	std::array<const gfx::RenderTarget*, 1u> renderTargets
	{
		mOffscreenRT.get()
	};

	static std::array<float, 4> clearColor{ 0.0f, 0.0f, 0.0f, 1.0f };
	static float exposure{ 1.0f };
	
	// RenderPass 1 : Render the model's to the offscreen render target.
	{
		graphicsContext->AddResourceBarrier(renderTargets, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
		graphicsContext->ExecuteResourceBarriers();

		graphicsContext->SetGraphicsPipelineState(mPBRPipelineState.get());
		graphicsContext->SetRenderTarget(renderTargets, mDepthStencilTexture.get());
		graphicsContext->SetDefaultViewportAndScissor();
		graphicsContext->SetPrimitiveTopologyLayout(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		graphicsContext->ClearRenderTargetView(renderTargets, clearColor);
		graphicsContext->ClearDepthStencilView(mDepthStencilTexture.get(), 1.0f);

		mScene->RenderModels(graphicsContext.get());

		graphicsContext->AddResourceBarrier(renderTargets, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	}
	
	// Render pass 2 : Render offscreen rt to post processed RT (after all processing has occured).
	{

		graphicsContext->AddResourceBarrier(mPostProcessingRT->GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
		graphicsContext->ExecuteResourceBarriers();

		graphicsContext->SetGraphicsPipelineState(mPipelineState.get());
		graphicsContext->SetRenderTarget(mPostProcessingRT.get(), mDepthStencilTexture.get());
		graphicsContext->SetDefaultViewportAndScissor();
		graphicsContext->SetPrimitiveTopologyLayout(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		graphicsContext->ClearRenderTargetView(mPostProcessingRT.get(), std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f});
		graphicsContext->ClearDepthStencilView(mDepthStencilTexture.get(), 1.0f);

		// Note : buffer indices can be set here or in the RenderTarget::Render function. Begin done there for now.
		RenderTargetRenderResources rtvRenderResources
		{
			.textureIndex = mOffscreenRT->renderTexture->srvIndex,
			.postProcessBufferIndex = mPostProcessBuffer->cbvIndex
		};

		gfx::RenderTarget::Render(graphicsContext.get(), rtvRenderResources);
	}

	// Render pass 3 : The RT that is to be displayed to swapchain is processed. For now, UI is rendered in this RT as well.
	{
		graphicsContext->AddResourceBarrier(mPostProcessingRT->GetResource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		graphicsContext->AddResourceBarrier(mFinalRT->GetResource(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
		graphicsContext->ExecuteResourceBarriers();

		graphicsContext->SetGraphicsPipelineState(mFinalPipelineState.get());
		graphicsContext->SetRenderTarget(mFinalRT.get(), mDepthStencilTexture.get());
		graphicsContext->SetDefaultViewportAndScissor();
		graphicsContext->SetPrimitiveTopologyLayout(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		graphicsContext->ClearDepthStencilView(mDepthStencilTexture.get(), 1.0f);

		// Note : buffer indices can be set here or in the RenderTarget::Render function. Begin done there for now.
		RenderTargetRenderResources rtvRenderResources
		{
			.textureIndex = mPostProcessingRT->renderTexture->srvIndex,
		};

		gfx::RenderTarget::Render(graphicsContext.get(), rtvRenderResources);

		mEditor->Render(mDevice.get(), mScene.get(), clearColor, mPostProcessBufferData, mDevice->GetTextureSrvDescriptorHandle(mPostProcessingRT->renderTexture.get()), graphicsContext.get());
	}

	// Render pass 3 : Copy the final RT to the swapchain
	{
		graphicsContext->AddResourceBarrier(mFinalRT->GetResource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
		graphicsContext->AddResourceBarrier(backBuffer->GetResource(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST);
		graphicsContext->ExecuteResourceBarriers();

		graphicsContext->CopyResource(mFinalRT->GetResource(), backBuffer->GetResource());

		graphicsContext->AddResourceBarrier(backBuffer->GetResource(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT);
		graphicsContext->ExecuteResourceBarriers();
	}


	mDevice->EndFrame();

	mDevice->ExecuteContext(std::move(graphicsContext));

	mDevice->Present();

	mFrameIndex++;
}

void SandBox::OnDestroy()
{
	scene::Light::DestroyLightResources();
	mEditor.reset();
}

void SandBox::OnKeyAction(uint8_t keycode, bool isKeyDown) 
{
	if (isKeyDown && keycode == VK_SPACE)
	{
		mEditor->ShowUI(false);
	}

	if (isKeyDown && keycode == VK_SHIFT)
	{
		mEditor->ShowUI(true);
	}

	mScene->mCamera->HandleInput(keycode, isKeyDown);
}

void SandBox::OnResize() 
{
	if (mDimensions != core::Application::GetClientDimensions())
	{
		mDevice->ResizeBuffers();

		mDimensions = core::Application::GetClientDimensions();

		mAspectRatio = static_cast<float>(mDimensions.x) / static_cast<float>(mDimensions.y);

		// Recreate RTV and SRV of all render targets.
		mDevice->ResizeRenderTarget(mFinalRT.get());
		mDevice->ResizeRenderTarget(mOffscreenRT.get());
		mDevice->ResizeRenderTarget(mPostProcessingRT.get());

		mEditor->OnResize(core::Application::GetClientDimensions());
	}
}
