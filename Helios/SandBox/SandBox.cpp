#include "Pch.hpp"

#include "SandBox.hpp"

using namespace helios;

struct LightingData
{
	dx::XMFLOAT4 lightPosition;
	dx::XMVECTOR cameraPosition;
};

enum class ShaderRegisterSpace : uint32_t
{
	VertexShader = 0u,
	PixelShader = 1u
};

enum class RootParameterIndex : uint32_t
{
	ConstantBuffer = 0u,
	RootConstant = 1u,
	DescriptorTable = 2u
};

enum class PBRRootParameterIndex : uint32_t
{
	VertexConstantBuffer = 0u,
	PixelConstantBuffer = 1u,
	PixelRootConstant = 2u,
	DescriptorTable = 3u
};

SandBox::SandBox(Config& config)
	: Engine(config)
{
}

void SandBox::OnInit()
{
	InitRendererCore();

	LoadContent();
}

void SandBox::OnUpdate()
{
	m_Camera.Update(static_cast<float>(Application::GetTimer().GetDeltaTime()));

	m_ViewMatrix = m_Camera.GetViewMatrix();

	m_ProjectionMatrix = dx::XMMatrixPerspectiveFovLH(dx::XMConvertToRadians(m_FOV), m_AspectRatio, 0.1f, 1000.0f);

	m_LightSource.GetTransform().translate.z = static_cast<float>(sin(Application::GetTimer().GetTotalTime() / 1.0f) * 2.0f);
	m_LightSource.GetTransform().translate.x = static_cast<float>(cos(Application::GetTimer().GetTotalTime() / 1.0f) * 2.0f);
	m_LightSource.GetTransform().translate.y = static_cast<float>(sin(Application::GetTimer().GetTotalTime()));

	m_LightSource.GetTransform().scale = dx::XMFLOAT3(0.1f, 0.1f, 0.1f);

	m_PBRMaterial.Update();
}

void SandBox::OnRender()
{
	auto commandList = m_CommandQueue.GetCommandList();
	wrl::ComPtr<ID3D12Resource> currentBackBuffer = m_BackBuffers[m_CurrentBackBufferIndex];
	
	m_UIManager.FrameStart();
	
	auto projectionView = m_ViewMatrix * m_ProjectionMatrix;

	// TODO : Move to OnUpdate soon.
	for (auto& [objectName, gameObject] : m_GameObjects)
	{
		gameObject.UpdateData(objectName);
		gameObject.UpdateTransformData(commandList.Get(), projectionView);
	}

	ImGui::Begin("Sphere");
	m_Sphere.UpdateData(L"Sphere");
	m_Sphere.UpdateTransformData(commandList.Get(), projectionView);
	ImGui::End();

	ImGui::Begin("Material Data");
	ImGui::SliderFloat3("Albedo", &m_PBRMaterial.GetBufferData().albedo.x, 0.0f, 1.0f);
	ImGui::SliderFloat("Metallic Factor", &m_PBRMaterial.GetBufferData().metallicFactor, 0.0f, 1.0f);
	ImGui::SliderFloat("Roughness Factor", &m_PBRMaterial.GetBufferData().roughnessFactor, 0.0f, 1.0f);
	ImGui::End();

	m_LightSource.UpdateTransformData(commandList.Get(), projectionView);

	// Set the necessary states
	commandList->SetPipelineState(m_PSO.Get());
	commandList->SetGraphicsRootSignature(m_RootSignature.Get());
	
	std::array<ID3D12DescriptorHeap*, 1> descriptorHeaps
	{
		m_SRV_CBV_UAV_Descriptor.GetDescriptorHeap()
	};
	
	commandList->SetDescriptorHeaps(static_cast<uint32_t>(descriptorHeaps.size()), descriptorHeaps.data());
	commandList->SetGraphicsRootDescriptorTable(2u, m_SRV_CBV_UAV_Descriptor.GetGPUDescriptorHandleForStart());

	commandList->RSSetViewports(1u, &m_Viewport);
	commandList->RSSetScissorRects(1u, &m_ScissorRect);
	
	// Inidicate back buffer will be used as RTV.
	gfx::utils::TransitionResource(commandList.Get(), currentBackBuffer.Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

	// Record rendering commands
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_RTVDescriptor.GetCPUDescriptorHandleForStart();
	m_RTVDescriptor.Offset(rtvHandle, m_CurrentBackBufferIndex);

	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = m_DSVDescriptor.GetCPUDescriptorHandleForStart();
	
	m_UIManager.Begin(L"Scene Settings");

	static std::array<float, 4> clearColor{0.01f, 0.01f, 0.01f, 1.0f};
	m_UIManager.SetClearColor(clearColor);

	gfx::utils::ClearRTV(commandList.Get(), rtvHandle, clearColor);
	
	gfx::utils::ClearDepthBuffer(commandList.Get(), dsvHandle);

	commandList->OMSetRenderTargets(1u, &rtvHandle, FALSE, &dsvHandle);

	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	LightingData lightingData
	{
		.lightPosition = {m_LightSource.GetTransform().translate.x, m_LightSource.GetTransform().translate.y, m_LightSource.GetTransform().translate.z, 1.0f},
		.cameraPosition = m_Camera.m_CameraPosition
	};

	for (auto& [objectName, gameObject] : m_GameObjects)
	{
		auto textureGPUHandle = gameObject.GetMaterial().m_BaseColorDescriptorHandle;
		if (!textureGPUHandle.ptr)
		{
			// In future add a default texture (black or white) here.
		}

		commandList->SetGraphicsRootDescriptorTable(EnumClassValue(RootParameterIndex::DescriptorTable), textureGPUHandle);
		commandList->SetGraphicsRootConstantBufferView(EnumClassValue(RootParameterIndex::ConstantBuffer), gameObject.GetTransformCBufferVirtualAddress());
		commandList->SetGraphicsRoot32BitConstants(EnumClassValue(RootParameterIndex::RootConstant), sizeof(LightingData) / 4, &lightingData, 0u);

		gameObject.Draw(commandList.Get());
	}

	// Draw sphere (for PBR Test).
	commandList->SetPipelineState(m_PBRPSO.Get());
	commandList->SetGraphicsRootSignature(m_PBRRootSignature.Get());
	
	std::array pbrMaterialDescriptorHandles
	{
		m_SphereBaseColor.GetGPUDescriptorHandle(),
		m_SphereMetalRough.GetGPUDescriptorHandle()
	};

	auto textureGPUHandle = m_TestTexture.GetGPUDescriptorHandle();


	auto pbrMaterialGPUVirutalAddress = m_PBRMaterial.GetBufferView().BufferLocation;
	commandList->SetGraphicsRootDescriptorTable(EnumClassValue(PBRRootParameterIndex::DescriptorTable), m_SphereBaseColor.GetGPUDescriptorHandle());
	commandList->SetGraphicsRootConstantBufferView(EnumClassValue(PBRRootParameterIndex::VertexConstantBuffer), m_Sphere.GetTransformCBufferVirtualAddress());
	commandList->SetGraphicsRootConstantBufferView(EnumClassValue(PBRRootParameterIndex::PixelConstantBuffer), pbrMaterialGPUVirutalAddress);
	commandList->SetGraphicsRoot32BitConstants(EnumClassValue(PBRRootParameterIndex::PixelRootConstant), sizeof(LightingData) / 4, &lightingData, 0u);

	m_Sphere.Draw(commandList.Get());

	// Draw the light source
	commandList->SetPipelineState(m_LightPSO.Get());
	commandList->SetGraphicsRootSignature(m_LightRootSignature.Get());

	m_LightSource.UpdateTransformData(commandList.Get(), projectionView);
	commandList->SetGraphicsRootConstantBufferView(0u, m_LightSource.GetTransformCBufferVirtualAddress());

	m_LightSource.Draw(commandList.Get());

	m_UIManager.End();
	m_UIManager.FrameEnd(commandList.Get());

	gfx::utils::TransitionResource(commandList.Get(), currentBackBuffer.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);

	m_FrameFenceValues[m_CurrentBackBufferIndex] = m_CommandQueue.ExecuteCommandList(commandList.Get());

	uint32_t syncInterval = m_VSync ? 1u : 0u;
	uint32_t presentFlags = m_IsTearingSupported && !m_VSync ? DXGI_PRESENT_ALLOW_TEARING : 0u;

	ThrowIfFailed(m_SwapChain->Present(syncInterval, presentFlags));

	m_CurrentBackBufferIndex = m_SwapChain->GetCurrentBackBufferIndex();

	m_CommandQueue.WaitForFenceValue(m_FrameFenceValues[m_CurrentBackBufferIndex]);
	
	m_FrameIndex++;
}

void SandBox::OnDestroy()
{
	m_CommandQueue.FlushQueue();

	m_UIManager.ShutDown();
}

void SandBox::OnKeyAction(uint8_t keycode, bool isKeyDown)
{
	if (isKeyDown)
	{
		if (keycode == VK_SPACE)
		{
			m_FOV -= static_cast<float>(Application::GetTimer().GetDeltaTime() * 10);
		}
	}

	m_Camera.HandleInput(keycode, isKeyDown);
}

void SandBox::OnResize()
{
	if (m_Width != Application::GetClientWidth() || m_Height != Application::GetClientHeight())
	{
		m_CommandQueue.FlushQueue();

		for (int i = 0; i < NUMBER_OF_FRAMES; i++)
		{
			m_BackBuffers[i].Reset();
			m_FrameFenceValues[i] = m_FrameFenceValues[m_CurrentBackBufferIndex];
		}
		
		DXGI_SWAP_CHAIN_DESC swapChainDesc{};
		ThrowIfFailed(m_SwapChain->GetDesc(&swapChainDesc));
		ThrowIfFailed(m_SwapChain->ResizeBuffers(NUMBER_OF_FRAMES, Application::GetClientWidth(), Application::GetClientHeight(), swapChainDesc.BufferDesc.Format, swapChainDesc.Flags));

		m_CurrentBackBufferIndex = m_SwapChain->GetCurrentBackBufferIndex();

		m_Width = Application::GetClientWidth();
		m_Height = Application::GetClientHeight();

		CreateBackBufferRenderTargetViews();
	}
}

void SandBox::InitRendererCore()
{
	EnableDebugLayer();
	SelectAdapter();
	CreateDevice();

	m_CommandQueue.Init(m_Device.Get(), D3D12_COMMAND_LIST_TYPE_DIRECT, L"Main Command Queue");

	CheckTearingSupport();
	CreateSwapChain();

	m_RTVDescriptor.Init(m_Device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, NUMBER_OF_FRAMES, L"RTV Descriptor");

	m_DSVDescriptor.Init(m_Device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 1u, L"DSV Descriptor");

	// Creating 15 descriptor heap slots as of now, just an arbitruary number.
	m_SRV_CBV_UAV_Descriptor.Init(m_Device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 15u, L"SRV_CBV_UAV Descriptor");

	CreateBackBufferRenderTargetViews();
	CreateDepthBuffer();

	m_Viewport =
	{
		.TopLeftX = 0.0f,
		.TopLeftY = 0.0f,
		.Width = static_cast<float>(m_Width),
		.Height = static_cast<float>(m_Height),
		.MinDepth = 0.0f,
		.MaxDepth = 1.0f
	};

	m_UIManager.Init(m_Device.Get(), NUMBER_OF_FRAMES, m_SRV_CBV_UAV_Descriptor);
}

void SandBox::LoadContent()
{
	// Reset command list and allocator for initial setup.
	auto commandList = m_CommandQueue.GetCommandList();

	// Check for highest available version for root signature. Version_1_1 provides driver optimizations.
	D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData =
	{
		.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1
	};

	if (FAILED(m_Device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
	{
		featureData =
		{
			.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0
		};
	}

	// Create static samplers descs.
	D3D12_STATIC_SAMPLER_DESC clampStaticSamplerDesc
	{
		.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT,
		.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		.MipLODBias = 0.0f,
		.MaxAnisotropy = 0u,
		.ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL,
		.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE,
		.MinLOD = 0.0f,
		.MaxLOD = D3D12_FLOAT32_MAX,
		.ShaderRegister = 0u,
		.RegisterSpace = 1u,
		.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL,
	};

	D3D12_STATIC_SAMPLER_DESC wrapStaticSamplerDesc
	{
		.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT,
		.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		.MipLODBias = 0.0f,
		.MaxAnisotropy = 0u,
		.ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL,
		.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE,
		.MinLOD = 0.0f,
		.MaxLOD = D3D12_FLOAT32_MAX,
		.ShaderRegister = 1u,
		.RegisterSpace = 1u,
		.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL,
	};

	std::array<D3D12_STATIC_SAMPLER_DESC, 2> samplers
	{
		clampStaticSamplerDesc,
		wrapStaticSamplerDesc

	};

	// Create General Root signature.
	// Note : There is no need for descriptor ranges to be in a array, it is for keeping track of all descriptor ranges used for each root signature.
	std::array<CD3DX12_DESCRIPTOR_RANGE1, 2> descriptorRanges{};
	descriptorRanges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1u, 0u, EnumClassValue(ShaderRegisterSpace::VertexShader), D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);
	descriptorRanges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1u, 0u, EnumClassValue(ShaderRegisterSpace::PixelShader), D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);

	std::array<CD3DX12_ROOT_PARAMETER1, 3> rootParameters{};
	rootParameters[0].InitAsConstantBufferView(0u, EnumClassValue(ShaderRegisterSpace::VertexShader), D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC, D3D12_SHADER_VISIBILITY_VERTEX);
	rootParameters[1].InitAsConstants(sizeof(LightingData) / 4, 0u, EnumClassValue(ShaderRegisterSpace::PixelShader), D3D12_SHADER_VISIBILITY_PIXEL);
	rootParameters[2].InitAsDescriptorTable(1u, &descriptorRanges[1], D3D12_SHADER_VISIBILITY_PIXEL);

	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc{};
	rootSignatureDesc.Init_1_1(static_cast<uint32_t>(rootParameters.size()), rootParameters.data(), static_cast<uint32_t>(samplers.size()), samplers.data(), D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	wrl::ComPtr<ID3DBlob> rootSignatureBlob;
	wrl::ComPtr<ID3DBlob> errorBlob;

	ThrowIfFailed(::D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &rootSignatureBlob, &errorBlob));
	ThrowIfFailed(m_Device->CreateRootSignature(0, rootSignatureBlob->GetBufferPointer(), rootSignatureBlob->GetBufferSize(), IID_PPV_ARGS(&m_RootSignature)));
	m_RootSignature->SetName(L"Root Signature");

	// Create general PSO and shaders.
	wrl::ComPtr<ID3DBlob> testVertexShaderBlob;
	wrl::ComPtr<ID3DBlob> testPixelShaderBlob;

	ThrowIfFailed(D3DReadFileToBlob(L"Shaders/TestVS.cso", &testVertexShaderBlob));
	ThrowIfFailed(D3DReadFileToBlob(L"Shaders/TestPS.cso", &testPixelShaderBlob));

	std::array<D3D12_INPUT_ELEMENT_DESC, 3> inputElementDesc
	{
		gfx::utils::CreateInputLayoutDesc("POSITION", DXGI_FORMAT_R32G32B32_FLOAT),
		gfx::utils::CreateInputLayoutDesc("NORMAL", DXGI_FORMAT_R32G32B32_FLOAT),
		gfx::utils::CreateInputLayoutDesc("TEXCOORD", DXGI_FORMAT_R32G32_FLOAT),
	};

	// Create general PSO
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = gfx::utils::CreateGraphicsPSODesc(m_RootSignature.Get(), testVertexShaderBlob.Get(), testPixelShaderBlob.Get(), inputElementDesc);
	ThrowIfFailed(m_Device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_PSO)));
	m_PSO->SetName(L"Graphics PSO");

	// Light Root signature
	std::array<CD3DX12_ROOT_PARAMETER1, 1> lightRootParameters{};
	lightRootParameters[0].InitAsConstantBufferView(EnumClassValue(RootParameterIndex::ConstantBuffer), EnumClassValue(ShaderRegisterSpace::VertexShader), D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC, D3D12_SHADER_VISIBILITY_VERTEX);

	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC lightRootSignatureDesc{};
	lightRootSignatureDesc.Init_1_1(static_cast<UINT>(lightRootParameters.size()), lightRootParameters.data(), 0u, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	wrl::ComPtr<ID3DBlob> lightRootSignatureErrorBlob;
	wrl::ComPtr<ID3DBlob> lightRootSignature;

	ThrowIfFailed(::D3D12SerializeVersionedRootSignature(&lightRootSignatureDesc, &lightRootSignature, &lightRootSignatureErrorBlob));
	ThrowIfFailed(m_Device->CreateRootSignature(0u, lightRootSignature->GetBufferPointer(), lightRootSignature->GetBufferSize(), IID_PPV_ARGS(&m_LightRootSignature)));
	m_LightRootSignature->SetName(L"Light Root Signature");

	wrl::ComPtr<ID3DBlob> lightVertexShaderBlob;
	wrl::ComPtr<ID3DBlob> lightPixelShaderBlob;

	ThrowIfFailed(::D3DReadFileToBlob(L"Shaders/LightVS.cso", &lightVertexShaderBlob));
	ThrowIfFailed(::D3DReadFileToBlob(L"Shaders/LightPS.cso", &lightPixelShaderBlob));

	// Create Light PSO
	D3D12_GRAPHICS_PIPELINE_STATE_DESC lightPsoDesc = gfx::utils::CreateGraphicsPSODesc(m_LightRootSignature.Get(), lightVertexShaderBlob.Get(), lightPixelShaderBlob.Get(), inputElementDesc);
	ThrowIfFailed(m_Device->CreateGraphicsPipelineState(&lightPsoDesc, IID_PPV_ARGS(&m_LightPSO)));
	m_LightPSO->SetName(L"Light PSO");
	
	// Create PBR Root signature.
	std::array<CD3DX12_DESCRIPTOR_RANGE1, 3> pbrDescriptorRanges{};
	pbrDescriptorRanges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1u, 0u, EnumClassValue(ShaderRegisterSpace::VertexShader), D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);
	pbrDescriptorRanges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1u, 0u, EnumClassValue(ShaderRegisterSpace::PixelShader), D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);
	pbrDescriptorRanges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2u, 0u, EnumClassValue(ShaderRegisterSpace::PixelShader), D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);

	std::array<CD3DX12_ROOT_PARAMETER1, 4> pbrRootParameters{};
	pbrRootParameters[0].InitAsConstantBufferView(0u, EnumClassValue(ShaderRegisterSpace::VertexShader), D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC, D3D12_SHADER_VISIBILITY_VERTEX);
	pbrRootParameters[1].InitAsConstantBufferView(0u, EnumClassValue(ShaderRegisterSpace::PixelShader), D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC, D3D12_SHADER_VISIBILITY_PIXEL);
	pbrRootParameters[2].InitAsConstants(sizeof(LightingData) / 4, 1u, EnumClassValue(ShaderRegisterSpace::PixelShader), D3D12_SHADER_VISIBILITY_PIXEL);
	pbrRootParameters[3].InitAsDescriptorTable(1u, &pbrDescriptorRanges[2], D3D12_SHADER_VISIBILITY_PIXEL);

	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC pbrRootSignatureDesc{};
	pbrRootSignatureDesc.Init_1_1(static_cast<uint32_t>(pbrRootParameters.size()), pbrRootParameters.data(), static_cast<uint32_t>(samplers.size()), samplers.data(), D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	ThrowIfFailed(::D3DX12SerializeVersionedRootSignature(&pbrRootSignatureDesc, featureData.HighestVersion, &rootSignatureBlob, &errorBlob));
	ThrowIfFailed(m_Device->CreateRootSignature(0, rootSignatureBlob->GetBufferPointer(), rootSignatureBlob->GetBufferSize(), IID_PPV_ARGS(&m_PBRRootSignature)));
	m_PBRRootSignature->SetName(L"PBR Root signature");

	// Create PBR PSO and shaders.
	wrl::ComPtr<ID3DBlob> pbrVertexShaderBlob;
	wrl::ComPtr<ID3DBlob> pbrPixelShaderBlob;

	ThrowIfFailed(D3DReadFileToBlob(L"Shaders/PBRVertex.cso", &pbrVertexShaderBlob));
	ThrowIfFailed(D3DReadFileToBlob(L"Shaders/PBRPixel.cso", &pbrPixelShaderBlob));

	// Create PBR general PSO
	D3D12_GRAPHICS_PIPELINE_STATE_DESC pbrPsoDesc = gfx::utils::CreateGraphicsPSODesc(m_PBRRootSignature.Get(), pbrVertexShaderBlob.Get(), pbrPixelShaderBlob.Get(), inputElementDesc);
	
	ThrowIfFailed(m_Device->CreateGraphicsPipelineState(&pbrPsoDesc, IID_PPV_ARGS(&m_PBRPSO)));
	m_PBRPSO->SetName(L"PBR PSO");

	// Load data and textures.
	m_TestTexture.Init(m_Device.Get(), commandList.Get(), m_SRV_CBV_UAV_Descriptor, L"Assets/Textures/TestTexture.png", L"Test Texture");

	m_MarbleTexture.Init(m_Device.Get(), commandList.Get(), m_SRV_CBV_UAV_Descriptor, L"Assets/Textures/Marble.jpg", L"Marble Texture");

	m_SphereBaseColor.Init(m_Device.Get(), commandList.Get(), m_SRV_CBV_UAV_Descriptor, L"Assets/Models/MetalRoughSpheres/glTF/Spheres_BaseColor.png", L"Sphere Base Color Texture", true);

	m_SphereMetalRough.Init(m_Device.Get(), commandList.Get(), m_SRV_CBV_UAV_Descriptor, L"Assets/Models/MetalRoughSpheres/glTF/Spheres_MetalRough.png", L"Sphere Roughness Metallic Texture", false);

	m_PBRMaterial.Init(m_Device.Get(), commandList.Get(), MaterialData{ .albedo = dx::XMFLOAT3(1.0f, 1.0f, 1.0f), .roughnessFactor = 0.1f }, m_SRV_CBV_UAV_Descriptor, L"Material PBR CBuffer");

	m_GameObjects[L"Cube"].Init(m_Device.Get(), commandList.Get(), L"Assets/Models/Cube/Cube.gltf", m_SRV_CBV_UAV_Descriptor, Material{.m_BaseColorDescriptorHandle = m_TestTexture.GetGPUDescriptorHandle()});
	m_GameObjects[L"Cube"].GetTransform().translate = dx::XMFLOAT3(0.0f, 5.0f, 0.0f);

	m_GameObjects[L"Floor"].Init(m_Device.Get(), commandList.Get(), L"Assets/Models/Cube/Cube.gltf", m_SRV_CBV_UAV_Descriptor, Material{.m_BaseColorDescriptorHandle = m_MarbleTexture.GetGPUDescriptorHandle()});
	m_GameObjects[L"Floor"].GetTransform().translate = dx::XMFLOAT3(0.0f, -2.0f, 0.0f);
	m_GameObjects[L"Floor"].GetTransform().scale = dx::XMFLOAT3(10.0f, 0.1f, 10.0f);

	m_LightSource.Init(m_Device.Get(), commandList.Get(), L"Assets/Models/Cube/Cube.gltf", m_SRV_CBV_UAV_Descriptor);
	m_LightSource.GetTransform().scale = dx::XMFLOAT3(0.1f, 0.1f, 0.1f);

	m_Sphere.Init(m_Device.Get(), commandList.Get(), L"Assets/Models/MetalRoughSpheres/glTF/MetalRoughSpheres.gltf", m_SRV_CBV_UAV_Descriptor);

	// Close command list and execute it (for the initial setup).
	m_FrameFenceValues[m_CurrentBackBufferIndex] =  m_CommandQueue.ExecuteCommandList(commandList.Get());
	m_CommandQueue.FlushQueue();
}

void SandBox::EnableDebugLayer()
{
#ifdef _DEBUG
	ThrowIfFailed(::D3D12GetDebugInterface(IID_PPV_ARGS(&m_DebugInterface)));
	m_DebugInterface->EnableDebugLayer();
	m_DebugInterface->SetEnableGPUBasedValidation(TRUE);
	m_DebugInterface->SetEnableSynchronizedCommandQueueValidation(TRUE);
#endif
}

void SandBox::SelectAdapter()
{
	wrl::ComPtr<IDXGIFactory4> dxgiFactory;
	UINT createFactoryFlags = 0;

#ifdef _DEBUG
	createFactoryFlags = DXGI_CREATE_FACTORY_DEBUG;
#endif

	ThrowIfFailed(::CreateDXGIFactory2(createFactoryFlags, IID_PPV_ARGS(&dxgiFactory)));

	wrl::ComPtr<IDXGIAdapter4> adapter4;
	wrl::ComPtr<IDXGIAdapter1> adapter1;

	// Prefer adapter with highest available dedicated video memory.
	SIZE_T maximumDedicatedVideoMemory{};
	for (UINT i = 0; dxgiFactory->EnumAdapters1(i, &adapter1) != DXGI_ERROR_NOT_FOUND; ++i)
	{
		DXGI_ADAPTER_DESC1 adapterDesc{};
		adapter1->GetDesc1(&adapterDesc);

		if ((!(adapterDesc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) &&
			(SUCCEEDED(::D3D12CreateDevice(adapter1.Get(), D3D_FEATURE_LEVEL_12_1, __uuidof(ID3D12Device), nullptr))) &&
			(adapterDesc.DedicatedVideoMemory > maximumDedicatedVideoMemory))
		{
			maximumDedicatedVideoMemory = adapterDesc.DedicatedVideoMemory;
			ThrowIfFailed(adapter1.As(&adapter4));
		}
	}

	m_Adapter = adapter4;
}

void SandBox::CreateDevice()
{
	ThrowIfFailed(::D3D12CreateDevice(m_Adapter.Get(), D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(&m_Device)));
	m_Device->SetName(L"D3D12 Device");

	// Set break points on certain severity levels in debug mode.
#ifdef _DEBUG
	wrl::ComPtr<ID3D12InfoQueue> infoQueue;
	ThrowIfFailed(m_Device.As(&infoQueue));

	infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
	infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
	infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, TRUE);

	// Configure queue filter to ignore info message severity.
	std::array<D3D12_MESSAGE_SEVERITY, 1> ignoreMessageSeverities
	{
		D3D12_MESSAGE_SEVERITY_INFO
	};

	D3D12_INFO_QUEUE_FILTER infoQueueFilter
	{
		.DenyList
		{
			.NumSeverities = static_cast<UINT>(ignoreMessageSeverities.size()),
			.pSeverityList = ignoreMessageSeverities.data()
		},
	};

	ThrowIfFailed(infoQueue->PushStorageFilter(&infoQueueFilter));
		
#endif
}

void SandBox::CheckTearingSupport()
{
	BOOL allowTearing = TRUE;
	wrl::ComPtr<IDXGIFactory5> dxgiFactory;
	
	ThrowIfFailed(CreateDXGIFactory(IID_PPV_ARGS(&dxgiFactory)));
	if (FAILED(dxgiFactory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing, sizeof(allowTearing))))
	{
		allowTearing = FALSE;
	}

	m_IsTearingSupported = allowTearing;
}

void SandBox::CreateSwapChain()
{
	wrl::ComPtr<IDXGIFactory4> dxgiFactory;
	UINT createFactoryFlags = 0;

#ifdef _DEBUG
	createFactoryFlags = DXGI_CREATE_FACTORY_DEBUG;
#endif

	ThrowIfFailed(::CreateDXGIFactory2(createFactoryFlags, IID_PPV_ARGS(&dxgiFactory)));

	DXGI_SWAP_CHAIN_DESC1 swapChainDesc
	{
		.Width = m_Width,
		.Height = m_Height,
		.Format = DXGI_FORMAT_R8G8B8A8_UNORM,
		.Stereo = FALSE,
		.SampleDesc
		{
			.Count = 1,
			.Quality = 0,
		},
		.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
		.BufferCount = NUMBER_OF_FRAMES,
		.Scaling = DXGI_SCALING_STRETCH,
		.SwapEffect = m_VSync ? DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL : DXGI_SWAP_EFFECT_FLIP_DISCARD,
		.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED,
		.Flags = m_IsTearingSupported && !m_VSync ? DXGI_PRESENT_ALLOW_TEARING : 0u
	};

	wrl::ComPtr<IDXGISwapChain1> swapChain1;
	ThrowIfFailed(dxgiFactory->CreateSwapChainForHwnd(m_CommandQueue.GetCommandQueue().Get(), Application::GetWindowHandle(), &swapChainDesc, nullptr, nullptr, &swapChain1));

	// Prevent DXGI from switching to full screen state automatically while using ALT + ENTER combination.
	ThrowIfFailed(dxgiFactory->MakeWindowAssociation(Application::GetWindowHandle(), DXGI_MWA_NO_ALT_ENTER));
	
	ThrowIfFailed(swapChain1.As(&m_SwapChain));

	m_CurrentBackBufferIndex = m_SwapChain->GetCurrentBackBufferIndex();
}

void SandBox::CreateBackBufferRenderTargetViews()
{
	for (int i = 0; i < NUMBER_OF_FRAMES; ++i)
	{
		wrl::ComPtr<ID3D12Resource> backBuffer;
		ThrowIfFailed(m_SwapChain->GetBuffer(i, IID_PPV_ARGS(&backBuffer)));

		m_Device->CreateRenderTargetView(backBuffer.Get(), nullptr, m_RTVDescriptor.GetCurrentCPUDescriptorHandle());

		m_BackBuffers[i] = backBuffer;

		m_RTVDescriptor.OffsetCurrentCPUDescriptor();
	}
}

void SandBox::CreateDepthBuffer()
{
	D3D12_CLEAR_VALUE clearValue
	{
		.Format = DXGI_FORMAT_D32_FLOAT,
		.DepthStencil
		{
			.Depth = 1.0f,
			.Stencil = 0u
		}
	};

	CD3DX12_HEAP_PROPERTIES defaultHeapProperties(D3D12_HEAP_TYPE_DEFAULT);
	CD3DX12_RESOURCE_DESC depthTextureResourceDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_D32_FLOAT, m_Width, m_Height, 1u, 0u, 1u, 0u, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);

	ThrowIfFailed(m_Device->CreateCommittedResource(&defaultHeapProperties, D3D12_HEAP_FLAG_NONE, &depthTextureResourceDesc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &clearValue, IID_PPV_ARGS(&m_DepthBuffer)));

	// Create DSV for the texture
	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc
	{
		.Format = DXGI_FORMAT_D32_FLOAT,
		.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D,
		.Flags = D3D12_DSV_FLAG_NONE,
		.Texture2D
		{
			.MipSlice = 0u,
		}
	};

	m_Device->CreateDepthStencilView(m_DepthBuffer.Get(), &dsvDesc, m_DSVDescriptor.GetCurrentCPUDescriptorHandle());
	m_DSVDescriptor.OffsetCurrentCPUDescriptor();
}

